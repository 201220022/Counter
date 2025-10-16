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

template <class Graph>
parlay::sequence<typename Graph::NodeId> MIS(const Graph& G) {
    using NodeId = typename Graph::NodeId;
    size_t n = G.n;
    enum Status : uint8_t { UNDECIDED = 0, SELECTED = 1, REMOVED = 2 };
    parlay::sequence<std::atomic<uint8_t>> status(n);
    parlay::sequence<std::atomic<int>> neighbor_selected_count(n);
    parlay::sequence<uint64_t> priority(n);
    parlay::parallel_for(0, n, [&](size_t i) {
        status[i].store(UNDECIDED, std::memory_order_relaxed);
        neighbor_selected_count[i].store(0, std::memory_order_relaxed);
        priority[i] = parlay::hash64(i + 1);
    });

    auto active_set = parlay::tabulate<NodeId>(n, [](size_t i) { return i; });
    while (active_set.size() > 0) {
        // 在active_set中过滤出frontier
        auto frontier = parlay::filter(active_set, [&](NodeId u) {
            // 不要已选的、移除了的
            if (status[u] != UNDECIDED) return false;
            // 不要有邻居被选中的
            if (neighbor_selected_count[u].load() > 0) return false;
            // 不要有更高优先级的邻居的
            bool higher_neighbor = false;
            for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
                NodeId v = G.edges[e].v;
                if (status[v] == UNDECIDED && priority[v] > priority[u]) {
                higher_neighbor = true;
                break;
                }
            }
            return !higher_neighbor;
        });

        // frontier 全部标记为 SELECTED
        parlay::parallel_for(0, frontier.size(), [&](size_t i) {
            status[frontier[i]].store(SELECTED, std::memory_order_relaxed);
        });

        // 更新 frontier 的邻居的计数
        parlay::parallel_for(0, frontier.size(), [&](size_t i) {
            NodeId u = frontier[i];
            for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
                NodeId v = G.edges[e].v;
                if (status[v] == UNDECIDED) neighbor_selected_count[v].fetch_add(1, std::memory_order_relaxed);
            }
        });

        // 将有已选邻居的节点移除
        parlay::parallel_for(0, n, [&](size_t u) {
            if (status[u] == UNDECIDED && neighbor_selected_count[u].load() > 0)
                status[u].store(REMOVED, std::memory_order_relaxed);
        });

        // 更新未决节点
        active_set = parlay::filter(active_set, [&](NodeId u) {
            return status[u] == UNDECIDED;
        });
    }

    auto mis = parlay::filter(parlay::iota<NodeId>(n), [&](NodeId u) {
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