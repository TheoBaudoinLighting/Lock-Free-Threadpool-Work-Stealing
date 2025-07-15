#include <gtest/gtest.h>
#include "../include/Threadpool.hpp"
#include <atomic>
#include <chrono>
#include <random>
#include <set>
#include <map>
#include <algorithm>
#include <numeric>
#include <memory>

using namespace std::chrono_literals;

class ThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        executed_count.store(0);
    }

    std::atomic<int> executed_count{0};
};

TEST_F(ThreadPoolTest, BasicExecution) {
    LockFreeThreadPool pool(4);
    
    auto future = pool.enqueue([this]() {
        executed_count.fetch_add(1);
        return 42;
    });
    
    EXPECT_EQ(future.get(), 42);
    EXPECT_EQ(executed_count.load(), 1);
}

TEST_F(ThreadPoolTest, MultipleTasksExecution) {
    LockFreeThreadPool pool(4);
    constexpr int task_count = 1000;
    
    std::vector<std::future<int>> futures;
    futures.reserve(task_count);
    
    for (int i = 0; i < task_count; ++i) {
        futures.push_back(pool.enqueue([this, i]() {
            executed_count.fetch_add(1);
            return i;
        }));
    }
    
    for (int i = 0; i < task_count; ++i) {
        EXPECT_EQ(futures[i].get(), i);
    }
    
    EXPECT_EQ(executed_count.load(), task_count);
}

TEST_F(ThreadPoolTest, VoidTaskExecution) {
    LockFreeThreadPool pool(2);
    
    auto future = pool.enqueue([this]() {
        executed_count.fetch_add(1);
    });
    
    future.get();
    EXPECT_EQ(executed_count.load(), 1);
}

TEST_F(ThreadPoolTest, ConcurrentEnqueue) {
    LockFreeThreadPool pool(8);
    constexpr int threads_count = 16;
    constexpr int tasks_per_thread = 1000;
    
    std::vector<std::thread> enqueuers;
    std::atomic<int> total_enqueued{0};
    
    for (int t = 0; t < threads_count; ++t) {
        enqueuers.emplace_back([&, t]() {
            for (int i = 0; i < tasks_per_thread; ++i) {
                pool.enqueue([&, t, i]() {
                    executed_count.fetch_add(1);
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                });
                total_enqueued.fetch_add(1);
            }
        });
    }
    
    for (auto& t : enqueuers) {
        t.join();
    }
    
    pool.wait();
    
    EXPECT_EQ(executed_count.load(), threads_count * tasks_per_thread);
    EXPECT_EQ(total_enqueued.load(), threads_count * tasks_per_thread);
}

TEST_F(ThreadPoolTest, WorkStealingBalance) {
    LockFreeThreadPool pool(4);
    constexpr int task_count = 10000;
    
    std::atomic<int> thread_executions[32] = {};
    
    for (int i = 0; i < task_count; ++i) {
        pool.enqueue([&thread_executions]() {
            std::hash<std::thread::id> hasher;
            size_t thread_hash = hasher(std::this_thread::get_id());
            thread_executions[thread_hash % 32].fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        });
    }
    
    pool.wait();
    
    int min_exec = INT_MAX;
    int max_exec = 0;
    int total = 0;
    
    for (int i = 0; i < 32; ++i) {
        int count = thread_executions[i].load();
        if (count > 0) {
            min_exec = std::min(min_exec, count);
            max_exec = std::max(max_exec, count);
            total += count;
        }
    }
    
    EXPECT_EQ(total, task_count);
    
    double balance_ratio = static_cast<double>(max_exec) / min_exec;
    EXPECT_LT(balance_ratio, 3.0);
}

TEST_F(ThreadPoolTest, StressTest) {
    LockFreeThreadPool pool(std::thread::hardware_concurrency());
    constexpr int task_count = 100000;
    
    std::atomic<int> sum{0};
    std::vector<std::future<void>> futures;
    futures.reserve(task_count);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < task_count; ++i) {
        futures.push_back(pool.enqueue([&sum, i]() {
            sum.fetch_add(i % 100);
        }));
    }
    
    for (auto& f : futures) {
        f.get();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    int expected_sum = 0;
    for (int i = 0; i < task_count; ++i) {
        expected_sum += i % 100;
    }
    
    EXPECT_EQ(sum.load(), expected_sum);
    std::cout << "Stress test completed in " << duration.count() << "ms\n";
}

