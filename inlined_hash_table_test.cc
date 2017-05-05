
#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "benchmark/benchmark.h"
#include "inlined_hash_table.h"

using Map = InlinedHashMap<std::string, std::string, 8>;
using Set = InlinedHashSet<std::string, 8>;

template <typename Bucket, typename Key, typename Value, int NumInlinedBuckets,
          typename Options, typename GetKey, typename Hash, typename EqualTo,
          typename IndexType>

void InlinedHashTable<Bucket, Key, Value, NumInlinedBuckets, Options, GetKey,
                      Hash, EqualTo, IndexType>::CheckConsistency() {
  const Array& array = array_;
  for (IndexType bi = 0; bi < array.capacity; ++bi) {
    const Bucket& bucket = GetBucket(array, bi);
    for (int li = 0; li < MaxHopDistance(); ++li) {
      if (bucket.md.HasLeaf(li)) {
        const Bucket& leaf = GetBucket(array, (bi + li) & (array.capacity - 1));
        ASSERT_EQ(leaf.md.GetOrigin(), li);
      }
    }
    int o = bucket.md.GetOrigin();
    if (o >= 0) {
      const Bucket& origin = GetBucket(array, (bi - o) & (array.capacity - 1));
      ASSERT_TRUE(origin.md.HasLeaf(o));
    }
  }
}

TEST(InlinedHashMap, Simple) {
  Map t;
  EXPECT_EQ(8, t.capacity());
  EXPECT_TRUE(t.empty());
  EXPECT_TRUE(t.insert(std::make_pair("hello", "world")).second);
  EXPECT_FALSE(t.empty());
  EXPECT_EQ(1, t.size());
  t.CheckConsistency();
  auto it = t.begin();
  EXPECT_EQ("hello", (*it).first);
  EXPECT_EQ("world", (*it).second);
  ++it;
  EXPECT_TRUE(it == t.end());
  EXPECT_EQ("world", t["hello"]);

  t.erase("hello");
  t.CheckConsistency();
  EXPECT_TRUE(t.empty());
  EXPECT_TRUE(t.find("hello") == t.end());
}

TEST(InlinedHashMap, EmptyInlinedPart) {
  InlinedHashMap<std::string, std::string, 0> t;
  EXPECT_EQ(0, t.capacity());
  t["k"] = "v";
  auto it = t.begin();
  EXPECT_EQ("k", (*it).first);
  EXPECT_EQ("v", (*it).second);
  ++it;
  EXPECT_TRUE(it == t.end());
  t.CheckConsistency();
}

TEST(InlinedHashMap, Clear) {
  Map t;
  t["h0"] = "w0";
  t["h1"] = "w1";
  t.clear();
  EXPECT_TRUE(t.empty());
  EXPECT_EQ(0, t.size());
  EXPECT_TRUE(t.find("h0") == t.end());
  EXPECT_TRUE(t.find("h1") == t.end());
  t.CheckConsistency();
}

TEST(InlinedHashMap, Capacity0) {
  Map t(0);
  EXPECT_EQ(8, t.capacity());
  t.CheckConsistency();
}

TEST(InlinedHashMap, Capacity5) {
  Map t(5);
  EXPECT_EQ(8, t.capacity());
  t.CheckConsistency();
}

TEST(InlinedHashMap, Capacity8) {
  Map t(8);  // MaxLoadFactor will bump the capacity to 16
  EXPECT_EQ(16, t.capacity());
  t.CheckConsistency();

  {
    class StrTableOptions {
     public:
      double MaxLoadFactor() const { return 1; }
    };
    InlinedHashMap<std::string, std::string, 8, StrTableOptions> t2(8);
    EXPECT_EQ(8, t2.capacity());
    t.CheckConsistency();
  }
}

TEST(InlinedHashMap, Iterators) {
  Map t;
  t["h0"] = "w0";
  t["h1"] = "w1";
  {
    auto it = t.begin();
    EXPECT_EQ("h0", it->first);
    EXPECT_EQ("w0", it->second);
    auto it2 = it++;
    EXPECT_EQ("h0", it2->first);
    EXPECT_EQ("w0", it2->second);
    EXPECT_EQ("h1", it->first);
    EXPECT_EQ("w1", it->second);
  }
  {
    Map::const_iterator it = t.begin();
    EXPECT_EQ("h0", it->first);
    EXPECT_EQ("w0", it->second);
    auto it2 = it++;
    EXPECT_EQ("h0", it2->first);
    EXPECT_EQ("w0", it2->second);
    EXPECT_EQ("h1", it->first);
    EXPECT_EQ("w1", it->second);
  }
}

TEST(InlinedHashMap, Copy) {
  Map t;
  t["h0"] = "w0";
  Map t2 = t;
  EXPECT_FALSE(t2.empty());
  EXPECT_EQ(1, t2.size());
  EXPECT_FALSE(t.empty());
  EXPECT_EQ(1, t.size());
  EXPECT_EQ(t2["h0"], "w0");
  EXPECT_EQ(t["h0"], "w0");
  t.CheckConsistency();
  t2.CheckConsistency();
}

