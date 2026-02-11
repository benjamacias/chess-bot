cmake_minimum_required(VERSION 3.16)
project(bm_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(bm_engine
  main.cpp
  opening_book.cpp
)

# Optimización (si querés debug, cambiá a -O0 -g)
target_compile_options(bm_engine PRIVATE -O3 -march=native)
