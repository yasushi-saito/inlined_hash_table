
#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "inlined_hash_table.h"

class StrTableOptions {
 public:
  const std::string& EmptyKey() const { return empty_key_; };
  const std::string& DeletedKey() const { return deleted_key_; }

 private:
  std::string empty_key_;
  std::string deleted_key_ = "xxx";
};

class IntTableOptions {
 public:
  int EmptyKey() const { return -1; }
  int DeletedKey() const { return -2; }
};

using Map = InlinedHashMap<std::string, std::string, 8, StrTableOptions>;
using Set = InlinedHashSet<std::string, 8, StrTableOptions>;

TEST(InlinedHashMap, Simple) {
  Map t;
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

TEST(InlinedHashMap, Clear) {
  Map t;
  t["h0"] = "w0";
  t["h1"] = "w1";
  t.clear();
  EXPECT_TRUE(t.empty());
  EXPECT_EQ(0, t.size());
  EXPECT_TRUE(t.find("h0") == t.end());
  EXPECT_TRUE(t.find("h1") == t.end());
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
}

TEST(InlinedHashMap, OptionsWithoutDeletedKeyWorks) {
  class StrTableOptionsWithoutDeletion {
   public:
    const std::string& EmptyKey() const { return empty_key_; };

   private:
    std::string empty_key_;
    std::string deleted_key_ = "xxx";
  };
  InlinedHashMap<std::string, std::string, 8, StrTableOptionsWithoutDeletion> t;

  EXPECT_TRUE(t.empty());
  EXPECT_TRUE(t.insert(std::make_pair("hello", "world")).second);
  EXPECT_EQ("world", t["hello"]);
  EXPECT_FALSE(t.empty());
  t.clear();
  EXPECT_TRUE(t.empty());
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

TEST(InlinedHashMap, EmptyInlinedArray) {
  InlinedHashSet<int, 0, IntTableOptions> s;
  ASSERT_TRUE(s.insert(10).second);
  ASSERT_TRUE(s.insert(11).second);
  ASSERT_FALSE(s.insert(10).second);
}

TEST(InlinedHashSet, Random) {
  InlinedHashSet<int, 8, IntTableOptions> t;
  std::unordered_set<int> model;

  std::mt19937 rand(0);
  for (int i = 0; i < 10000; ++i) {
    int op = rand() % 100;
    if (op < 50) {
      int n = rand() % 100;
      // std::cout << i << "Insert " << n << "\n";
      ASSERT_EQ(t.insert(n).second, model.insert(n).second);
    } else if (op < 70) {
      int n = rand() % 100;
      // std::cout << i << "Erase " << n << "\n";
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
    ASSERT_EQ(elems_in_t, elems_in_model);
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
  // std::shuffle(values.begin(), values.end(), rand);
  start = std::chrono::system_clock::now();
  for (unsigned v : values) {
    ASSERT_EQ(map[v], v + 1);
  }
  end = std::chrono::system_clock::now();
  elapsed = end - start;
  std::cout << "Lookup elapsed: " << elapsed.count() << "\n";
}

TEST(Benchmark, InlinedMapInsert) {
  class IntTableOptions {
   public:
    unsigned EmptyKey() const { return -1; }
    unsigned DeletedKey() const { return -2; }
  };
  std::vector<unsigned> values = TestValues();
  auto start = std::chrono::system_clock::now();
  InlinedHashMap<unsigned, unsigned, 8, IntTableOptions> map;
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
  // std::shuffle(values.begin(), values.end(), rand);
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
