
#include <gtest/gtest.h>

#define NDEBUG 1
#include <chrono>
#include <google/dense_hash_map>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "benchmark/benchmark.h"
#include "inlined_hash_table.h"

extern "C" {
void ProfilerStart(const char* path);
void ProfilerStop();
}

using Map = InlinedHashMap<std::string, std::string, 8>;
using Set = InlinedHashSet<std::string, 8>;

template <typename Key, typename Value, int NumInlinedBuckets, typename GetKey,
          typename Hash, typename EqualTo, typename IndexType>
void InlinedHashTable<Key, Value, NumInlinedBuckets, GetKey, Hash, EqualTo,
                      IndexType>::CheckConsistency() {
  const Array& array = array_;
  for (IndexType bi = 0; bi < array.capacity(); ++bi) {
    const Bucket& bucket = array.GetBucket(bi);

    BucketMetadata::LeafIterator it(&bucket.md);
    int distance;
    while ((distance = it.Next()) >= 0) {
      const Bucket& leaf =
          array.GetBucket((bi + distance) & array.capacity_mask());
      ASSERT_TRUE(leaf.md.IsOccupied());
      size_t hash = hash_(get_key_.Get(leaf.value.Get()));
      ASSERT_EQ(array.Clamp(hash), bi);
    }
    if (bucket.md.IsOccupied()) {
      size_t hash = hash_(get_key_.Get(bucket.value.Get()));
      IndexType origin_index = array.Clamp(hash);
      const Bucket& origin = array.GetBucket(origin_index);
      ASSERT_TRUE(origin.md.HasLeaf(array.Distance(origin_index, bi)));
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

TEST(ManualConstructor, String) {
  InlinedHashTableManualConstructor<std::string> m;
  static_assert(sizeof(m) == sizeof(std::string), "size");
  m.New("foobar");
  EXPECT_EQ(m.Get(), "foobar");
  *m.Mutable() = "hello";
  EXPECT_EQ(m.Get(), "hello");
  m.Delete();
}

TEST(ManualConstructor, Int) {
  InlinedHashTableManualConstructor<int> m;
  static_assert(sizeof(m) == sizeof(int), "size");
  m.New(4);
  EXPECT_EQ(m.Get(), 4);
  *m.Mutable() = 5;
  EXPECT_EQ(m.Get(), 5);
  m.Delete();
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

#pragma GCC push_options
#pragma GCC optimize("O0")
template <typename Value>
Value& Callback(Value& v) {
  return v;
}
#pragma GCC pop_options

template <typename T>
std::vector<T> TestValues(int num_values) {
  abort();
}

template <>
std::vector<std::string> TestValues<std::string>(int num_values) {
  std::mt19937 rand(0);
  std::vector<std::string> values;
  std::uniform_int_distribution<int> length_dist(1, 128);
  std::uniform_int_distribution<int> char_dist(0, 64);
  for (int i = 0; i < num_values; ++i) {
    int length = length_dist(rand);
    std::string v;
    for (int i = 0; i < length; ++i) {
      v.append(1, ' ' + char_dist(rand));
    }
    values.push_back(v);
  }
  return values;
}

template <>
std::vector<int> TestValues<int>(int num_values) {
  std::mt19937 rand(0);
  std::vector<int> values;
  std::uniform_int_distribution<int> dist(0, std::numeric_limits<int>::max());
  for (int i = 0; i < num_values; ++i) {
    values.push_back(dist(rand));
  }
  return values;
}

template <typename Key, typename NewMapCallback>
void DoInsertTest(benchmark::State& state, NewMapCallback cb) {
  std::vector<Key> values = TestValues<Key>(state.range(0));
  while (state.KeepRunning()) {
    auto map = cb();
    int n = 0;
    for (const Key& v : values) {
      Callback((*map)[v]) = n++;
    }
    state.PauseTiming();
    map.reset();
    state.ResumeTiming();
  }
}

template <typename Key, typename Map>
void DoLookupTest(benchmark::State& state, std::unique_ptr<Map> map) {
  std::vector<Key> values = TestValues<Key>(state.range(0));
  int n = 0;
  for (const Key& v : values) {
    (*map)[v] = n++;
  }

  while (state.KeepRunning()) {
    for (const Key& v : values) {
      Callback((*map)[v]);
    }
  }
}

#if 0
template <typename Key, typename Map>
void DoDeleteTest(benchmark::State& state, std::unique_ptr<Map> map) {
  std::vector<Key> values0 = TestValues<Key>(state.range(0) / 2);
  std::vector<Key> values1 = TestValues<Key>(state.range(0) / 2);
  while (state.KeepRunning()) {
    int n = 0;
    for (const Key& v : values) {
      (*map)[v] = n++;
    }
    for (const Key& v : values) {
      Callback((*map)[v]);
    }
  }
}
#endif

int kMinValues = 4;
int kMaxValues = 1024 * 1024;

template <typename Key>
std::unique_ptr<InlinedHashMap<Key, int64_t, 0>> NewInlinedHashMap() {
  return std::unique_ptr<InlinedHashMap<Key, int64_t, 0>>(
      new InlinedHashMap<Key, int64_t, 0>);
}

template <typename Key>
std::unique_ptr<std::unordered_map<Key, int64_t>> NewUnorderedMap() {
  return std::unique_ptr<std::unordered_map<Key, int64_t>>(
      new std::unordered_map<Key, int64_t>);
}

template <typename Key>
std::unique_ptr<google::dense_hash_map<Key, int64_t>> NewDenseHashMap() {
  auto map = std::unique_ptr<google::dense_hash_map<Key, int64_t>>(
      new google::dense_hash_map<Key, int64_t>);
  if
    constexpr(std::is_same<Key, std::string>::value) {
      map->set_empty_key("");
      map->set_deleted_key("d");
    }
  else {
    map->set_empty_key(-1);
    map->set_deleted_key(-1);
  }
  return map;
}

void BM_Insert_InlinedMap_Int(benchmark::State& state) {
  DoInsertTest<int>(state, []() { return NewInlinedHashMap<int>(); });
}
BENCHMARK(BM_Insert_InlinedMap_Int)->Range(kMinValues, kMaxValues);

void BM_Insert_UnorderedMap_Int(benchmark::State& state) {
  DoInsertTest<int>(state, []() { return NewUnorderedMap<int>(); });
}
BENCHMARK(BM_Insert_UnorderedMap_Int)->Range(kMinValues, kMaxValues);

void BM_Insert_DenseHashMap_Int(benchmark::State& state) {
  DoInsertTest<int>(state, []() { return NewDenseHashMap<int>(); });
}
BENCHMARK(BM_Insert_DenseHashMap_Int)->Range(kMinValues, kMaxValues);

void BM_Lookup_InlinedMap_Int(benchmark::State& state) {
  DoLookupTest<int>(state, NewInlinedHashMap<int>());
}

BENCHMARK(BM_Lookup_InlinedMap_Int)->Range(kMinValues, kMaxValues);

void BM_Lookup_UnorderedMap_Int(benchmark::State& state) {
  DoLookupTest<int>(state, NewUnorderedMap<int>());
}

BENCHMARK(BM_Lookup_UnorderedMap_Int)->Range(kMinValues, kMaxValues);

void BM_Lookup_DenseHashMap_Int(benchmark::State& state) {
  DoLookupTest<int>(state, NewDenseHashMap<int>());
}

BENCHMARK(BM_Lookup_DenseHashMap_Int)->Range(kMinValues, kMaxValues);

void BM_Insert_InlinedMap_String(benchmark::State& state) {
  DoInsertTest<std::string>(state,
                            []() { return NewInlinedHashMap<std::string>(); });
}
BENCHMARK(BM_Insert_InlinedMap_String)->Range(kMinValues, kMaxValues);

void BM_Insert_UnorderedMap_String(benchmark::State& state) {
  DoInsertTest<std::string>(state,
                            []() { return NewUnorderedMap<std::string>(); });
}

BENCHMARK(BM_Insert_UnorderedMap_String)->Range(kMinValues, kMaxValues);

void BM_Insert_DenseHashMap_String(benchmark::State& state) {
  DoInsertTest<std::string>(state,
                            []() { return NewDenseHashMap<std::string>(); });
}
BENCHMARK(BM_Insert_DenseHashMap_String)->Range(kMinValues, kMaxValues);

void BM_Lookup_InlinedMap_String(benchmark::State& state) {
  DoLookupTest<std::string>(state, NewInlinedHashMap<std::string>());
}

BENCHMARK(BM_Lookup_InlinedMap_String)->Range(kMinValues, kMaxValues);

void BM_Lookup_UnorderedMap_String(benchmark::State& state) {
  DoLookupTest<std::string>(state, NewUnorderedMap<std::string>());
}

BENCHMARK(BM_Lookup_UnorderedMap_String)->Range(kMinValues, kMaxValues);

void BM_Lookup_DenseHashMap_String(benchmark::State& state) {
  DoLookupTest<std::string>(state, NewDenseHashMap<std::string>());
}

BENCHMARK(BM_Lookup_DenseHashMap_String)->Range(kMinValues, kMaxValues);

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  bool run_benchmark = false;

  for (int i = 1; i < argc; ++i) {
    if (strncmp(argv[i], "--cpu_profile=", strlen("--cpu_profile=")) == 0) {
      const char* cpu_profile = argv[i] + 14;
      std::cerr << "Recording CPU profile in " << cpu_profile << "\n";
      ProfilerStart(cpu_profile);
      atexit(ProfilerStop);
    }
    if (strncmp(argv[i], "--benchmark", 11) == 0) {
      run_benchmark = true;
    }
  }
  if (run_benchmark) {
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    return 0;
  }
  return RUN_ALL_TESTS();
}
