#include "../include/Threadpool.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <thread>

using namespace std::chrono;

struct BenchmarkResult {
    double mean;
    double median;
    double stddev;
    double min;
    double max;
    double throughput;
};

class Benchmark {
private:
    static double calculate_mean(const std::vector<double>& times) {
        return std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    }
    
    static double calculate_median(std::vector<double> times) {
        std::sort(times.begin(), times.end());
        size_t n = times.size();
        return n % 2 == 0 ? (times[n/2-1] + times[n/2]) / 2.0 : times[n/2];
    }
    
    static double calculate_stddev(const std::vector<double>& times, double mean) {
        double sum = 0.0;
        for (double t : times) {
            sum += (t - mean) * (t - mean);
        }
        return std::sqrt(sum / times.size());
    }

public:
    static BenchmarkResult run_benchmark(const std::string& name, 
                                       std::function<void()> setup,
                                       std::function<void()> benchmark,
                                       std::function<void()> teardown,
                                       int iterations,
                                       int operations_per_iteration) {
        
        std::cout << "\n=== " << name << " ===\n";
        std::vector<double> times;
        times.reserve(iterations);
        
        for (int i = 0; i < iterations; ++i) {
            setup();
            
            auto start = high_resolution_clock::now();
            benchmark();
            auto end = high_resolution_clock::now();
            
            teardown();
            
            double elapsed = duration_cast<microseconds>(end - start).count() / 1000.0;
            times.push_back(elapsed);
            
            std::cout << "Iteration " << std::setw(3) << i + 1 
                     << ": " << std::fixed << std::setprecision(2) 
                     << elapsed << " ms\n";
        }
        
        BenchmarkResult result;
        result.mean = calculate_mean(times);
        result.median = calculate_median(times);
        result.stddev = calculate_stddev(times, result.mean);
        result.min = *std::min_element(times.begin(), times.end());
        result.max = *std::max_element(times.begin(), times.end());
        result.throughput = (operations_per_iteration * 1000.0) / result.mean;
        
        std::cout << "\nResults:\n";
        std::cout << "  Mean:       " << std::fixed << std::setprecision(2) << result.mean << " ms\n";
        std::cout << "  Median:     " << result.median << " ms\n";
        std::cout << "  Std Dev:    " << result.stddev << " ms\n";
        std::cout << "  Min:        " << result.min << " ms\n";
        std::cout << "  Max:        " << result.max << " ms\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0) 
                 << result.throughput << " ops/sec\n";
        
        return result;
    }
};

void benchmark_simple_tasks() {
    LockFreeThreadPool* pool = nullptr;
    std::atomic<int> counter{0};
    constexpr int task_count = 100000;
    
    Benchmark::run_benchmark(
        "Simple Task Execution",
        [&]() { 
            pool = new LockFreeThreadPool(std::thread::hardware_concurrency());
            counter = 0;
        },
        [&]() {
            std::vector<std::future<void>> futures;
            futures.reserve(task_count);
            
            for (int i = 0; i < task_count; ++i) {
                futures.push_back(pool->enqueue([&counter]() {
                    counter.fetch_add(1, std::memory_order_relaxed);
                }));
            }
            
            for (auto& f : futures) {
                f.get();
            }
        },
        [&]() { 
            delete pool;
            pool = nullptr;
        },
        10,
        task_count
    );
}

void benchmark_computational_tasks() {
    LockFreeThreadPool* pool = nullptr;
    constexpr int task_count = 10000;
    
    Benchmark::run_benchmark(
        "Computational Tasks",
        [&]() { 
            pool = new LockFreeThreadPool(std::thread::hardware_concurrency());
        },
        [&]() {
            std::vector<std::future<double>> futures;
            futures.reserve(task_count);
            
            for (int i = 0; i < task_count; ++i) {
                futures.push_back(pool->enqueue([i]() {
                    double sum = 0.0;
                    for (int j = 0; j < 1000; ++j) {
                        sum += std::sin(i * j) * std::cos(i * j);
                    }
                    return sum;
                }));
            }
            
            double total = 0.0;
            for (auto& f : futures) {
                total += f.get();
            }
        },
        [&]() { 
            delete pool;
            pool = nullptr;
        },
        10,
        task_count
    );
}

