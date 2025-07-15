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