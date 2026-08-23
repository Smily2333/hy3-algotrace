cmake_minimum_required(VERSION 3.16)

project(hy3_algotrace CXX)

# Phase 0 skeleton only. No business logic is implemented yet.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Minimal placeholder target to validate the C++17 toolchain hook.
# Real modules (Ingest / ProcessEvaluator / Reporter ...) are added in later phases.
add_executable(hy3_algotrace_skeleton src/main.cpp)