void benchmark_io_simulation() {
    LockFreeThreadPool* pool = nullptr;
    constexpr int task_count = 1000;
    
    Benchmark::run_benchmark(
        "I/O Simulation (sleep-based)",
        [&]() { 
            pool = new LockFreeThreadPool(std::thread::hardware_concurrency() * 2);
        },
        [&]() {
            std::vector<std::future<void>> futures;
            futures.reserve(task_count);
            
            for (int i = 0; i < task_count; ++i) {
                futures.push_back(pool->enqueue([]() {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }));
            }
            
            for (auto& f : futures) {
                f.get();
            }
        },
        [&]() { 
            delete pool;
            pool = nullptr;
        },
        5,
        task_count
    );
}

void benchmark_mixed_workload() {
    LockFreeThreadPool* pool = nullptr;
    constexpr int task_count = 50000;
    
    Benchmark::run_benchmark(
        "Mixed Workload",
        [&]() { 
            pool = new LockFreeThreadPool(std::thread::hardware_concurrency());
        },
        [&]() {
            std::vector<std::future<double>> futures;
            futures.reserve(task_count);
            std::mt19937 rng(42);
            std::uniform_int_distribution<int> dist(0, 2);
            
            for (int i = 0; i < task_count; ++i) {
                int task_type = dist(rng);
                
                switch (task_type) {
                    case 0:
                        futures.push_back(pool->enqueue([i]() {
                            return static_cast<double>(i * 2);
                        }));
                        break;
                    case 1:
                        futures.push_back(pool->enqueue([i]() {
                            double sum = 0.0;
                            for (int j = 0; j < 100; ++j) {
                                sum += std::sqrt(i * j);
                            }
                            return sum;
                        }));
                        break;
                    case 2:
                        futures.push_back(pool->enqueue([i]() {
                            std::this_thread::sleep_for(std::chrono::microseconds(10));
                            return static_cast<double>(i);
                        }));
                        break;
                }
            }
            
            double total = 0.0;
            for (auto& f : futures) {
                total += f.get();
            }
        },
        [&]() { 
            delete pool;
            pool = nullptr;
        },
        10,
        task_count
    );
}

void benchmark_scalability() {
    std::cout << "\n\n=== SCALABILITY TEST ===\n";
    constexpr int task_count = 100000;
    std::atomic<int> counter{0};
    
    std::vector<int> thread_counts = {1, 2, 4, 8, 16, 32};
    std::vector<double> throughputs;
    
    for (int threads : thread_counts) {
        if (threads > static_cast<int>(std::thread::hardware_concurrency() * 2)) {
            break;
        }
        
        LockFreeThreadPool pool(threads);
        counter = 0;
        
        auto start = high_resolution_clock::now();
        
        std::vector<std::future<void>> futures;
        for (int i = 0; i < task_count; ++i) {
            futures.push_back(pool.enqueue([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
        }
        
        for (auto& f : futures) {
            f.get();
        }
        
        auto end = high_resolution_clock::now();
        double elapsed = duration_cast<microseconds>(end - start).count() / 1000.0;
        double throughput = (task_count * 1000.0) / elapsed;
        
        throughputs.push_back(throughput);
        
        std::cout << "Threads: " << std::setw(3) << threads 
                 << " | Time: " << std::fixed << std::setprecision(2) << std::setw(8) << elapsed << " ms"
                 << " | Throughput: " << std::fixed << std::setprecision(0) << std::setw(10) 
                 << throughput << " ops/sec\n";
    }
    
    std::cout << "\nSpeedup factors:\n";
    for (size_t i = 1; i < throughputs.size(); ++i) {
        double speedup = throughputs[i] / throughputs[0];
        std::cout << thread_counts[i] << " threads: " 
                 << std::fixed << std::setprecision(2) << speedup << "x\n";
    }
}

int main() {
    std::cout << "=== LOCK-FREE THREADPOOL BENCHMARK ===\n";
    std::cout << "Hardware threads: " << std::thread::hardware_concurrency() << "\n";
    
    benchmark_simple_tasks();
    benchmark_computational_tasks();
    benchmark_io_simulation();
    benchmark_mixed_workload();
    benchmark_scalability();
    
    std::cout << "\n=== BENCHMARK COMPLETE ===\n";
    
    return 0;
}