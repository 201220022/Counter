#include "utils/utils.h"
#include "utils/graph.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
// #include <vector>
using namespace parlay;

template <class Graph>
parlay::sequence<typename Graph::NodeId> MIS_DAG(const Graph &G) {
    using NodeId = typename Graph::NodeId;
    size_t n = G.n;

    auto perm = parlay::random_permutation<NodeId>(n);

    parlay::sequence<int> priorities(n);
    for (NodeId u = 0; u < n; u++) {
        int count = 0;
        for (size_t e = G.offsets[u]; e < G.offsets[u+1]; e++) {
            NodeId v = G.edges[e].v;
            if (perm[v] < perm[u]) count++;
        }
        priorities[u] = count;
    }

    parlay::sequence<bool> in_mis(n, false);
    parlay::sequence<bool> excluded(n, false);
    size_t finished = 0;

    while (finished < n) {
        parlay::sequence<NodeId> roots;
        for (NodeId u = 0; u < n; u++) {
            if (priorities[u] == 0 && !in_mis[u] && !excluded[u]) {
                roots.push_back(u);
            }
        }

        if (roots.empty()) break;

        for (NodeId u : roots) {
            in_mis[u] = true;
        }

        parlay::sequence<bool> removed_mark(n, false);
        parlay::sequence<NodeId> removed;
        for (NodeId u : roots) {
            for (size_t e = G.offsets[u]; e < G.offsets[u+1]; e++) {
                NodeId v = G.edges[e].v;
                if (priorities[v] > 0 && !removed_mark[v]) {
                    removed.push_back(v);
                    removed_mark[v] = true;
                    excluded[v] = true;
                    priorities[v] = 0;
                }
            }
        }

        for (NodeId u : removed) {
            for (size_t e = G.offsets[u]; e < G.offsets[u+1]; e++) {
                NodeId v = G.edges[e].v;
                if (priorities[v] > 0 && perm[u] < perm[v]) {
                    priorities[v]--;
                }
            }
        }

        finished += roots.size();
        finished += removed.size();
    }

    parlay::sequence<NodeId> result;
    for (NodeId u = 0; u < n; u++) {
        if (in_mis[u]) result.push_back(u);
    }
    return result;
}

template <class NodeId>
void save_mis_to_file(const parlay::sequence<NodeId>& mis_set, const std::string& filename) {
    auto sorted_mis = parlay::sort(mis_set);

    size_t last_slash = filename.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string dir = filename.substr(0, last_slash);
        system(("mkdir -p " + dir).c_str());
    }

    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "Error: Cannot open output file " << filename << std::endl;
        return;
    }

    out << "# MIS size: " << sorted_mis.size() << "\n";
    for (const auto& v : sorted_mis) {
        out << v << "\n";
    }
    out.close();
    std::cout << "MIS result saved to " << filename << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./mis_dag input_graph" << std::endl;
        return 1;
    }
    const char* filename = argv[1];
    Graph<uint32_t, uint64_t> G;
    G.read_graph(filename);
    if (!G.symmetrized) {
        G = make_symmetrized(G);
    }

    std::cout << "Warming up..." << std::endl;
    { auto tmp = MIS_DAG(G); }

    std::vector<double> times;
    std::vector<size_t> sizes;
    std::cout << "Running MIS_DAG on " << filename << std::endl;
    for (int run = 1; run <= 3; run++) {
        internal::timer t;
        auto mis_set = MIS_DAG(G);
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

    auto mis_set = MIS_DAG(G);
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

    std::string output_file = "./results/dag_mis.txt";
    save_mis_to_file(mis_set, output_file);

    return 0;
}
