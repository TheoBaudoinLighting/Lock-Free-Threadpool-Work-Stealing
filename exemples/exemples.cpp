// ==================================================================================
// File: examples.cpp
// Description: A collection of examples demonstrating the usage of the ThreadPool
//              for different kinds of workloads.
// ==================================================================================

#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <algorithm>
#include <atomic>
#include <string>
#include <regex>
#include <complex> // Used in a previous version, kept for potential future use

#include "../include/Threadpool.hpp" // Adjust path if necessary

// ==================================================================================
// EXAMPLE 1: SIMPLE RAY TRACER
// Use Case: CPU-bound workload, perfect for observing speed-up from parallelization.
// ==================================================================================

namespace RayTracer {
    struct Vec3 {
        double x = 0, y = 0, z = 0;
        Vec3 operator+(const Vec3& v) const { return {x + v.x, y + v.y, z + v.z}; }
        Vec3 operator-(const Vec3& v) const { return {x - v.x, y - v.y, z - v.z}; }
        Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
        double dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
        Vec3 normalize() const { double l = std::sqrt(dot(*this)); return {x/l, y/l, z/l}; }
    };

    struct Ray { Vec3 origin, direction; };
    struct Sphere { Vec3 center; double radius; Vec3 color; };
    struct Light { Vec3 position; double intensity; };
    struct Color { unsigned char r = 0, g = 0, b = 0; };

    bool intersect(const Ray& r, const Sphere& s, double& t) {
        Vec3 oc = r.origin - s.center;
        double a = r.direction.dot(r.direction);
        double b = 2.0 * oc.dot(r.direction);
        double c = oc.dot(oc) - s.radius * s.radius;
        double discriminant = b * b - 4 * a * c;
        if (discriminant < 0) return false;
        t = (-b - std::sqrt(discriminant)) / (2.0 * a);
        return t > 0;
    }

    Color cast_ray(const Ray& r, const std::vector<Sphere>& spheres, const Light& light) {
        double closest_t = std::numeric_limits<double>::max();
        const Sphere* hit_sphere = nullptr;

        for (const auto& sphere : spheres) {
            double t = 0;
            if (intersect(r, sphere, t) && t < closest_t) {
                closest_t = t;
                hit_sphere = &sphere;
            }
        }

        if (!hit_sphere) return {25, 25, 40};

        Vec3 hit_point = r.origin + r.direction * closest_t;
        Vec3 normal = (hit_point - hit_sphere->center).normalize();
        Vec3 light_dir = (light.position - hit_point).normalize();
        
        double diff = std::max(0.0, normal.dot(light_dir));
        double intensity = diff * light.intensity;

        return {
            (unsigned char)(std::min(255.0, hit_sphere->color.x * intensity)),
            (unsigned char)(std::min(255.0, hit_sphere->color.y * intensity)),
            (unsigned char)(std::min(255.0, hit_sphere->color.z * intensity))
        };
    }

    void save_image(const std::string& filename, const std::vector<Color>& pixels, int width, int height) {
        std::ofstream ofs(filename);
        ofs << "P3\n" << width << " " << height << "\n255\n";
        for (const auto& p : pixels) {
            ofs << (int)p.r << " " << (int)p.g << " " << (int)p.b << "\n";
        }
    }

    void run() {
        std::cout << "\n--- 1. Simple Ray Tracer ---" << std::endl;
        const int WIDTH = 1280, HEIGHT = 720;
        
        std::vector<Sphere> spheres = {
            {{-3, 0, -16}, 2, {255, 128, 128}},
            {{2, 1, -14}, 3, {128, 255, 128}},
            {{0, -502, -20}, 500, {128, 128, 255}} // "Floor"
        };
        Light light = {{20, 20, 0}, 1.5};
        std::vector<Color> pixels(WIDTH * HEIGHT);
        LockFreeThreadPool pool;

        auto start = std::chrono::high_resolution_clock::now();

        for (int y = 0; y < HEIGHT; ++y) {
            pool.enqueue([&, y]() {
                for (int x = 0; x < WIDTH; ++x) {
                    double fov = 1.0;
                    double dir_x = (x + 0.5) - WIDTH / 2.0;
                    double dir_y = -(y + 0.5) + HEIGHT / 2.0;
                    double dir_z = -HEIGHT / fov;
                    Ray r = {{0, 0, 0}, Vec3{dir_x, dir_y, dir_z}.normalize()};
                    pixels[y * WIDTH + x] = cast_ray(r, spheres, light);
                }
            });
        }
        pool.wait();

        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "Render finished in " << std::chrono::duration<double, std::milli>(end - start).count() << " ms." << std::endl;
        
        save_image("ray_tracer_output.ppm", pixels, WIDTH, HEIGHT);
        std::cout << "Image saved to 'ray_tracer_output.ppm'." << std::endl;
    }
} // namespace RayTracer


// ==================================================================================
// EXAMPLE 2: MASSIVE PARALLEL SORT
// Use Case: Tests latency, handling of variable-length tasks, and result synchronization.
// ==================================================================================
namespace ParallelSort {
    void merge(std::vector<int>& arr, size_t l, size_t m, size_t r) {
        std::vector<int> tmp(r - l + 1);
        size_t i = l, j = m + 1, k = 0;
        while (i <= m && j <= r) {
            tmp[k++] = (arr[i] <= arr[j]) ? arr[i++] : arr[j++];
        }
        while (i <= m) tmp[k++] = arr[i++];
        while (j <= r) tmp[k++] = arr[j++];
        for (i = 0; i < tmp.size(); ++i) arr[l + i] = tmp[i];
    }

