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
    for (size_t i = 0; i < n; i++) status[i].store(UNDECIDED, std::memory_order_relaxed);
    parlay::random_generator gen;
    auto priorities = parlay::tabulate<int>(n, [&](size_t i) {
        return static_cast<int>(parlay::hash64(i));
    });
    auto undecided = parlay::tabulate<NodeId>(n, [](size_t i) { return i; });

    while (undecided.size() > 0) {
        auto can_select = parlay::filter(undecided, [&](NodeId u) {
        bool can_choose = true;
        for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
            NodeId v = G.edges[e].v;
            if (status[v] == SELECTED) return false;
            if (status[v] == UNDECIDED && priorities[v] < priorities[u]) {
            can_choose = false;
            break;
            }
        }
        return can_choose;
        });
        parlay::parallel_for(0, can_select.size(), [&](size_t i) {
        status[can_select[i]].store(SELECTED, std::memory_order_relaxed);
        });
        parlay::parallel_for(0, can_select.size(), [&](size_t i) {
        NodeId u = can_select[i];
        for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
            NodeId v = G.edges[e].v;
            uint8_t expected = UNDECIDED;
            status[v].compare_exchange_strong(expected, REMOVED);
        }
        });
        undecided = parlay::filter(undecided, [&](NodeId u) {
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