#include "graph.h"
#include <vector>
#include <iostream>
#include <unordered_set>

#include "parlay/parallel.h"
#include "parlay/primitives.h"
#include "parlay/random.h"
#include "graph.h"
#include <atomic>
#include <iostream>
#include <chrono>
class Counter {
private:
    std::atomic<int> value;
public:
    Counter() : value(0) {}
    Counter(int init) : value(init) {}
    inline void increment() noexcept { value.fetch_add(1, std::memory_order_relaxed); }
    inline void decrement() noexcept { value.fetch_sub(1, std::memory_order_relaxed); }
    inline void operator++() noexcept { increment(); }
    inline void operator++(int) noexcept { increment(); }
    inline void operator--() noexcept { decrement(); }
    inline void operator--(int) noexcept { decrement(); }
    inline void add(int delta) noexcept { value.fetch_add(delta, std::memory_order_relaxed); }
    inline void sub(int delta) noexcept { value.fetch_sub(delta, std::memory_order_relaxed); }
    inline int load() const noexcept { return value.load(std::memory_order_relaxed); }
    inline void store(int x) noexcept { value.store(x, std::memory_order_relaxed); }
    inline bool is_zero() { return value.load(std::memory_order_relaxed) <= 0; }
};

using namespace parlay;

template <class Graph>
parlay::sequence<typename Graph::NodeId> MIS(const Graph& G) {
    using NodeId = typename Graph::NodeId;
    size_t n = G.n;
    enum Status : uint64_t { UNDECIDED = 0, SELECTED = 1, REMOVED = 2 };
    sequence<std::atomic<uint64_t>> status(n);                   // status:  顶点当前的状态
    sequence<Counter> counter(n);                                // counter: 顶点的邻居比他优先级更高并且未被处理的数量
    sequence<uint64_t> priority(n);                              // priority:顶点的优先级
    uint64_t seed = std::chrono::high_resolution_clock::now().time_since_epoch().count(); //随机数种子
    parallel_for(0, n, [&](size_t u) {                           // u:       遍历所有的顶点
        status[u].store(UNDECIDED, std::memory_order_relaxed);   // status初始化全是Undecided
        priority[u] = parlay::hash64(u+seed);                    // priority随机初始化                          //相同的优先级？
    });
    parallel_for(0, n, [&](size_t u) {                           // u:       遍历所有的顶点
        int count = 0;                                           // counter初始化
        for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
            NodeId v = G.edges[e].v;
            if (priority[v] > priority[u]) count++;              // 如果邻居有更高的优先级，计数器+1
        }
        counter[u].store(count);
    });

    sequence<NodeId> active = to_sequence(iota<NodeId>(n));      // active: 活跃集，一开始所有顶点都活跃
    sequence<NodeId> frontier;                                   // frontier:   前沿集，里面的元素是满足了选入MIS的要求，然后准备ban他邻居
    while (!active.empty()) {           
        // 过滤出frontier: 未处理且计数器为0                   
        frontier = filter(active, [&](NodeId u) {
            return status[u] == UNDECIDED && counter[u].is_zero();
        });
        if (frontier.empty()) break;
        // frontier全部设置为Selected
        parallel_for(0, frontier.size(), [&](size_t i) {
            status[frontier[i]].store(SELECTED, std::memory_order_relaxed);
        });
        // frontier的邻居全部设置为Removed, 邻居的邻居的计数器看情况调整
        parallel_for(0, frontier.size(), [&](size_t i) {
            NodeId u = frontier[i];
            for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
                // v: frontier的邻居
                NodeId v = G.edges[e].v;
                uint64_t expected = UNDECIDED;
                // 原子地访问邻居，避免两个线程重复工作
                if (status[v].compare_exchange_strong(expected, REMOVED,std::memory_order_acq_rel, std::memory_order_acquire)) {
                    // 只有成功设置了Removed的邻居能进来
                    // 邻居的邻居中，如果优先级低，则计数器--
                    for (size_t f = G.offsets[v]; f < G.offsets[v + 1]; f++) {
                        // w: frontier的邻居的邻居
                        NodeId w = G.edges[f].v;
                        if (status[w].load(std::memory_order_acquire) == UNDECIDED && priority[w] < priority[v]) {
                            counter[w]--;
                        }
                    }
                }
            }
        });
    }
    // 过滤出Selected，返回
    auto mis = filter(iota<NodeId>(n), [&](NodeId u) {
        return status[u] == SELECTED;
    });
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
    if (!G.symmetrized) { G = make_symmetrized(G); }
    std::cout << "Running parallel MIS..." << std::endl;
    auto mis_set = MIS(G);
    std::cout << "MIS size: " << mis_set.size() << " / " << G.n << std::endl;
    size_t bad_edges = 0;
    std::sort(mis_set.begin(), mis_set.end());
    for (auto u : mis_set) {
        for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
            if (std::binary_search(mis_set.begin(), mis_set.end(), G.edges[e].v)) {
                bad_edges++;
            }
        }
    }
    if (bad_edges == 0) {
        std::cout << "✅ Verified: MIS is valid (no edge inside set)" << std::endl;
    } else {
        std::cout << "❌ MIS invalid: " << bad_edges << " conflicting edges" << std::endl;
    }
    return 0;
}
