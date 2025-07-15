cmake_minimum_required(VERSION 3.14)
project(LockFreeThreadPool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()

add_library(threadpool INTERFACE)
target_include_directories(threadpool INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

add_executable(main_app main.cpp)

target_link_libraries(main_app
        PRIVATE
        threadpool
        pthread
)

add_executable(threadpool_tests tests/threadpool_tests.cpp)
target_link_libraries(threadpool_tests
        PRIVATE
        threadpool
        GTest::gtest_main
        GTest::gtest
        pthread
)
add_test(NAME ThreadPoolTests COMMAND threadpool_tests)

add_executable(threadpool_benchmark tests/benchmark.cpp)
target_link_libraries(threadpool_benchmark
        PRIVATE
        threadpool
        pthread
)

add_executable(heavy_benchmark tests/heavy_benchmark.cpp
        tests/heavy_benchmark.cpp
)
target_link_libraries(heavy_benchmark
        PRIVATE
        threadpool
        pthread
)

add_executable(threadpool_examples
        exemples/exemples.cpp)
target_link_libraries(threadpool_examples
        PRIVATE
        threadpool
        pthread
)

install(DIRECTORY include/ DESTINATION include)
install(TARGETS threadpool EXPORT ThreadPoolTargets)
install(EXPORT ThreadPoolTargets
        FILE ThreadPoolTargets.cmake
        NAMESPACE ThreadPool::
        DESTINATION lib/cmake/ThreadPool
)