cmake_minimum_required(VERSION 3.20)
project(HFT_Engine VERSION 1.0 LANGUAGES CXX C CUDA)

# Modern C++ Configuration
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Strict build environment for High-Frequency Trading performance
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra")

# Find DPDK using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

# Threading library (pthread)
find_package(Threads REQUIRED)

# Find CUDA
find_package(CUDA REQUIRED)

# Add our core network ingress library
add_library(network_ingress SHARED itch_parser.cpp network_ingress.cpp strategy_engine.cpp tensorrt_engine.cpp order_execution.cpp telemetry.cpp)

# Include DPDK directories
target_include_directories(network_ingress PUBLIC ${DPDK_INCLUDE_DIRS})

# Link against DPDK, pthread, and TensorRT
target_link_libraries(network_ingress PUBLIC 
    ${DPDK_LIBRARIES} 
    Threads::Threads
    nvinfer
    cudart
)

# Set DPDK specific flags and directories
target_link_directories(network_ingress PUBLIC ${DPDK_LIBRARY_DIRS})
target_compile_options(network_ingress PUBLIC ${DPDK_CFLAGS_OTHER})

# Executable entry point
add_executable(hft_main main.cpp)
target_link_libraries(hft_main PRIVATE network_ingress)

# ==========================================
# 🧪 Unit Testing Framework (Google Test)
# ==========================================
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(hft_tests tests/test_itch_parser.cpp itch_parser.cpp)
target_link_libraries(hft_tests PRIVATE gtest_main ${DPDK_LIBRARIES})
target_include_directories(hft_tests PUBLIC ${DPDK_INCLUDE_DIRS})
target_link_directories(hft_tests PUBLIC ${DPDK_LIBRARY_DIRS})
target_compile_options(hft_tests PUBLIC ${DPDK_CFLAGS_OTHER})
