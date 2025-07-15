// main.cpp
// A demo application for the LockFreeThreadPool, a Mandelbrot fractal generator.
// This program uses the ThreadPool to parallelize the expensive fractal calculation.

#include <iostream>
#include <vector>
#include <chrono>
#include <fstream>
#include <complex>

#include "Threadpool.hpp"

constexpr int IMAGE_WIDTH = 1920;
constexpr int IMAGE_HEIGHT = 1080;

constexpr int MAX_ITERATIONS = 500;

struct Color {
    unsigned char r{0}, g{0}, b{0};
};

int calculate_mandelbrot_iterations(const std::complex<double>& c) {
    std::complex<double> z = 0;
    int iterations = 0;
    while (std::abs(z) <= 2.0 && iterations < MAX_ITERATIONS) {
        z = z * z + c;
        iterations++;
    }
    return iterations;
}

Color map_iterations_to_color(int iterations) {
    if (iterations == MAX_ITERATIONS) {
        return {0, 0, 0};
    }

    double hue = 0.7 + 10.0 * static_cast<double>(iterations) / MAX_ITERATIONS;
    double sat = 0.8;
    double val = 1.0;

    int h_i = static_cast<int>(hue * 6);
    double f = hue * 6 - h_i;
    double p = val * (1 - sat);
    double q = val * (1 - f * sat);
    double t = val * (1 - (1 - f) * sat);

    val *= 255;
    t *= 255;
    p *= 255;
    q *= 255;

    switch (h_i % 6) {
        case 0: return {static_cast<unsigned char>(val), static_cast<unsigned char>(t), static_cast<unsigned char>(p)};
        case 1: return {static_cast<unsigned char>(q), static_cast<unsigned char>(val), static_cast<unsigned char>(p)};
        case 2: return {static_cast<unsigned char>(p), static_cast<unsigned char>(val), static_cast<unsigned char>(t)};
        case 3: return {static_cast<unsigned char>(p), static_cast<unsigned char>(q), static_cast<unsigned char>(val)};
        case 4: return {static_cast<unsigned char>(t), static_cast<unsigned char>(p), static_cast<unsigned char>(val)};
        default: return {static_cast<unsigned char>(val), static_cast<unsigned char>(p), static_cast<unsigned char>(q)};
    }
}

int main() {
    std::cout << "=== Mandelbrot Fractal Generator with ThreadPool ===" << std::endl;
    std::cout << "Using " << std::thread::hardware_concurrency() << " threads to generate an image of "
              << IMAGE_WIDTH << "x" << IMAGE_HEIGHT << " pixels." << std::endl;

    LockFreeThreadPool pool;
    std::vector<Color> pixels(IMAGE_WIDTH * IMAGE_HEIGHT);

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        pool.enqueue([&pixels, y]() {
            for (int x = 0; x < IMAGE_WIDTH; ++x) {
                double real = (x - IMAGE_WIDTH / 2.0) * 4.0 / IMAGE_WIDTH;
                double imag = (y - IMAGE_HEIGHT / 2.0) * 4.0 / IMAGE_WIDTH;
                std::complex<double> c(real, imag);

                int iterations = calculate_mandelbrot_iterations(c);
                pixels[y * IMAGE_WIDTH + x] = map_iterations_to_color(iterations);
            }
        });
    }

    std::cout << "All tasks have been submitted. Waiting for calculations to finish..." << std::endl;
    pool.wait();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_ms = end_time - start_time;
    std::cout << "Calculation finished in " << elapsed_ms.count() << " ms." << std::endl;

    std::cout << "Saving image to 'mandelbrot.ppm'..." << std::endl;
    std::ofstream output_file("mandelbrot.ppm");
    if (!output_file) {
        std::cerr << "Error: could not open output file." << std::endl;
        return 1;
    }

    output_file << "P3\n" << IMAGE_WIDTH << " " << IMAGE_HEIGHT << "\n255\n";

    for (const auto& p : pixels) {
        output_file << static_cast<int>(p.r) << " "
                    << static_cast<int>(p.g) << " "
                    << static_cast<int>(p.b) << "\n";
    }

    output_file.close();
    std::cout << "Image saved successfully!" << std::endl;

    return 0;
}
