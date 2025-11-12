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

void show_counter(sequence<Counter>& counter, size_t n, string graphname){
    std::vector<int> values(n);
    parallel_for(0, n, [&](size_t i) {
        values[i] = counter[i].get_verified();
    });

    std::ofstream ofs1("counter_distribution/" + graphname + ".txt");
    for (int v : values)
        ofs1 << v << "\n";
    ofs1.close();

    // 计算基础统计信息
    long long sum = 0;
    int min_val = INT_MAX, max_val = INT_MIN;
    for (int v : values) {
        sum += v;
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
    }
    double mean = static_cast<double>(sum) / n;

    // 计算方差与标准差
    double var_sum = 0.0;
    for (int v : values) {
        var_sum += (v - mean) * (v - mean);
    }
    double variance = var_sum / n;
    double stddev = std::sqrt(variance);

    // 计算分位数
    std::sort(values.begin(), values.end());
    auto percentile = [&](double p) {
        size_t idx = std::min<size_t>(n - 1, static_cast<size_t>(p * n));
        return values[idx];
    };

    std::cout << "\n===== Counter Initialization Statistics =====\n";
    std::cout << "Size: " << n << "\n";
    std::cout << "Min: " << min_val << "\n";
    std::cout << "Max: " << max_val << "\n";
    std::cout << "Mean: " << mean << "\n";
    std::cout << "Std Dev: " << stddev << "\n";
    std::cout 
            << percentile(0.1) << ", " << percentile(0.2) << ", "  << percentile(0.3) << ", "  << percentile(0.4) << ", "  << percentile(0.5) << ", " 
            << percentile(0.6) << ", " << percentile(0.7) << ", "  << percentile(0.8) << ", "  << percentile(0.9) << ", "  << percentile(1.0) << "\n"; 
    std::cout 
            << percentile(0.91) << ", " << percentile(0.92) << ", "  << percentile(0.93) << ", "  << percentile(0.94) << ", "  << percentile(0.95) << ", " 
            << percentile(0.96) << ", " << percentile(0.97) << ", "  << percentile(0.98) << ", "  << percentile(0.99) << ", "  << percentile(1.00) << "\n";
    std::cout << "=============================================\n\n";
}


template <class Graph>
parlay::sequence<typename Graph::NodeId> MIS(const Graph& G, string graphname) {
    using NodeId = typename Graph::NodeId;
    size_t n = G.n;
    enum Status : uint64_t { UNDECIDED = 0, SELECTED = 1, REMOVED = 2 };
    sequence<std::atomic<uint64_t>> status(n);                    // status:  顶点当前的状态
    auto priority = parlay::random_permutation<NodeId>(n);
    // counter: verified_value = 0, approxmt_count = 0, targeted_value = 顶点的邻居比他优先级更高并且未被处理的数量
    sequence<Counter> counter = parlay::tabulate(n, [&](size_t u) {
        int count = 0;
        for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
            NodeId v = G.edges[e].v;
            if (priority[v] < priority[u]) count++;
        }
        return Counter(count);
    });
    show_counter(counter, n, graphname);
    sequence<NodeId> frontier = filter(                          // frontier: 准备标记Selected的点，初始化为counter为0的
        iota<NodeId>(n),
        [&](NodeId u) { return counter[u].is_zero(); }
    );

    std::ofstream ofs_round("round_distribution/" + graphname + ".txt");

    ofs_round << G.n << "\n";
    ofs_round << G.m << "\n";
    int round = 0;
    string frontier_size = "";

    while (!frontier.empty()) {
        round++;
        frontier_size = frontier_size + to_string(frontier.size()) + "\n";

        // step 1: frontier里面的点全部标记 Selected
        parallel_for(0, frontier.size(), [&](size_t i) {
            status[frontier[i]].store(SELECTED, std::memory_order_relaxed);
        });

        // step 2: 初始化下一轮 frontier 的写入空间
        parlay::sequence<NodeId> next_frontier = parlay::sequence<NodeId>::uninitialized(G.m);
        std::atomic<size_t> write_ptr = 0;

        const size_t WIDTH = 10000;
        for (size_t start = 0; start < frontier.size(); start += WIDTH) {
            size_t end = std::min(start + WIDTH, frontier.size());
            // step 3: frontier的邻居全部设置为Removed, 邻居的邻居的计数器看情况调整
            parallel_for(start, end, [&](size_t i) {
                NodeId u = frontier[i];
                for (size_t e = G.offsets[u]; e < G.offsets[u + 1]; e++) {
                    // v: frontier的邻居
                    NodeId v = G.edges[e].v;
                    uint64_t expected = UNDECIDED;
                    // 原子地访问邻居，避免两个线程重复工作
                    if (status[v].compare_exchange_strong(expected, REMOVED)) {
                        // 只有成功设置了Removed的邻居能进来
                        // 邻居的邻居中，如果优先级低，则计数器--
                        for (size_t f = G.offsets[v]; f < G.offsets[v + 1]; f++) {
                            // w: frontier的邻居的邻居
                            NodeId w = G.edges[f].v;
                            if (status[w].load() == UNDECIDED && priority[w] > priority[v]) {
                                counter[w]--;
                                // 生成新的frontier
                                if (counter[w].is_zero()) { 
                                    size_t pos = write_ptr.fetch_add(1);
                                    next_frontier[pos] = w;
                                }
                            }
                        }
                    }
                }
            });
        }

        // step 4: 去重 + 切割有效部分
        size_t new_size = write_ptr.load();
        next_frontier = parlay::to_sequence(next_frontier.cut(0, new_size));
        next_frontier = parlay::unique(parlay::sort(next_frontier));

        // step 5: 更新 frontier
        frontier = std::move(next_frontier);
    }

    // 过滤出Selected，返回
    auto mis = filter(iota<NodeId>(n), [&](NodeId u) {
        return status[u] == SELECTED;
    });
    ofs_round << mis.size() << "\n" << round << "\n" << frontier_size;
    ofs_round.close();
    return mis;

}

template <class NodeId>
void save_mis_to_file(const parlay::sequence<NodeId>& mis_set,
                      const std::string& filename) {
    // 拷贝到 std::vector 后排序（兼容性更好）
    std::vector<NodeId> sorted_mis(mis_set.begin(), mis_set.end());
    std::sort(sorted_mis.begin(), sorted_mis.end());

    // 如有需要，创建输出目录
    std::filesystem::path p(filename);
    if (!p.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            std::cerr << "Error: Cannot create directory "
                      << p.parent_path().string() << " : " << ec.message() << std::endl;
            return;
        }
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
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) { std::cerr << "Usage: ./mis input_graph [verify]" << std::endl; return 1; }
    const char* filename = argv[1];
    std::string graphname = std::filesystem::path(filename).stem().string();
    Graph<uint32_t, uint64_t> G;
    G.read_graph(filename);
    if (!G.symmetrized) { G = make_symmetrized(G); }
    auto mis_set = MIS(G, graphname);
    std::string output_file = "./results/" + graphname + ".txt";
    save_mis_to_file(mis_set, output_file);
    return 0;
}