TEST_F(ThreadPoolTest, ExceptionHandling) {
    LockFreeThreadPool pool(2);
    
    auto future1 = pool.enqueue([]() {
        throw std::runtime_error("Test exception");
        return 42;
    });
    
    auto future2 = pool.enqueue([]() {
        return 84;
    });
    
    EXPECT_THROW(future1.get(), std::runtime_error);
    EXPECT_EQ(future2.get(), 84);
}

TEST_F(ThreadPoolTest, WaitFunctionality) {
    LockFreeThreadPool pool(4);
    constexpr int task_count = 100;
    
    for (int i = 0; i < task_count; ++i) {
        pool.enqueue([this]() {
            std::this_thread::sleep_for(10ms);
            executed_count.fetch_add(1);
        });
    }
    
    pool.wait();
    
    EXPECT_EQ(executed_count.load(), task_count);
    EXPECT_EQ(pool.pending_tasks(), 0);
}

TEST_F(ThreadPoolTest, MixedTaskTypes) {
    LockFreeThreadPool pool(4);
    
    auto int_future = pool.enqueue([]() { return 42; });
    auto string_future = pool.enqueue([]() { return std::string("Hello"); });
    auto void_future = pool.enqueue([]() { });
    auto double_future = pool.enqueue([]() { return 3.14; });
    
    EXPECT_EQ(int_future.get(), 42);
    EXPECT_EQ(string_future.get(), "Hello");
    void_future.get();
    EXPECT_DOUBLE_EQ(double_future.get(), 3.14);
}

TEST_F(ThreadPoolTest, RecursiveTaskSubmission) {
    LockFreeThreadPool pool(4);

    auto recursive_count_ptr = std::make_shared<std::atomic<int>>(0);

    std::function<void(int)> recursive_task;
    recursive_task = [=, &pool, &recursive_task](int depth) mutable {
        if (depth > 0) {
            recursive_count_ptr->fetch_add(1);

            pool.enqueue(recursive_task, depth - 1);
            pool.enqueue(recursive_task, depth - 1);
        }
    };

    pool.enqueue(recursive_task, 5);
    pool.wait();

    // 4. Vérifiez la valeur via le pointeur.
    EXPECT_EQ(recursive_count_ptr->load(), 31);
}

TEST_F(ThreadPoolTest, ThreadCountVerification) {
    for (size_t count : {1, 2, 4, 8, 16}) {
        LockFreeThreadPool pool(count);
        EXPECT_EQ(pool.thread_count(), count);
    }
}

TEST_F(ThreadPoolTest, LongRunningTasks) {
    LockFreeThreadPool pool(4);
    constexpr int task_count = 20;
    
    std::atomic<int> concurrent_tasks{0};
    std::atomic<int> max_concurrent{0};
    
    std::vector<std::future<void>> futures;
    
    for (int i = 0; i < task_count; ++i) {
        futures.push_back(pool.enqueue([&]() {
            int current = concurrent_tasks.fetch_add(1) + 1;
            int expected = max_concurrent.load();
            while (current > expected && 
                   !max_concurrent.compare_exchange_weak(expected, current)) {}
            
            std::this_thread::sleep_for(50ms);
            
            concurrent_tasks.fetch_sub(1);
            executed_count.fetch_add(1);
        }));
    }
    
    for (auto& f : futures) {
        f.get();
    }
    
    EXPECT_EQ(executed_count.load(), task_count);
    EXPECT_LE(max_concurrent.load(), 4);
    EXPECT_GE(max_concurrent.load(), 2);
}

