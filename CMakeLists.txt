cmake_minimum_required(VERSION 2.6)
project(InlinedHashTable)
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

include(ExternalProject)

ExternalProject_Add (
 project_benchmark
 PREFIX ${CMAKE_CURRENT_BINARY_DIR}/benchmark
 GIT_REPOSITORY "https://github.com/google/benchmark.git"
 GIT_TAG "7a74b74856bae690a0998c967c7807dd2272af82"
 UPDATE_COMMAND ""
 INSTALL_COMMAND "")

ExternalProject_Get_Property(project_benchmark install_dir)

add_library(benchmark STATIC IMPORTED)
set_property(
  TARGET benchmark
  PROPERTY IMPORTED_LOCATION
  benchmark/src/project_benchmark-build/src/libbenchmark.a)
add_dependencies(benchmark project_benchmark)

add_executable(InlinedHashTableTest inlined_hash_table_test.cc)
set_property(TARGET InlinedHashTableTest  PROPERTY CXX_STANDARD 11)
include_directories(benchmark/src/project_benchmark/include)
target_link_libraries(
  InlinedHashTableTest
  benchmark ${GTEST_LIBRARIES} pthread)
