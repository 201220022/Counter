#include "graph.h"
#include <vector>
#include <iostream>
#include <unordered_set>

template <class Graph>
std::vector<typename Graph::NodeId> sequential_MIS(const Graph &G) {
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