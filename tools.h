#include <iostream>
#include <vector>
#include <unordered_map>
#include <cmath>
#include "counter.h"
#include "parlay/parallel.h"
#include "parlay/primitives.h"
using namespace parlay;

void show_counter(sequence<Counter>& counter, size_t n){
    std::vector<int> values(n);
    parallel_for(0, n, [&](size_t i) {
        values[i] = counter[i].get_verified();
    });

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
    std::cout << "Number of vertices: " << n << "\n";
    std::cout << "Min: " << min_val << "\n";
    std::cout << "Max: " << max_val << "\n";
    std::cout << "Mean: " << mean << "\n";
    std::cout << "Std Dev: " << stddev << "\n";
    std::cout << "Median (50%): " << percentile(0.5) << "\n";
    std::cout << "25%: " << percentile(0.25)
              << " | 75%: " << percentile(0.75) << "\n";
    std::cout << "90%: " << percentile(0.9)
              << " | 99%: " << percentile(0.99) << "\n";

    // ===== 新增部分：统计 >10, >20, ... >100 的占比 =====
    std::cout << "\nPercentage of nodes with counter value greater than thresholds:\n";
    for (int t = 10; t <= 100; t += 10) {
        size_t count = std::count_if(values.begin(), values.end(), [&](int v){ return v > t; });
        double pct = 100.0 * count / n;
        std::cout << "> " << t << ": " << pct << "% (" << count << " nodes)\n";
    }

    // 统计出现频率（粗略分布）
    std::unordered_map<int, int> freq;
    for (int v : values) freq[v]++;
    std::cout << "\nTop frequency values (value: count):\n";
    int count_out = 0;
    for (auto& [val, cnt] : freq) {
        if (count_out++ > 20) break;  // 仅打印前20个
        std::cout << val << ": " << cnt << "\n";
    }
    std::cout << "=============================================\n\n";
}