TEST(InlinedHashMap, Move) {
  Map t;
  t["h0"] = "w0";
  Map t2 = std::move(t);
  EXPECT_FALSE(t2.empty());
  EXPECT_EQ(1, t2.size());
  EXPECT_EQ(t2["h0"], "w0");
  EXPECT_TRUE(t.empty());
  EXPECT_TRUE(t.find("h0") == t.end());
  t.CheckConsistency();
  t2.CheckConsistency();
}

// Set the max load factor to 1.
TEST(InlinedHashMap, OverrideMaxLoadFactor_1) {
  class Options {
   public:
    int EmptyKey() const { return -1; }
    double MaxLoadFactor() const { return 1.0; }
  };

  constexpr int kCapacity = 8;
  InlinedHashSet<int, kCapacity, Options> t;
  // Empty table should have just the inlined
  // elements.
  EXPECT_EQ(t.capacity(), kCapacity);
  for (int i = 0; i < kCapacity; ++i) {
    ASSERT_TRUE(t.insert(i).second);
  }
  EXPECT_EQ(t.capacity(), kCapacity);
  t.insert(100);
  EXPECT_EQ(t.capacity(), kCapacity * 2);
}

// Set the max load factor to 0.5
TEST(InlinedHashMap, OverrideMaxLoadFactor_0_5) {
  class Options {
   public:
    int EmptyKey() const { return -1; }
    double MaxLoadFactor() const { return 0.5; }
  };

  constexpr int kCapacity = 8;
  InlinedHashSet<int, kCapacity, Options> t;
  // Empty table should have just the inlined
  // elements.
  EXPECT_EQ(t.capacity(), kCapacity);
  for (int i = 0; i <= kCapacity; ++i) {
    ASSERT_TRUE(t.insert(i).second);
    if (i <= kCapacity / 2) {
      EXPECT_EQ(t.capacity(), kCapacity) << i;
    } else {
      EXPECT_EQ(t.capacity(), kCapacity * 2) << i;
    }
  }
}

TEST(InlinedHashSet, Random) {
  InlinedHashSet<int, 8> t;
  std::unordered_set<int> model;

  std::mt19937 rand(0);
  for (int i = 0; i < 100000; ++i) {
    int op = rand() % 100;
    if (op < 50) {
      int n = rand() % 100;
      // std::cout << i << ": Insert " << n << "\n";
      ASSERT_EQ(t.insert(n).second, model.insert(n).second);
    } else if (op < 70) {
      int n = rand() % 100;
      // std::cout << i << ": Erase " << n << "\n";
      ASSERT_EQ(t.erase(n), model.erase(n));
    } else if (op < 99) {
      int n = rand() % 100;
      ASSERT_EQ(t.find(n) == t.end(), model.find(n) == model.end());
    } else {
      t.clear();
      model.clear();
    }
    ASSERT_EQ(t.size(), model.size());
    ASSERT_EQ(t.empty(), model.empty());
    std::set<int> elems_in_t(t.begin(), t.end());
    std::set<int> elems_in_model(model.begin(), model.end());
    ASSERT_EQ(elems_in_t, elems_in_model) << i;
    t.CheckConsistency();
  }
}

TEST(InlinedHashSet, Simple) {
  Set t;
  EXPECT_TRUE(t.empty());
  EXPECT_TRUE(t.insert("hello").second);
  EXPECT_FALSE(t.empty());
  EXPECT_EQ(1, t.size());
  auto it = t.begin();
  EXPECT_EQ("hello", *it);
  ++it;
  EXPECT_TRUE(it == t.end());
  t.CheckConsistency();
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

template <typename Map>
void DoInsertTest(benchmark::State& state, Map* map) {
  std::vector<unsigned> values = TestValues();
  while (state.KeepRunning()) {
    for (unsigned v : values) {
      (*map)[v] = v + 1;
    }
  }
  for (int i = 0; i < kBenchmarkIters; ++i) {
    ASSERT_EQ((*map)[values[i]], values[i] + 1);
  }
}

void BM_Insert_InlinedMap(benchmark::State& state) {
  InlinedHashMap<unsigned, unsigned, 8> map;
  DoInsertTest(state, &map);
}
BENCHMARK(BM_Insert_InlinedMap);

void BM_Insert_UnorderedMap(benchmark::State& state) {
  std::unordered_map<unsigned, unsigned> map;
  DoInsertTest(state, &map);
}
BENCHMARK(BM_Insert_UnorderedMap);

template <typename Map>
void DoLookupTest(benchmark::State& state, Map* map) {
  std::mt19937 rand(0);
  std::vector<unsigned> values = TestValues();
  for (unsigned v : values) {
    (*map)[v] = v + 1;
  }

  while (state.KeepRunning()) {
    for (unsigned v : values) {
      ASSERT_EQ((*map)[v], v + 1);
    }
  }
}

void BM_Lookup_InlinedMap(benchmark::State& state) {
  InlinedHashMap<unsigned, unsigned, 8> map;
  DoLookupTest(state, &map);
}

BENCHMARK(BM_Lookup_InlinedMap);

void BM_Lookup_UnorderedMap(benchmark::State& state) {
  std::unordered_map<unsigned, unsigned> map;
  DoLookupTest(state, &map);
}

BENCHMARK(BM_Lookup_UnorderedMap);

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::benchmark::Initialize(&argc, argv);
  ::benchmark::RunSpecifiedBenchmarks();
  return RUN_ALL_TESTS();
}