TEST_F(ThreadPoolTest, MemoryOrderingTest) {
    LockFreeThreadPool pool(8);
    constexpr int iterations = 10000;
    
    std::atomic<int> x{0}, y{0};
    std::atomic<int> r1{0}, r2{0};
    std::atomic<int> violations{0};
    
    for (int i = 0; i < iterations; ++i) {
        x.store(0);
        y.store(0);
        
        auto f1 = pool.enqueue([&]() {
            x.store(1, std::memory_order_release);
            r1.store(y.load(std::memory_order_acquire));
        });
        
        auto f2 = pool.enqueue([&]() {
            y.store(1, std::memory_order_release);
            r2.store(x.load(std::memory_order_acquire));
        });
        
        f1.get();
        f2.get();
        
        if (r1.load() == 0 && r2.load() == 0) {
            violations.fetch_add(1);
        }
    }
    
    EXPECT_EQ(violations.load(), 0);
}

TEST_F(ThreadPoolTest, DestructorWaitsForTasks) {
    std::atomic<bool> task_completed{false};
    
    {
        LockFreeThreadPool pool(2);
        pool.enqueue([&task_completed]() {
            std::this_thread::sleep_for(100ms);
            task_completed.store(true);
        });
    }
    
    EXPECT_TRUE(task_completed.load());
}

TEST_F(ThreadPoolTest, PerformanceComparison) {
    constexpr int task_count = 100000;
    const int thread_count = std::thread::hardware_concurrency();

    auto measure_time = [](auto&& func) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    };

    std::atomic<int> counter{0};

    auto lock_free_time = measure_time([&]() {
        LockFreeThreadPool pool(thread_count);
        std::vector<std::future<void>> futures;

        for (int i = 0; i < task_count; ++i) {
            futures.push_back(pool.enqueue([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
        }

        for (auto& f : futures) {
            f.get();
        }
    });

    EXPECT_EQ(counter.load(), task_count);

    std::cout << "Lock-free performance: " << lock_free_time << " microseconds\n";
    std::cout << "Tasks per second: " << (task_count * 1000000.0) / lock_free_time << "\n";
}

TEST_F(ThreadPoolTest, BurstLoadTest) {
    LockFreeThreadPool pool(4);
    constexpr int burst_size = 10000;
    constexpr int burst_count = 10;
    
    for (int burst = 0; burst < burst_count; ++burst) {
        std::vector<std::future<void>> futures;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < burst_size; ++i) {
            futures.push_back(pool.enqueue([this]() {
                executed_count.fetch_add(1);
            }));
        }
        
        for (auto& f : futures) {
            f.get();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "Burst " << burst + 1 << " completed in " << duration.count() << "ms\n";
        
        std::this_thread::sleep_for(100ms);
    }
    
    EXPECT_EQ(executed_count.load(), burst_size * burst_count);
}

TEST(TargetedThreadPoolTest, RingBufferContentionTest) {
    constexpr int NUM_PRODUCER_THREADS = 4;
    constexpr int TASKS_PER_PRODUCER = 25000;
    constexpr int TOTAL_TASKS = NUM_PRODUCER_THREADS * TASKS_PER_PRODUCER;
    const unsigned int hardware_threads = std::max(4u, std::thread::hardware_concurrency());

    std::cout << "\nStarting of RingBufferContentionTest..." << std::endl;
    std::cout << "Pool with " << hardware_threads << " threads." << std::endl;
    std::cout << NUM_PRODUCER_THREADS << " threads producers, "
              << TASKS_PER_PRODUCER << " each tasks. Total of : " << TOTAL_TASKS << " tasks." << std::endl;

    LockFreeThreadPool pool(hardware_threads);
    std::atomic<int> task_counter{0};
    std::vector<std::thread> producers;

    for (int i = 0; i < NUM_PRODUCER_THREADS; ++i) {
        producers.emplace_back([&pool, &task_counter]() {
            for (int j = 0; j < TASKS_PER_PRODUCER; ++j) {
                pool.enqueue([&task_counter]() {
                    task_counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }
    std::cout << "All producers have finished. Waiting for the pool to complete..." << std::endl;

    pool.wait();
    std::cout << "Pool finished. Verifying the result..." << std::endl;
    std::cout << "Final count of executed tasks: " << task_counter.load() << std::endl;

    EXPECT_EQ(task_counter.load(), TOTAL_TASKS);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}