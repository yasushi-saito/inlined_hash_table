
#include <gtest/gtest.h>
#include <string>

#include "inlined_hash_table.h"

using Table = InlinedHashMap<std::string, std::string, 8>;

TEST(InlinedHashTable, Simple) {
  Table t;
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
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
