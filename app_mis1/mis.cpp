#include <vector>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <numeric>

#include "parlay/parallel.h"
#include "parlay/primitives.h"
#include "parlay/random.h"

#include "graph.h"
#include "counter.h"
//#include "tools.h"

#include <atomic>
#include <chrono>

using namespace parlay;

template <class Graph>
parlay::sequence<typename Graph::NodeId> MIS(const Graph& G) {
    using NodeId = typename Graph::NodeId;
    size_t n = G.n;

    enum Status : uint64_t { UNDECIDED = 0, SELECTED = 1, REMOVED = 2 };

    sequence<std::atomic<uint64_t>> status(n);
    sequence<double> priority(n);

    parallel_for(0, n, [&](size_t u) {
        status[u].store(UNDECIDED, std::memory_order_relaxed);
        uint32_t r = parlay::hash32(static_cast<uint32_t>(u) * 2654435761u);
        double deg = 1.0 + static_cast<double>(G.offsets[u + 1] - G.offsets[u]);
        priority[u] = static_cast<double>(r) / (static_cast<double>(UINT32_MAX) * deg);
    });

    // 初始化 Counter：为每个 u 精确数一遍“高优未定邻居数”
    sequence<Counter<Graph>> counter = parlay::tabulate(n, [&](size_t u) {
        int count = 0;
        for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
            NodeId v = G.edges[e].v;
            if (priority[v] > priority[u]) count++;
        }
        return Counter<Graph>(G, static_cast<NodeId>(u), &status, &priority, count);
    });

    // 初始 frontier：计数为 0 的顶点
    sequence<NodeId> frontier = filter(iota<NodeId>(n), [&](NodeId u) {
        return counter[u].is_zero();
    });

    while (!frontier.empty()) {
        // 1) 标记 SELECTED
        parallel_for(0, frontier.size(), [&](size_t i) {
            status[frontier[i]].store(SELECTED, std::memory_order_relaxed);
        });

        // 2) 预留 next_frontier 写空间
        parlay::sequence<NodeId> next_frontier = parlay::sequence<NodeId>::uninitialized(G.m);
        std::atomic<size_t> write_ptr = 0;

        // 3) 邻居设 REMOVED；邻居的邻居(按优先级)扣减
        parallel_for(0, frontier.size(), [&](size_t i) {
            NodeId u = frontier[i];
            for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
                NodeId v = G.edges[e].v;

                uint64_t expected = UNDECIDED;
                if (status[v].compare_exchange_strong(expected, REMOVED, std::memory_order_acq_rel)) {
                    for (size_t f = G.offsets[v]; f < G.offsets[v + 1]; f++) {
                        NodeId w = G.edges[f].v;
                        if (status[w].load(std::memory_order_relaxed) == UNDECIDED &&
                            priority[w] < priority[v]) {
                            // 外部采样：事件命中时直接调用 -- （内部会减 s）
                            counter[w]--;
                            if (counter[w].is_zero()) {
                                size_t pos = write_ptr.fetch_add(1, std::memory_order_relaxed);
                                next_frontier[pos] = w;
                            }
                        }
                    }
                }
            }
        });

        // 4) 去重 + 裁剪
        size_t new_size = write_ptr.load(std::memory_order_relaxed);
        next_frontier = parlay::to_sequence(next_frontier.cut(0, new_size));
        if (!next_frontier.empty()) {
            next_frontier = parlay::unique(parlay::sort(std::move(next_frontier)));
        }

        // 5) 下一轮
        frontier = std::move(next_frontier);
    }

    // 输出 SELECTED 集合
    auto mis = filter(iota<NodeId>(n), [&](NodeId u) {
        return status[u].load(std::memory_order_relaxed) == SELECTED;
    });
    return mis;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./mis input_graph" << std::endl;
        return 1;
    }
    const char* filename = argv[1];

    // 你的 graph.h 应该定义 Graph、make_symmetrized 等
    Graph G;
    G.read_graph(filename);
    if (!G.symmetrized) {
        G = make_symmetrized(G);
    }

    std::cout << "Warming up (dry run)..." << std::endl;
    {
        auto tmp = MIS(G);
        (void)tmp;
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

    // 校验 MIS
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
