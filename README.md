# Lock-Free Thread Pool

![Language](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg)
![Type](https://img.shields.io/badge/Type-Header--Only-orange.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)

A high-performance, header-only, lock-free thread pool for modern C++. It utilizes a work-stealing mechanism to ensure efficient load balancing among worker threads, minimizing contention and maximizing throughput.

## Features

-   **Header-Only**: Simple integration by just including `Threadpool.hpp`.
-   **Lock-Free Queues**: Utilizes lock-free data structures for both global and local task queues, reducing thread synchronization overhead.
-   **Work-Stealing**: Idle threads actively "steal" work from busy threads, ensuring high CPU utilization.
-   **Modern C++**: Designed with C++17 features, providing a clean and type-safe API.
-   **Future-Based API**: The `enqueue` method returns a `std::future`, allowing easy retrieval of task results and exception propagation.

## Requirements

-   A C++17 compatible compiler (e.g., GCC 7+, Clang 5+, MSVC 2017+).
-   The `<thread>` and `<future>` headers (standard library).

## Installation

As a single-header library, integration is straightforward:

1.  Copy the `include/Threadpool.hpp` file into your project's include directory.
2.  Include the header in your source files:
    ```cpp
    #include "Threadpool.hpp"
    ```

## How to Use

Instantiate the pool and submit tasks using the `enqueue` method. The destructor of the pool will automatically wait for all submitted tasks to complete.

```cpp
#include <iostream>
#include <chrono>
#include "Threadpool.hpp"

// A sample function that returns a value
int multiply(int a, int b) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return a * b;
}

// A sample function that does not return a value
void print_message(const std::string& msg) {
    std::cout << msg << std::endl;
}

int main() {
    // Create a thread pool with the optimal number of threads for the hardware.
    LockFreeThreadPool pool;

    // Enqueue tasks and get their std::future objects.
    std::future<int> future1 = pool.enqueue(multiply, 5, 10);
    std::future<void> future2 = pool.enqueue(print_message, "Hello from the thread pool!");

    // You can continue doing other work in the main thread.
    std::cout << "Tasks have been enqueued." << std::endl;

    // Wait for the results. Calling .get() on a future will block until the task is complete.
    int result = future1.get();
    future2.get();

    std::cout << "The result of the multiplication is: " << result << std::endl;

    // The LockFreeThreadPool destructor will be called here,
    // automatically waiting for any remaining tasks to finish before exiting.
    return 0;
}
```

## The Work-Stealing Mechanism

This thread pool uses a sophisticated work-stealing strategy to achieve high performance and efficient load balancing.

1.  **Multiple Queues**: Instead of a single, shared task queue (which creates high contention), each worker thread in the pool maintains its own **local task queue**.

2.  **Task Submission**:
    -   When a task is submitted from an external thread (like `main`), it is placed in a lightweight **global queue**.
    -   When a worker thread submits a new task *from within an existing task*, the new task is pushed onto its own **local queue**. This improves data locality, as related tasks tend to stay on the same core.

3.  **Task Execution Flow**: A worker thread follows this priority order to find work:
    -   **1. Local Queue**: It first tries to pop a task from its own local queue. This is the fastest and most common case, with no contention.
    -   **2. Work-Stealing**: If its local queue is empty, the thread becomes a **"thief"**. It randomly selects another thread (a "victim") and attempts to **"steal"** a task from the victim's local queue. This redistributes work from busy threads to idle threads.
    -   **3. Global Queue**: If stealing fails, the thread checks the global queue for any tasks submitted externally.

This approach minimizes lock contention and keeps all threads productive, adapting dynamically to the workload.

## Building Tests and Benchmarks

The repository includes a set of tests and benchmarks. To build them, you can use CMake:

```bash
# Clone the repository
git clone https://your-repo-url/LockFreeThreadPool.git
cd LockFreeThreadPool

# Configure the build
cmake -B build

# Build the project
cmake --build build

# Run the tests from the build directory
cd build
ctest
```

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.
