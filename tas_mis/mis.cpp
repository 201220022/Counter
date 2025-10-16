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
struct TASTree {
    std::atomic<bool> flag;               // 当前节点的标志位
    TASTree* parent;                      // 指向父节点
    std::vector<TASTree*> children;       // 子节点列表

    TASTree() : flag(false), parent(nullptr) {}
};
#include <unordered_map>

struct VertexTAS {
    std::vector<TASTree*> leaves;                // 每个阻塞邻居对应的叶子节点
    std::unordered_map<size_t, TASTree*> neighbor_to_leaf; // 邻居 -> 叶节点 映射
    TASTree* root = nullptr;
};


template <class Graph>
parlay::sequence<typename Graph::NodeId> MIS(const Graph& G) {
    using NodeId = typename Graph::NodeId;
    size_t n = G.n;

    enum Status : uint8_t { UNDECIDED, SELECTED, REMOVED };
    parlay::sequence<std::atomic<uint8_t>> status(n);
    for (size_t i = 0; i < n; i++) status[i].store(UNDECIDED);

    // 随机优先级
    auto priorities = parlay::tabulate<uint64_t>(n, [&](size_t i) {
        return parlay::hash64(i+2);
    });

    // -------------------------------
    // 构建每个节点的 TAS 树
    // -------------------------------
    parlay::sequence<VertexTAS> TAS_info(n);
    parlay::parallel_for(0, n, [&](size_t v) {
        // 找出比 v 优先级高的邻居（阻塞者）
        std::vector<NodeId> blockers;
        for (size_t e = G.offsets[v]; e < G.offsets[v + 1]; e++) {
        NodeId u = G.edges[e].v;
        if (priorities[u] > priorities[v]) blockers.push_back(u);
        }

        // 构建二叉 TAS 树（简单完全二叉树结构）
        if (blockers.empty()) {
        TAS_info[v].root = new TASTree();
        return;
        }

        // 叶子节点
        std::vector<TASTree*> leaves(blockers.size());
        for (size_t i = 0; i < blockers.size(); i++) {
            leaves[i] = new TASTree();
            TAS_info[v].neighbor_to_leaf[blockers[i]] = leaves[i];
        }

        // 自底向上构建二叉树
        auto build_tree = [&](auto&& self, std::vector<TASTree*>& nodes) -> TASTree* {
        if (nodes.size() == 1) return nodes[0];
        std::vector<TASTree*> parents;
        for (size_t i = 0; i < nodes.size(); i += 2) {
            TASTree* p = new TASTree();
            nodes[i]->parent = p;
            p->children.push_back(nodes[i]);
            if (i + 1 < nodes.size()) {
            nodes[i + 1]->parent = p;
            p->children.push_back(nodes[i + 1]);
            }
            parents.push_back(p);
        }
        return self(self, parents);
        };

        TAS_info[v].root = build_tree(build_tree, leaves);
        TAS_info[v].leaves = std::move(leaves);
    });

    // -------------------------------
    // 定义异步唤醒函数
    // -------------------------------
    std::function<void(NodeId)> WakeUp = [&](NodeId v) {
        if (status[v].exchange(SELECTED) != UNDECIDED) return;

        // 移除邻居
        for (size_t e = G.offsets[v]; e < G.offsets[v + 1]; e++) {
        NodeId u = G.edges[e].v;
        uint8_t expected = UNDECIDED;
            if (status[u].compare_exchange_strong(expected, REMOVED)) {
                for (size_t e2 = G.offsets[u]; e2 < G.offsets[u + 1]; e2++) {
                    NodeId w = G.edges[e2].v;
                    if (priorities[w] < priorities[u]) continue;

                    auto& info = TAS_info[w];
                    if (info.leaves.empty()) continue;

                    auto it = info.neighbor_to_leaf.find(u);
                    if (it == info.neighbor_to_leaf.end()) continue;

                    TASTree* leaf = it->second;
                    if (!leaf->flag.exchange(true)) {
                    TASTree* p = leaf->parent;
                    while (p && !p->flag.exchange(true)) {
                        p = p->parent;
                    }
                    if (p == nullptr && status[w].load() == UNDECIDED) {
                        WakeUp(w);
                    }
                    }
                }
            }
        }
    };
    parlay::parallel_for(0, n, [&](size_t v) {
        if (TAS_info[v].leaves.empty()) {
        WakeUp(v);
        }
    });
    return parlay::filter(parlay::iota<NodeId>(n), [&](NodeId v){ return status[v] == SELECTED; });
}


// 用法示例
int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: ./mis -i input_graph" << std::endl;
    return 1;
  }

  const char* filename = argv[1];
  Graph<uint32_t, uint64_t> G;
  G.read_graph(filename);
  if (!G.symmetrized) { G = make_symmetrized(G); }

  std::cout << "Running parallel MIS..." << std::endl;
  auto mis_set = MIS(G);
  std::cout << "MIS size: " << mis_set.size() << " / " << G.n << std::endl;

  // 简单验证：检查没有边内两点都在 MIS 中
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
