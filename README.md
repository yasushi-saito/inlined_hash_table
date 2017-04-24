# inlined_hash_table

Fast open-addressed C++ hash table

The API is close to
google's
[dense hash map](http://goog-sparsehash.sourceforge.net/doc/dense_hash_map.html),
but it should be faster (I'd hope).

## Prerequisites

You need a C++-11 compiler. I tested using gcc-4.8/libstdc++ and clang-4.0/libc++.

## Installation

The library consists of a single header file with no extra dependencies. Just
copy it to where you want.

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
  for (auto [key, value] = map) {
     printf("entry %d %d\n", key, value);
  }
}
```

InlinedHashMap must be given an Option template type. The Option type must define two methods,
`EmptyKey` and `DeletedKey`.


## Performance

Lookup and insert are both about 2x faster than clang4+libc++ in small
benchmarks, where all the data fits in L2. More to come.
