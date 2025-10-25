#include "graph.h"
#include <vector>
#include <iostream>
#include <unordered_set>
using namespace parlay;

template <class Graph>
std::vector<typename Graph::NodeId> MIS(const Graph &G) {
    using NodeId = typename Graph::NodeId;
    size_t n = G.n;
    std::vector<bool> in_MIS(n, false);  
    std::vector<bool> removed(n, false);  
    for (NodeId u = 0; u < n; u++) {
        if (!removed[u]) {
        // 将该节点加入 MIS
        in_MIS[u] = true;
        removed[u] = true;
        // 禁用所有邻居
        for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
            NodeId v = G.edges[e].v;
            removed[v] = true;
        }
        }
    }
    // 收集结果
    std::vector<NodeId> result;
    for (NodeId u = 0; u < n; u++) {
        if (in_MIS[u]) result.push_back(u);
    }
    return result;
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
    return 0;
}