#pragma once
#include <atomic>
#include <bit>
#include <cstdint>
#include <thread>
/*
#ifndef WIDTH
#define WIDTH 100
#endif

template <class Graph>
struct Counter {
    using NodeId = typename Graph::NodeId;

    // 绑定对象
    const Graph* G;                                      // 图
    NodeId       u;                                      // 本计数器对应的顶点
    const parlay::sequence<std::atomic<uint64_t>>* status;   // 全局状态数组
    const parlay::sequence<double>*                 priority; // 全局优先级

    // 状态（和采样配置）
    int              verified_value;     // 上次校准得到的精确值
    std::atomic<int> approxmt_count;     // 近似计数(被并发 fetch_sub)
    std::atomic<bool> gate;              // 轻量互斥，避免重复校准
    int              s;                  // 采样步长
    double           p;                  // 采样概率（外部采样契约）
    int              threshould;         // 几何带级阈值（避免频繁校准）

    Counter()
      : G(nullptr), u(0), status(nullptr), priority(nullptr),
        verified_value(0), approxmt_count(0), gate(false),
        s(1), p(1.0), threshould(0) {}

    // 用精确初值 verified 来初始化（外部已数好，或初次计算）
    Counter(const Graph& g,
            NodeId u_,
            const parlay::sequence<std::atomic<uint64_t>>* status_,
            const parlay::sequence<double>* priority_,
            int verified)
      : G(&g), u(u_), status(status_), priority(priority_),
        verified_value(verified), approxmt_count(verified), gate(false)
    {
        s = (verified < WIDTH) ? 1 : 2 * (verified / WIDTH);
        p = 1.0 / static_cast<double>(s);
        int bands = verified / WIDTH;
        threshould = (bands > 0)
          ? static_cast<int>(std::bit_floor(static_cast<unsigned>(bands)) * WIDTH)
          : 0;
    }

    inline int  get_verified()  { return verified_value; }
    inline int  get_approxmt()  { return approxmt_count.load(std::memory_order_relaxed); }
    inline bool is_zero()       { return get_approxmt() == 0; }

    // 扣减并在跨越阈值时触发校准（校准里会“重扫邻域”做精确计数）
    inline void decrement() noexcept {
        thread_local uint64_t tl_counter = 1469598103934665603ull;
        uint64_t seed = tl_counter++ ^ reinterpret_cast<uintptr_t>(this);
        uint64_t x = parlay::hash64(seed);
        double u = static_cast<double>(x >> 11) * (1.0 / 9007199254740992.0);
        if (u >= p) return;

        if (approxmt_count.fetch_sub(s, std::memory_order_relaxed) > threshould) return;

        bool expected = false;
        if (gate.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            int cur = approxmt_count.load(std::memory_order_relaxed);
            if (cur <= threshould) {
                update(); // 真正的图上“重扫邻域”
            }
            gate.store(false, std::memory_order_release);
        } else {
            while (gate.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        }
    }
    inline void operator--(int) noexcept { decrement(); }

private:
    // 在图上“重扫邻域”，精确数
    inline void update() noexcept {
        if (G == nullptr || status == nullptr || priority == nullptr) return;
        double pu = (*priority)[u];
        int exact = 0;
        size_t beg = G->offsets[u];
        size_t end = G->offsets[u + 1];
        for (size_t e = beg; e < end; ++e) {
            NodeId v = G->edges[e].v;
            uint64_t sv = (*status)[v].load(std::memory_order_relaxed);
            if (sv == 0  && (*priority)[v] > pu) {
                ++exact;
            }
        }
        verified_value = exact;
        approxmt_count.store(verified_value, std::memory_order_relaxed);

        if (verified_value < WIDTH) {
            s = 1;
        } else {
            s = 2 * (verified_value / WIDTH);
            if (s <= 0) s = 1;
        }
        p = (s > 0) ? (1.0 / static_cast<double>(s)) : 0.0;

        int bands = verified_value / WIDTH;
        threshould = (bands > 0)
          ? static_cast<int>(std::bit_floor(static_cast<unsigned>(bands)) * WIDTH)
          : 0;

        if (verified_value == 0) {
            // 彻底关闭采样
            p = 0.0;
            s = 1;
            threshould = 0;
        }
    }
};

*/


