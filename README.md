# inlined_hash_table

Fast open-addressed C++ hash table

The API is close to
google's
[dense hash map](http://goog-sparsehash.sourceforge.net/doc/dense_hash_map.html),
but it should be faster (I'd hope).

## Prerequisites

You need a C++-11 compiler. I tested using gcc-4.8/libstdc++ and clang-4.0/libc++.

## Installation

The library consists of a single header file with no extra dependency. Just copy
it to where you want.

The cmakefiles are for unittests, and you can ignore them.

## Usage

```
class Options {
 public:
  int EmptyKey() const { return -1; }
  int DeletedKey() const { return -2; }
};

using Map = InlinedHashMap<int, int, 8, Options>;

void Test() {
  Map map;
  map[1] = 2;
  map[3] = 4;
  for (auto [key, value] : map) {
     printf("entry %d %d\n", key, value);
  }
}
```

The above example creates a intger→integer hash map. It uses -1 as an empty key,
and -2 as the deleted key (tombstones). That is, all empty buckets in the hash
map are filled with -1.

After erasing an existing element, the bucket is set to -2. You cannot use -1 or
-2 as a valid key. `EmptyKey()` is needed only when `InlineHashTable::erase()`
is going to be used.

The third parameter, `8`, defines the number of elements stored in-line with the
hash map. That is, up to 8 elements can be stored in the hash table without
`malloc`. It is allowed to set this parameter to 0.


## Performance

Lookup and insert are both about 2x faster than clang4+libc++ in small
benchmarks, where all the data fits in L2. More to come.
