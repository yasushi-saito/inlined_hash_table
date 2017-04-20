
#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>

#include "inlined_hash_table.h"

using Map = InlinedHashMap<std::string, std::string, 8>;
using Set = InlinedHashSet<std::string, 8>;

TEST(InlinedHashMap, Simple) {
  Map t;
  t.set_empty_key("");
  t.set_deleted_key("xxx");
  EXPECT_TRUE(t.empty());
  EXPECT_TRUE(t.insert(std::make_pair("hello", "world")).second);
  EXPECT_FALSE(t.empty());
  EXPECT_EQ(1, t.size());
  auto it = t.begin();
  EXPECT_EQ("hello", (*it).first);
  EXPECT_EQ("world", (*it).second);
  ++it;
  EXPECT_TRUE(it == t.end());
  EXPECT_EQ("world", t["hello"]);

  t.erase("hello");
  EXPECT_TRUE(t.empty());
  EXPECT_TRUE(t.find("hello") == t.end());
}

TEST(InlinedHashSet, Simple) {
  Set t;
  t.set_empty_key("");
  EXPECT_TRUE(t.empty());
  EXPECT_TRUE(t.insert("hello").second);
  EXPECT_FALSE(t.empty());
  EXPECT_EQ(1, t.size());
  auto it = t.begin();
  EXPECT_EQ("hello", *it);
  ++it;
  EXPECT_TRUE(it == t.end());
}

const int kBenchmarkIters = 10000;

std::vector<unsigned> TestValues() {
  std::mt19937 rand(0);
  std::vector<unsigned> values;
  for (int i = 0; i < kBenchmarkIters; ++i) {
    values.push_back(rand());
  }
  return values;
}

TEST(Benchmark, UnorderedMapInsert) {
  std::vector<unsigned> values = TestValues();
  auto start = std::chrono::system_clock::now();
  std::unordered_map<unsigned, unsigned> map;
  for (unsigned v : values) {
    map[v] = v + 1;
  }
  auto end = std::chrono::system_clock::now();
  for (int i = 0; i < kBenchmarkIters; ++i) {
    ASSERT_EQ(map[values[i]], values[i] + 1);
  }
  std::chrono::duration<double> elapsed = end - start;
  std::cout << "Insert elapsed: " << elapsed.count() << "\n";

  std::mt19937 rand(0);
  //std::shuffle(values.begin(), values.end(), rand);
  start = std::chrono::system_clock::now();
  for (unsigned v : values) {
    ASSERT_EQ(map[v], v + 1);
  }
  end = std::chrono::system_clock::now();
  elapsed = end - start;
  std::cout << "Lookup elapsed: " << elapsed.count() << "\n";
}

TEST(Benchmark, InlinedMapInsert) {
  std::vector<unsigned> values = TestValues();
  auto start = std::chrono::system_clock::now();
  InlinedHashMap<unsigned, unsigned, 8> map;
  map.set_empty_key(-1);
  for (unsigned v : values) {
    map[v] = v + 1;
  }
  auto end = std::chrono::system_clock::now();
  for (int i = 0; i < kBenchmarkIters; ++i) {
    ASSERT_EQ(map[values[i]], values[i] + 1);
  }
  std::chrono::duration<double> elapsed = end - start;
  std::cout << "Elapsed: " << elapsed.count() << "\n";

  std::mt19937 rand(0);
  //std::shuffle(values.begin(), values.end(), rand);
  start = std::chrono::system_clock::now();
  for (unsigned v : values) {
    ASSERT_EQ(map[v], v + 1);
  }
  end = std::chrono::system_clock::now();
  elapsed = end - start;
  std::cout << "Lookup elapsed: " << elapsed.count() << "\n";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
