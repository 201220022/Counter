#include "graph.h"
#include <vector>
#include <iostream>
#include <sstream> 
#include <unordered_set>

#include "parlay/parallel.h"
#include "parlay/primitives.h"
#include "parlay/random.h"
#include "graph.h"
#include "counter.h"
#include "tools.h"
#include <atomic>
#include <iostream>
#include <chrono>
using namespace parlay;

// 设置概率              用法：P(a)
static inline int P(Counter& c) noexcept { return c.p([] { 
    return 1;
});}

template <class Graph>
parlay::sequence<typename Graph::NodeId> MIS(const Graph& G) {
    internal::timer t1;
    int round = 1;
    std::cout << "\n预处理: " << t1.total_time() << " ";

    using NodeId = typename Graph::NodeId;
    size_t n = G.n;
    enum Status : uint64_t { UNDECIDED = 0, SELECTED = 1, REMOVED = 2 };
    sequence<std::atomic<uint64_t>> status(n);                  // status:   顶点当前的状态
    sequence<double> priority(n);                               // priority: 顶点的优先级
    parallel_for(0, n, [&](size_t u) {
        status[u].store(UNDECIDED, std::memory_order_relaxed);
        uint32_t r = parlay::hash32(u * 2654435761u);
        double deg = 1.0 + (G.offsets[u + 1] - G.offsets[u]);
        priority[u] = (double)r / ((double)UINT32_MAX * deg);
    });
    std::cout << t1.total_time() << " ";
    // counter: verified_value = 0, approxmt_count = 0, targeted_value = 顶点的邻居比他优先级更高并且未被处理的数量
    sequence<Counter> counter = parlay::tabulate(n, [&](size_t u) {
        int count = 0;
        int pu = priority[u];
        size_t r = G.offsets[u + 1];
        for (size_t e = G.offsets[u]; e < r; e++) {
            count += (priority[G.edges[e].v] > pu);
        }
        return Counter(count);
    });
    std::cout << t1.total_time() << " ";
    //show_counter(counter, n);
    sequence<NodeId> frontier = filter(                          // frontier: 准备标记Selected的点，初始化为counter为0的
        iota<NodeId>(n),
        [&](NodeId u) { return counter[u].is_zero(); }
    );
    std::cout << t1.total_time() << " ";

    while (!frontier.empty()) {

        std::cout << "\nRound: " << round << "    "; round++;
        std::cout << t1.total_time() << " ";

        // step 1: frontier里面的点全部标记 Selected
        parallel_for(0, frontier.size(), [&](size_t i) {
            status[frontier[i]].store(SELECTED, std::memory_order_relaxed);
        });

        // step 2: 初始化下一轮 frontier 的写入空间
        parlay::sequence<NodeId> next_frontier = parlay::sequence<NodeId>::uninitialized(G.m);
        std::atomic<size_t> write_ptr = 0;
        
        std::cout << t1.total_time() << " ";
        // step 3: frontier的邻居全部设置为Removed, 邻居的邻居的计数器看情况调整
        parallel_for(0, frontier.size(), [&](size_t i) {
            NodeId u = frontier[i];
            for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
                // v: frontier的邻居
                NodeId v = G.edges[e].v;
                uint64_t expected = UNDECIDED;
                // 原子地访问邻居，避免两个线程重复工作
                if (status[v].compare_exchange_strong(expected, REMOVED)) {
                    // 只有成功设置了Removed的邻居能进来
                    // 邻居的邻居中，如果优先级低，则计数器--
                    for (size_t f = G.offsets[v]; f < G.offsets[v + 1]; f++) {
                        // w: frontier的邻居的邻居
                        NodeId w = G.edges[f].v;
                        if (status[w].load() == UNDECIDED && priority[w] < priority[v]) {
                            counter[w]--;
                            // 生成新的frontier
                            if (counter[w].is_zero()) { 
                                size_t pos = write_ptr.fetch_add(1);
                                next_frontier[pos] = w;
                            }
                        }
                    }
                }
            }
        });
        std::cout << t1.total_time() << " ";

        // step 4: 去重 + 切割有效部分
        size_t new_size = write_ptr.load();
        next_frontier = parlay::to_sequence(next_frontier.cut(0, new_size));
        next_frontier = parlay::unique(parlay::sort(next_frontier));

        // step 5: 更新 frontier
        frontier = std::move(next_frontier);
        std::cout << t1.total_time() << " ";
    }

    std::cout << "\n出循环：" << t1.total_time() << " ";
    // 过滤出Selected，返回
    auto mis = filter(iota<NodeId>(n), [&](NodeId u) {
        return status[u] == SELECTED;
    });
    std::cout << t1.total_time() << " ";
    return mis;
}



int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./mis input_graph" << std::endl;
        return 1;
    }
    const char* filename = argv[1];
    Graph<uint32_t, uint64_t> G;
    G.read_graph(filename);
    if (!G.symmetrized) { 
        G = make_symmetrized(G); 
    }
    std::cout << "Warming up (dry run)..." << std::endl;
    {
        auto tmp = MIS(G); 
    }
    std::vector<double> times;
    std::vector<size_t> sizes;
    std::cout << "Running MIS on " << filename << std::endl;
    for (int run = 1; run <= 3; run++) {
        internal::timer t;
        auto mis_set = MIS(G);
        t.stop();
        double elapsed = t.total_time();
        times.push_back(elapsed);
        sizes.push_back(mis_set.size());
        std::cout << "Run " << run << ": " << elapsed << " s" << std::endl;
    }
    double avg_time = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    size_t last_size = sizes.back();
    std::cout << "Average time (3 runs): " << avg_time << " s\n";
    std::cout << "MIS size (last run): " << last_size << " / " << G.n << std::endl;
    // 验证最后一次 MIS
    auto mis_set = MIS(G);
    auto mis_flags = parlay::sequence<bool>(G.n, false);
    parlay::parallel_for(0, mis_set.size(), [&](size_t i) {
        mis_flags[mis_set[i]] = true;
    });
    auto bad_edges_count = parlay::delayed_seq<size_t>(G.n, [&](size_t u) {
        if (!mis_flags[u]) return (size_t)0;
        size_t local_conflicts = 0;
        for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
            if (mis_flags[G.edges[e].v]) {
                local_conflicts++;
            }
        }
        return local_conflicts;
    });
    size_t bad_edges = parlay::reduce(bad_edges_count);
    if (bad_edges == 0) {
        std::cout << "✅ Verified: MIS is valid (no edge inside set)" << std::endl;
    } else {
        std::cout << "❌ MIS invalid: " << bad_edges << " conflicting edges" << std::endl;
    }
    return 0;
}