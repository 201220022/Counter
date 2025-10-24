#pragma once
#include <atomic>
#include <random>
#include "parlay/parallel.h"
#include "parlay/primitives.h"
#include "parlay/random.h"


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
    template <typename F>
    inline int p(F&& fn) noexcept { return fn(); }
    template <typename F>
    inline void update(F&& f) noexcept {
        approxmt_count.store(0, std::memory_order_relaxed);
        //if constexpr (std::is_invocable_v<F>) verified_value.store(f(), std::memory_order_relaxed);
        //else verified_value.store(f, std::memory_order_relaxed);
    }
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
