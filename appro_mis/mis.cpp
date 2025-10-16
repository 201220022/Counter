#include "graph.h"
#include "sampler.h"
#include "parlay/parallel.h"
#include "parlay/primitives.h"
#include "parlay/random.h"
#include <atomic>
#include <vector>
#include <iostream>

template <class Graph>
parlay::sequence<typename Graph::NodeId> MIS(const Graph& G, double sample_rate = 0.25) {
    using NodeId = typename Graph::NodeId;
    size_t n = G.n;
    enum Status : uint8_t { UNDECIDED, SELECTED, REMOVED };
    parlay::sequence<std::atomic<uint8_t>> status(n);
    for (size_t i = 0; i < n; i++) status[i].store(UNDECIDED);
    auto priorities = parlay::tabulate<uint64_t>(n, [&](size_t i) { return parlay::hash64(i+4); });
    auto samplers = parlay::tabulate<Sampler>(n, [&](size_t v) {
    size_t num_blockers = 0;
    for (size_t e = G.offsets[v]; e < G.offsets[v + 1]; e++) {
        NodeId u = G.edges[e].v;
        if (priorities[u] > priorities[v]) num_blockers=10;
    }
    return Sampler(num_blockers, sample_rate);
    });
    auto ready = parlay::filter(parlay::iota<NodeId>(n), [&](NodeId v) {
        return samplers[v].get_exp_hits() == 0;
    });
    while (!ready.empty()) {
        parlay::parallel_for(0, ready.size(), [&](size_t i) {
        NodeId v = ready[i];
        if (status[v].exchange(SELECTED) != UNDECIDED) return;
        for (size_t e = G.offsets[v]; e < G.offsets[v + 1]; e++) {
            NodeId u = G.edges[e].v;
            uint8_t expected = UNDECIDED;
            if (status[u].compare_exchange_strong(expected, REMOVED)) {
            for (size_t e2 = G.offsets[u]; e2 < G.offsets[u + 1]; e2++) {
                NodeId w = G.edges[e2].v;
                if (priorities[w] < priorities[u]) {
                bool callback = false;
                hash_t r = static_cast<hash_t>(parlay::hash64(u ^ w));
                if (samplers[w].sample(r, callback) && callback) {
                    if (status[w] == UNDECIDED)
                    status[w].store(SELECTED); 
                }
                }
            }
            }
        }
        });
        ready.clear();
        ready = parlay::filter(parlay::iota<NodeId>(n), [&](NodeId v) {
            return status[v] == UNDECIDED && samplers[v].get_num_hits() >= samplers[v].get_exp_hits();
        });
    }

    return parlay::filter(parlay::iota<NodeId>(n), [&](NodeId v) {
        return status[v] == SELECTED;
    });
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