struct Counter {
    int              verified_value;
    std::atomic<int> approxmt_count;
    Counter() : verified_value(0), approxmt_count(0) {}
    Counter(int verified) : verified_value(verified), approxmt_count(verified) {}
    inline int get_verified() { return verified_value; }
    inline int get_approxmt() { return approxmt_count.load(std::memory_order_relaxed); }
    inline void decrement() noexcept { approxmt_count.fetch_sub(1, std::memory_order_relaxed); }
    inline void operator--(int) noexcept { decrement(); }
    inline bool is_zero() noexcept { return !approxmt_count.load(std::memory_order_relaxed); }
};




/*
struct Counter {
    static constexpr int WIDTH = 8192; // 每个桶的容量上限

    int verified_value;
    int num_buckets;
    parlay::sequence<std::atomic<int>> buckets;
    parlay::sequence<std::atomic<int>> tree;
    std::atomic<int> root_val;

    static thread_local parlay::random rng;

    Counter() : verified_value(0), num_buckets(0), root_val(0) {}

    Counter(int verified) : verified_value(verified) {
        if (verified <= 0) {
            num_buckets = 1;
            buckets = parlay::sequence<std::atomic<int>>(1);
            buckets[0].store(0, std::memory_order_relaxed);
            tree = parlay::sequence<std::atomic<int>>(1);
            tree[0].store(0, std::memory_order_relaxed);
            root_val.store(0, std::memory_order_relaxed);
            return;
        }

        num_buckets = (verified + WIDTH - 1) / WIDTH;
        buckets = parlay::sequence<std::atomic<int>>(num_buckets);
        int remaining = verified;

        parlay::parallel_for(0, num_buckets, [&](size_t i) {
            int val = std::min(WIDTH, remaining - (int)i * WIDTH);
            buckets[i].store(val, std::memory_order_relaxed);
        });

        int leaf_count = 1;
        while (leaf_count < num_buckets) leaf_count <<= 1;
        tree = parlay::sequence<std::atomic<int>>(2 * leaf_count - 1);
        build_tree(0, 0, leaf_count);
        root_val.store(tree[0].load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    inline int get_verified() { return verified_value; }

    inline int get_approxmt() {
        int sum = 0;
        parlay::parallel_for(0, num_buckets, [&](size_t i) {
            sum += buckets[i].load(std::memory_order_relaxed);
        });
        return sum;
    }

    inline void decrement() noexcept {
        if (root_val.load(std::memory_order_relaxed) == 0) return;

        size_t idx = rng.rand() % num_buckets;

        for (int probe = 0; probe < num_buckets; probe++) {
            size_t i = (idx + probe) % num_buckets;
            int old = buckets[i].load(std::memory_order_relaxed);
            if (old > 0) {
                if (buckets[i].compare_exchange_strong(old, old - 1,
                                                       std::memory_order_relaxed)) {
                    if (old == 1) propagate_empty(i);
                    return;
                }
            }
        }
    }

    inline void operator--(int) noexcept { decrement(); }

    inline bool is_zero() noexcept {
        return root_val.load(std::memory_order_relaxed) == 0;
    }

    template <typename F>
    inline int p(F&& fn) noexcept { return fn(); }

private:
    int build_tree(int idx, int l, int r) {
        if (r - l == 1) {
            int active = (l < num_buckets && buckets[l].load() > 0) ? 1 : 0;
            tree[idx].store(active, std::memory_order_relaxed);
            return active;
        }
        int mid = (l + r) / 2;
        int left = build_tree(2 * idx + 1, l, mid);
        int right = build_tree(2 * idx + 2, mid, r);
        tree[idx].store(left + right, std::memory_order_relaxed);
        return left + right;
    }

    void propagate_empty(int bucket_index) noexcept {
        int leaf_count = (tree.size() + 1) / 2;
        int tree_idx = leaf_count - 1 + bucket_index;
        while (tree_idx >= 0) {
            int new_val = tree[tree_idx].fetch_sub(1, std::memory_order_relaxed) - 1;
            if (new_val > 0) break;
            if (tree_idx == 0) {
                root_val.store(0, std::memory_order_relaxed);
                break;
            }
            tree_idx = (tree_idx - 1) / 2;
        }
    }

};

thread_local parlay::random Counter::rng(1234567);

*/
