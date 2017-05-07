
#include <gtest/gtest.h>

#define NDEBUG 1
#include <chrono>
#include <limits>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <google/dense_hash_map>

#include "benchmark/benchmark.h"
#include "inlined_hash_table.h"

using Map = InlinedHashMap<std::string, std::string, 8>;
using Set = InlinedHashSet<std::string, 8>;

template <typename Key, typename Value, int NumInlinedBuckets, typename Options,
          typename GetKey, typename Hash, typename EqualTo, typename IndexType>
void InlinedHashTable<Key, Value, NumInlinedBuckets, Options, GetKey, Hash,
                      EqualTo, IndexType>::CheckConsistency() {
  const Array& array = array_;
  for (IndexType bi = 0; bi < array.capacity; ++bi) {
    const Bucket& bucket = GetBucket(array, bi);

    BucketMetadata::LeafIterator it(&bucket.md);
    int distance;
    while ((distance = it.Next()) >= 0) {
      const Bucket& leaf =
          GetBucket(array, (bi + distance) & (array.capacity - 1));
      ASSERT_EQ(leaf.md.GetOrigin(), distance);
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

TEST(InlinedHashSet, ManyInserts) {
  InlinedHashMap<unsigned, unsigned, 8> t;
  {
    std::mt19937 rand(0);
    for (int i = 0; i < 10000; ++i) {
      unsigned r = rand();
      if (i == 368 || r == 3598945970) {
        std::cout << i << ": insert " << r << "\n";
      }
      t[r] = r + 1;
      t.CheckConsistency();
    }
  }
  {
    std::mt19937 rand(0);
    for (int i = 0; i < 10000; ++i) {
      unsigned r = rand();
      if (r == 3598945970) {
        std::cout << i << ": lookup " << r << "\n";
      }
      ASSERT_EQ(r + 1, t[r]) << i;
    }
  }
}

TEST(LeafIterator, Basic) {
  InlinedHashTableBucketMetadata md;
  md.SetLeaf(0);
  md.SetLeaf(1);
  md.SetLeaf(5);
  md.SetLeaf(8);
  md.SetLeaf(9);
  md.SetLeaf(21);

  InlinedHashTableBucketMetadata::LeafIterator it(&md);
  ASSERT_EQ(it.Next(), 0);
  ASSERT_EQ(it.Next(), 1);
  ASSERT_EQ(it.Next(), 5);
  ASSERT_EQ(it.Next(), 8);
  ASSERT_EQ(it.Next(), 9);
  ASSERT_EQ(it.Next(), 21);
  ASSERT_EQ(it.Next(), -1);
}

const int kBenchmarkIters = 10000;

#pragma GCC push_options
#pragma GCC optimize ("O0")
template <typename Value>
void Callback(Value&v ) {}
#pragma GCC pop_options

std::vector<int> TestIntValues() {
  std::mt19937 rand(0);
  std::vector<int> values;
  std::uniform_int_distribution<int> dist(0, std::numeric_limits<int>::max());
  for (int i = 0; i < kBenchmarkIters; ++i) {
    values.push_back(dist(rand));
  }
  return values;
}

template <typename Map>
void DoInsertIntTest(benchmark::State& state, Map* map) {
  std::vector<int> values = TestIntValues();
  while (state.KeepRunning()) {
    for (unsigned v : values) {
      (*map)[v] = v + 1;
    }
  }
  Callback(*map);
}

template <typename Map>
void DoLookupIntTest(benchmark::State& state, Map* map) {
  std::vector<int> values = TestIntValues();
  for (unsigned v : values) {
    (*map)[v] = v + 1;
  }

  while (state.KeepRunning()) {
    for (unsigned v : values) {
      Callback((*map)[v]);
    }
  }
}

std::vector<std::string> TestStringValues() {
  std::mt19937 rand(0);
  std::vector<std::string> values;
  std::uniform_int_distribution<int> length_dist(1, 128);
  std::uniform_int_distribution<int> char_dist(0, 64);
  for (int i = 0; i < kBenchmarkIters; ++i) {
    int length = length_dist(rand);
    std::string v;
    for (int i = 0; i < length; ++i) {
      v.append(1, ' ' + char_dist(rand));
    }
    values.push_back(v);
  }
  return values;
}

template <typename Map>
void DoInsertStringTest(benchmark::State& state, Map* map) {
  std::vector<std::string> values = TestStringValues();
  while (state.KeepRunning()) {
    int n = 0;
    for (const auto& v : values) {
      (*map)[v] = n++;;
    }
  }
  Callback(*map);
}

template <typename Map>
void DoLookupStringTest(benchmark::State& state, Map* map) {
  std::vector<std::string> values = TestStringValues();
  int n=0;
  for (const auto& v : values) {
    (*map)[v] = n++;
  }

  while (state.KeepRunning()) {
    for (const auto& v : values) {
      Callback((*map)[v]);
    }
  }
}

void BM_Insert_InlinedMap_Int(benchmark::State& state) {
  InlinedHashMap<int, int, 8> map;
  DoInsertIntTest(state, &map);
}
BENCHMARK(BM_Insert_InlinedMap_Int);

void BM_Insert_UnorderedMap_Int(benchmark::State& state) {
  std::unordered_map<int, int> map;
  DoInsertIntTest(state, &map);
}
BENCHMARK(BM_Insert_UnorderedMap_Int);

void BM_Insert_DenseHashMap_Int(benchmark::State& state) {
  google::dense_hash_map<int, int> map;
  map.set_empty_key(-1);
  map.set_deleted_key(-1);
  DoInsertIntTest(state, &map);
}
BENCHMARK(BM_Insert_DenseHashMap_Int);

void BM_Lookup_InlinedMap_Int(benchmark::State& state) {
  InlinedHashMap<int, int, 8> map;
  DoLookupIntTest(state, &map);
}

BENCHMARK(BM_Lookup_InlinedMap_Int);

void BM_Lookup_UnorderedMap_Int(benchmark::State& state) {
  std::unordered_map<int, int> map;
  DoLookupIntTest(state, &map);
}

BENCHMARK(BM_Lookup_UnorderedMap_Int);

void BM_Lookup_DenseHashMap_Int(benchmark::State& state) {
  google::dense_hash_map<int, int> map;
  map.set_empty_key(-1);
  map.set_deleted_key(-2);
  DoLookupIntTest(state, &map);
}

BENCHMARK(BM_Lookup_DenseHashMap_Int);

void BM_Insert_InlinedMap_String(benchmark::State& state) {
  InlinedHashMap<std::string, int, 8> map;
  DoInsertStringTest(state, &map);
}
BENCHMARK(BM_Insert_InlinedMap_String);

void BM_Insert_UnorderedMap_String(benchmark::State& state) {
  std::unordered_map<std::string, int> map;
  DoInsertStringTest(state, &map);
}
BENCHMARK(BM_Insert_UnorderedMap_String);

void BM_Insert_DenseHashMap_String(benchmark::State& state) {
  google::dense_hash_map<std::string, int> map;
  map.set_empty_key("");
  map.set_deleted_key("d");
  DoInsertStringTest(state, &map);
}
BENCHMARK(BM_Insert_DenseHashMap_String);

void BM_Lookup_InlinedMap_String(benchmark::State& state) {
  InlinedHashMap<std::string, int, 8> map;
  DoLookupStringTest(state, &map);
}

BENCHMARK(BM_Lookup_InlinedMap_String);

void BM_Lookup_UnorderedMap_String(benchmark::State& state) {
  std::unordered_map<std::string, int> map;
  DoLookupStringTest(state, &map);
}

BENCHMARK(BM_Lookup_UnorderedMap_String);

void BM_Lookup_DenseHashMap_String(benchmark::State& state) {
  google::dense_hash_map<std::string, int> map;
  map.set_empty_key("");
  map.set_deleted_key("d");
  DoLookupStringTest(state, &map);
}

BENCHMARK(BM_Lookup_DenseHashMap_String);

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  bool run_benchmark = false;
  for (int i = 1; i < argc; ++i) {
    if (strncmp(argv[i], "--benchmark", 11) == 0) {
      run_benchmark = true;
      break;
    }
  }
  if (run_benchmark) {
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    return 0;
  }
  return RUN_ALL_TESTS();
}
