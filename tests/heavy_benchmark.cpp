#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <numeric>
#include <memory>
#include <iomanip>
#include "Threadpool.hpp"

constexpr size_t MATRIX_SIZE = 64;
struct Matrix {
    std::array<std::array<float, MATRIX_SIZE>, MATRIX_SIZE> data;
};

std::unique_ptr<Matrix> perform_matrix_multiplication(const Matrix& a, const Matrix& b) {
    auto result = std::make_unique<Matrix>();
    for (size_t i = 0; i < MATRIX_SIZE; ++i) {
        for (size_t j = 0; j < MATRIX_SIZE; ++j) {
            float sum = 0.0f;
            for (size_t k = 0; k < MATRIX_SIZE; ++k) {
                sum += a.data[i][k] * b.data[k][j];
            }
            result->data[i][j] = sum;
        }
    }
    return result;
}

void io_bound_task() {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

long long recursive_sum(LockFreeThreadPool& pool, long long start, long long end) {
    if (end - start <= 1000) {
        long long total = 0;
        for (long long i = start; i <= end; ++i) {
            total += i;
        }
        return total;
    } else {
        long long mid = start + (end - start) / 2;

        auto future1 = pool.enqueue(recursive_sum, std::ref(pool), start, mid);

        auto future2 = pool.enqueue(recursive_sum, std::ref(pool), mid + 1, end);

        return future1.get() + future2.get();
    }
}

int main() {
    const unsigned int num_threads = std::thread::hardware_concurrency();
    LockFreeThreadPool pool(num_threads);

    std::cout << "Starting Heavy Benchmark with " << num_threads << " threads." << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    {
        std::cout << "\n--- SCENARIO 1: Sustained CPU-Bound Load ---" << std::endl;
        const int num_tasks = 500;
        Matrix matrix_a{}, matrix_b{};

        auto start_time = std::chrono::high_resolution_clock::now();

        std::vector<std::future<std::unique_ptr<Matrix>>> futures;
        futures.reserve(num_tasks);
        for (int i = 0; i < num_tasks; ++i) {
            futures.emplace_back(pool.enqueue(perform_matrix_multiplication, std::cref(matrix_a), std::cref(matrix_b)));
        }
        for (auto& f : futures) {
            f.get();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_ms = end_time - start_time;
        std::cout << "Completed " << num_tasks << " matrix multiplications in " << elapsed_ms.count() << " ms." << std::endl;
        std::cout << "Throughput: " << num_tasks / (elapsed_ms.count() / 1000.0) << " tasks/sec." << std::endl;
    }

    {
        std::cout << "\n--- SCENARIO 2: Mixed CPU & I/O Workload ---" << std::endl;
        const int num_tasks = 1000;
        Matrix matrix_a{}, matrix_b{};
        std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, 1);

        auto start_time = std::chrono::high_resolution_clock::now();

        std::vector<std::future<void>> futures;
        futures.reserve(num_tasks);
        for (int i = 0; i < num_tasks; ++i) {
            if (dist(rng) == 0) {
                futures.emplace_back(pool.enqueue([](const Matrix& a, const Matrix& b){ perform_matrix_multiplication(a, b); }, std::cref(matrix_a), std::cref(matrix_b)));
            } else {
                futures.emplace_back(pool.enqueue(io_bound_task));
            }
        }
        for (auto& f : futures) {
            f.get();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_ms = end_time - start_time;
        std::cout << "Completed " << num_tasks << " mixed tasks in " << elapsed_ms.count() << " ms." << std::endl;
    }

    {
        std::cout << "\n--- SCENARIO 3: Recursive Task Decomposition ---" << std::endl;
        const long long total_sum_up_to = 10'000'000;
        const long long base_case_threshold = 10000;

        auto start_time = std::chrono::high_resolution_clock::now();

        std::vector<std::future<long long>> futures;

        std::function<void(long long, long long)> decompose_task =
            [&](long long start, long long end) {

            if (end - start <= base_case_threshold) {
                futures.push_back(pool.enqueue([start, end]() {
                    long long total = 0;
                    for (long long i = start; i <= end; ++i) {
                        total += i;
                    }
                    return total;
                }));
            } else {
                long long mid = start + (end - start) / 2;
                decompose_task(start, mid);
                decompose_task(mid + 1, end);
            }
        };

        decompose_task(1, total_sum_up_to);

        long long final_result = 0;
        for (auto& f : futures) {
            final_result += f.get();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_ms = end_time - start_time;

        long long expected_result = (total_sum_up_to * (total_sum_up_to + 1)) / 2;
        std::cout << "Recursive sum completed in " << elapsed_ms.count() << " ms." << std::endl;
        std::cout << "Result: " << final_result << (final_result == expected_result ? " (Correct)" : " (Incorrect)") << std::endl;
    }

    return 0;
}