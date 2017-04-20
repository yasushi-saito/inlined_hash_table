
#include <gtest/gtest.h>
#include <string>

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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
