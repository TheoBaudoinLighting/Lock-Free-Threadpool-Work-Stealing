/*******************************************************************************
@file    Threadpool.hpp
@author  Theo Baudoin
@brief   A high-performance, header-only, lock-free thread pool
implementation with a work-stealing queue system.

@section DISCLAIMER
This software is provided "as is" and comes with no warranty of any kind,
express or implied. In no event shall the author be held liable for any
claim, damages, or other liability arising from the use of this software.
******************************************************************************/

#pragma once

#include <vector>
#include <thread>
#include <atomic>
#include <future>
#include <functional>
#include <memory>
#include <random>
#include <array>
#include <chrono>
#include <cstddef>
#include <utility>
#include <new>

template<typename T, size_t Size>
class LockFreeRingBuffer {
private:
    struct alignas(64) Node {
        std::atomic<T*> data{nullptr};
        char padding[64 - sizeof(std::atomic<T*>)];
    };

    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
    std::array<Node, Size> buffer;

    static constexpr size_t MASK = Size - 1;
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");

public:
    bool push(T* item) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) & MASK;

        if (next_tail == head.load(std::memory_order_acquire)) {
            return false;
        }

        buffer[current_tail].data.store(item, std::memory_order_release);
        tail.store(next_tail, std::memory_order_release);
        return true;
    }

    T* pop() {
        size_t current_head = head.load(std::memory_order_relaxed);
        if (current_head == tail.load(std::memory_order_acquire)) {
            return nullptr;
        }

        T* item = buffer[current_head].data.load(std::memory_order_acquire);
        head.store((current_head + 1) & MASK, std::memory_order_release);
        return item;
    }

    T* steal() {
        size_t current_head = head.load(std::memory_order_acquire);
        size_t current_tail = tail.load(std::memory_order_acquire);

        if (current_head == current_tail) {
            return nullptr;
        }

        T* item = buffer[current_head].data.load(std::memory_order_acquire);
        size_t next_head = (current_head + 1) & MASK;

        if (head.compare_exchange_strong(current_head, next_head,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
            return item;
        }

        return nullptr;
    }

    bool empty() const {
        return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
    }
};

class LockFreeThreadPool {
private:
    struct Task {
        std::function<void()> func;
        Task* next{nullptr};
    };

    struct alignas(64) WorkerData {
        LockFreeRingBuffer<Task, 4096> local_queue;
        std::atomic<bool> sleeping{false};
        std::atomic<size_t> steal_attempts{0};
        char padding[64 - sizeof(std::atomic<bool>) - sizeof(std::atomic<size_t>)];
    };

    std::vector<std::thread> threads;
    std::vector<std::unique_ptr<WorkerData>> worker_data;
    std::atomic<bool> stop{false};
    std::atomic<size_t> active_tasks{0};
    std::atomic<size_t> global_queue_size{0};

    alignas(64) std::atomic<Task*> global_queue_head{nullptr};
    alignas(64) std::atomic<size_t> task_counter{0};

    // MODIFICATION ICI: Ajout de 'inline static'
    inline static thread_local size_t thread_id = std::numeric_limits<size_t>::max();
    inline static thread_local std::mt19937 thread_rng{std::random_device{}()};

    void worker_thread(size_t id) {
        thread_id = id;
        auto& data = *worker_data[id];

        while (!stop.load(std::memory_order_relaxed)) {
            Task* task = nullptr;

            task = data.local_queue.pop();

            if (!task) {
                task = steal_from_global();
            }

            if (!task) {
                task = steal_from_others(id);
            }

            if (task) {
                active_tasks.fetch_add(1, std::memory_order_relaxed);
                task->func();
                active_tasks.fetch_sub(1, std::memory_order_relaxed);
                delete task;
                data.steal_attempts.store(0, std::memory_order_relaxed);
            } else {
                backoff(data);
            }
        }
    }