    void run() {
        std::cout << "\n--- 2. Massive Parallel Sort ---" << std::endl;
        const size_t ARRAY_SIZE = 10'000'000;
        const size_t CHUNK_SIZE = 1'000'000;
        
        std::vector<int> data(ARRAY_SIZE);
        std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, 10000);
        std::generate(data.begin(), data.end(), [&]() { return dist(rng); });

        LockFreeThreadPool pool;
        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::future<void>> sort_futures;
        for (size_t i = 0; i < ARRAY_SIZE; i += CHUNK_SIZE) {
            sort_futures.push_back(pool.enqueue([&data, i, CHUNK_SIZE]() {
                size_t end = std::min(i + CHUNK_SIZE, data.size());
                std::sort(data.begin() + i, data.begin() + end);
            }));
        }
        for(auto& f : sort_futures) f.get();

        for (size_t size = CHUNK_SIZE; size < ARRAY_SIZE; size *= 2) {
            for (size_t left = 0; left < ARRAY_SIZE - size; left += 2 * size) {
                size_t mid = left + size - 1;
                size_t right = std::min(left + 2 * size - 1, ARRAY_SIZE - 1);
                merge(data, left, mid, right);
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "Sorting " << ARRAY_SIZE << " integers finished in " << std::chrono::duration<double, std::milli>(end - start).count() << " ms." << std::endl;

        bool sorted = std::is_sorted(data.begin(), data.end());
        std::cout << "The array is " << (sorted ? "correctly sorted." : "incorrectly sorted!") << std::endl;
    }
} // namespace ParallelSort


// ==================================================================================
// EXAMPLE 3: MONTE CARLO PI SOLVER
// Use Case: Embarrassingly parallel tasks, very short and independent. Ideal for measuring max throughput.
// ==================================================================================
namespace MonteCarloPi {
    long long calculate_hits_in_circle(long long num_points) {
        thread_local std::mt19937 generator(std::random_device{}());
        std::uniform_real_distribution<double> distribution(0.0, 1.0);
        long long hits = 0;
        for (long long i = 0; i < num_points; ++i) {
            double x = distribution(generator);
            double y = distribution(generator);
            if (x * x + y * y <= 1.0) {
                hits++;
            }
        }
        return hits;
    }

    void run() {
        std::cout << "\n--- 3. Monte Carlo Pi Solver ---" << std::endl;
        const long long TOTAL_POINTS = 100'000'000;
        const int NUM_TASKS = 100;
        const long long POINTS_PER_TASK = TOTAL_POINTS / NUM_TASKS;

        LockFreeThreadPool pool;
        std::atomic<long long> total_hits{0};
        std::vector<std::future<void>> futures;

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < NUM_TASKS; ++i) {
            futures.push_back(pool.enqueue([&total_hits, POINTS_PER_TASK]() {
                long long hits = calculate_hits_in_circle(POINTS_PER_TASK);
                total_hits.fetch_add(hits, std::memory_order_relaxed);
            }));
        }
        for(auto& f : futures) f.get();

        double pi_estimate = 4.0 * total_hits.load() / TOTAL_POINTS;

        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "Pi calculation finished in " << std::chrono::duration<double, std::milli>(end - start).count() << " ms." << std::endl;
        std::cout << "Pi estimate: " << pi_estimate << std::endl;
    }
} // namespace MonteCarloPi


// ==================================================================================
// EXAMPLE 4: PARALLEL REGEX GREP
// Use Case: Mix of I/O operations (file reading) and CPU (regex matching).
// ==================================================================================
namespace RegexGrep {
    void create_dummy_file(const std::string& filename, int num_lines) {
        std::ifstream f(filename);
        if (f.good()) return;
        
        std::cout << "Creating test file '" << filename << "'..." << std::endl;
        std::ofstream ofs(filename);
        for (int i = 0; i < num_lines; ++i) {
            ofs << "Line " << i << ": The quick brown fox jumps over the lazy dog. ID=" << std::hex << i << "\n";
            if (i % 1000 == 0) {
                ofs << "Line " << i << " contains a special keyword: 'important_data_packet'.\n";
            }
        }
    }

    void run() {
        std::cout << "\n--- 4. Parallel Regex Grep ---" << std::endl;
        const std::string FILENAME = "large_corpus.txt";
        const int NUM_LINES = 5'000'000;
        create_dummy_file(FILENAME, NUM_LINES);

        std::cout << "Reading file into memory..." << std::endl;
        std::ifstream file(FILENAME);
        std::vector<std::string> lines;
        lines.reserve(NUM_LINES);
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }

        const std::regex search_regex("important_data_packet");
        const size_t CHUNK_SIZE = 100000;
        LockFreeThreadPool pool;
        std::atomic<int> match_count{0};
        std::vector<std::future<void>> futures;

        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < lines.size(); i += CHUNK_SIZE) {
            futures.push_back(pool.enqueue([&, i, CHUNK_SIZE, &search_regex]() {
                int local_matches = 0;
                size_t end = std::min(i + CHUNK_SIZE, lines.size());
                for (size_t j = i; j < end; ++j) {
                    if (std::regex_search(lines[j], search_regex)) {
                        local_matches++;
                    }
                }
                if (local_matches > 0) {
                    match_count.fetch_add(local_matches, std::memory_order_relaxed);
                }
            }));
        }
        for(auto& f : futures) f.get();

        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "Search finished in " << std::chrono::duration<double, std::milli>(end - start).count() << " ms." << std::endl;
        std::cout << "Number of matches found: " << match_count.load() << std::endl;
    }
} // namespace RegexGrep

int main() {
    RayTracer::run();
    ParallelSort::run();
    MonteCarloPi::run();
    RegexGrep::run();
    return 0;
}
