#include "utils/utils.h"
#include "graph.h"
#include <vector>
#include <iostream>
#include <sstream> 
#include <unordered_set>

#include "parlay/parallel.h"
#include "parlay/primitives.h"
#include "parlay/random.h"
#include "counter.h"
#include <unordered_map>
#include <cmath>
#include <atomic>
#include <chrono>
#include <filesystem>
using namespace parlay;
using namespace std;

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) { std::cerr << "Usage: ./mis input_graph [verify]" << std::endl; return 1; }
    const char* filename = argv[1];
    std::string graphname = std::filesystem::path(filename).stem().string();
    Graph<uint32_t, uint64_t> G;
    G.read_graph(filename);
    if (!G.symmetrized) { G = make_symmetrized(G); }

    std::ifstream fin("WikiTalk_sym.txt");
    if (!fin.is_open()) {
        std::cerr << "Error: cannot open " << filename << std::endl;
        return 1;
    }
    std::vector<int> mis_set;
    std::string line;
    std::getline(fin, line);
    int x;
    while (fin >> x) {
        mis_set.push_back(x);
    }
    fin.close();

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
    if (bad_edges != 0) std::cout << "❌ MIS invalid: " << bad_edges << " conflicting edges" << std::endl;

    size_t non_maximal = parlay::count_if(parlay::iota<size_t>(G.n), [&](size_t u) {
        if (mis_flags[u]) return false; // 已选节点跳过
        for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
            if (mis_flags[G.edges[e].v]) return false; // 有邻居在 MIS 中
        }
        return true; // 没邻居在 MIS 中 → 非极大
    });
    if (non_maximal != 0) std::cout << "⚠️ MIS not maximal: " << non_maximal << " nodes could be added\n";
    return 0;
}