    Task* steal_from_global() {
        Task* head = global_queue_head.load(std::memory_order_acquire);
        while (head) {
            Task* next = head->next;
            if (global_queue_head.compare_exchange_weak(head, next,
                                                        std::memory_order_release,
                                                        std::memory_order_acquire)) {
                global_queue_size.fetch_sub(1, std::memory_order_relaxed);
                return head;
            }
        }
        return nullptr;
    }

    Task* steal_from_others(size_t thief_id) {
        size_t victim_count = worker_data.size();
        std::uniform_int_distribution<size_t> dist(0, victim_count - 1);

        for (size_t attempts = 0; attempts < victim_count * 2; ++attempts) {
            size_t victim_id = dist(thread_rng);
            if (victim_id == thief_id) continue;

            Task* task = worker_data[victim_id]->local_queue.steal();
            if (task) return task;
        }

        return nullptr;
    }

    void backoff(WorkerData& data) {
        size_t attempts = data.steal_attempts.fetch_add(1, std::memory_order_relaxed);

        if (attempts < 10) {
            std::this_thread::yield();
        } else if (attempts < 20) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        } else if (attempts < 100) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        } else {
            data.sleeping.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            data.sleeping.store(false, std::memory_order_release);
        }
    }

    void wake_sleeping_thread() {
        for (auto& data : worker_data) {
            if (data->sleeping.load(std::memory_order_acquire)) {
                data->steal_attempts.store(0, std::memory_order_relaxed);
                break;
            }
        }
    }

public:
    explicit LockFreeThreadPool(size_t num_threads = std::thread::hardware_concurrency()) {
        worker_data.reserve(num_threads);
        threads.reserve(num_threads);

        for (size_t i = 0; i < num_threads; ++i) {
            worker_data.emplace_back(std::make_unique<WorkerData>());
        }

        for (size_t i = 0; i < num_threads; ++i) {
            threads.emplace_back(&LockFreeThreadPool::worker_thread, this, i);
        }
    }

    ~LockFreeThreadPool() {
        wait();

        stop.store(true, std::memory_order_release);

        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        Task* head = global_queue_head.load(std::memory_order_acquire);
        while (head) {
            Task* next = head->next;
            delete head;
            head = next;
        }
    }

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto promise = std::make_shared<std::promise<return_type>>();
        auto future = promise->get_future();

        auto task = new Task{
            [promise, func = std::bind(std::forward<F>(f), std::forward<Args>(args)...)]() mutable {
                try {
                    if constexpr (std::is_void_v<return_type>) {
                        func();
                        promise->set_value();
                    } else {
                        promise->set_value(func());
                    }
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            }
        };

        if (thread_id < worker_data.size()) {
            if (worker_data[thread_id]->local_queue.push(task)) {
                wake_sleeping_thread();
                return future;
            }
        }

        Task* old_head = global_queue_head.load(std::memory_order_acquire);
        do {
            task->next = old_head;
        } while (!global_queue_head.compare_exchange_weak(old_head, task,
                                                           std::memory_order_release,
                                                           std::memory_order_acquire));

        global_queue_size.fetch_add(1, std::memory_order_relaxed);
        wake_sleeping_thread();

        return future;
    }

    void wait() {
        while (active_tasks.load(std::memory_order_acquire) > 0 ||
               global_queue_size.load(std::memory_order_acquire) > 0) {
            std::this_thread::yield();
        }

        bool all_empty = false;
        while (!all_empty) {
            all_empty = true;
            for (const auto& data : worker_data) {
                if (!data->local_queue.empty()) {
                    all_empty = false;
                    break;
                }
            }
            if (!all_empty) {
                std::this_thread::yield();
            }
        }
    }

    size_t thread_count() const {
        return threads.size();
    }

    size_t pending_tasks() const {
        return global_queue_size.load(std::memory_order_acquire) +
               active_tasks.load(std::memory_order_acquire);
    }
};