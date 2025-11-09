#include "utils/utils.h"
#include "utils/graph.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <unordered_set>
#include <vector>
using namespace parlay;

template <class Graph>
std::vector<typename Graph::NodeId> MIS(const Graph &G, bool use_permutation = true) {
    using NodeId = typename Graph::NodeId;
    size_t n = G.n;

    // Create a permutation to randomize vertex processing order (like GBBS)
    auto perm = use_permutation ? parlay::random_permutation<NodeId>(n)
                                 : parlay::sequence<NodeId>::from_function(n, [](size_t i) { return i; });

    std::vector<bool> in_MIS(n, false);
    std::vector<bool> removed(n, false);

    // in default permuted order
    for (size_t i = 0; i < n; i++) {
        NodeId u = perm[i];
        if (!removed[u]) {
            // Add this vertex 
            in_MIS[u] = true;
            removed[u] = true;
            // Mark neighbors as removed
            for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
                NodeId v = G.edges[e].v;
                removed[v] = true;
            }
        }
    }


    std::vector<NodeId> result;
    for (NodeId u = 0; u < n; u++) {
        if (in_MIS[u]) result.push_back(u);
    }
    return result;
}

template <class NodeId>
void save_mis_to_file(const std::vector<NodeId>& mis_set, const std::string& filename) {
    auto sorted_mis = mis_set;
    std::sort(sorted_mis.begin(), sorted_mis.end());

    // Create directory if needed
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
/*
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./mis input_graph" << std::endl;
        return 1;
    }
    const char* filename = argv[1];
    Graph<uint32_t, uint64_t> G;
    G.read_graph(filename);
    if (!G.symmetrized) { G = make_symmetrized(G); }
    std::cout << "Running MIS..." << std::endl;
    internal::timer t;
    auto mis_set = MIS(G);
    t.stop();
    std::cout << "Average time: " << t.total_time() << "\n";
    std::cout << "MIS size: " << mis_set.size() << " / " << G.n << std::endl;
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

    // Save MIS result to file
    std::string output_file = std::string(filename) + ".mis_result.txt";
    save_mis_to_file(mis_set, output_file);

    return 0;
}
*/


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

    std::string output_file = "./results/test.txt";
    save_mis_to_file(mis_set, output_file);

    return 0;
}