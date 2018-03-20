# InlinedHashTable and HopScotchHashTable

InlinedHashTable is a fast open-addressed C++ hash table.  It is very similar to
google's
[dense hash map](http://goog-sparsehash.sourceforge.net/doc/dense_hash_map.html),
but it's takes less memory. HopScotchHashTable is an implementation of the
following paper.  It is _not_ thread safe; it's merely thread compatible.

[Hopscotch hashing](https://pdfs.semanticscholar.org/48c2/af3d559fb2c7ef5e71efd24ab5ae217c1fee.pdf),
Maurice Herlihy, Nir Shavit, Moran Tzafrir.

`InlinedHashTable` is small, simple and fast, especially when keys and values
are small (think integers and floating points). The downside is that it needs
two special keys, _empty key_ and _deleted keys_ to represent empty slots and
tombstones. Besides a just bit cumbersome to configure, these two cannot be used
as regular keys.

`HopScotchHashTable` is a bit slower than `InlinedHashTable` but faster than
`std::unordered_map`. It doesn't require empty nor deleted keys. One of the
advantages of this algorithm is that it uses linear probing to handle
collisions, but yet it can handle very high load factor. So it should perform
better on a very large data set.

## Prerequisites

You need a C++-11 compiler. I tested using gcc-4.8/libstdc++ and clang-4.0/libc++.
To run the test and benchmark, you need to install google sparsehash. For ubuntu, do:

    sudo apt-get install g++ libsparsehash-dev

Follow the following descriptions to install gtest:

    https://www.eriksmistad.no/getting-started-with-google-test-on-ubuntu/

To build the test:

    cmake .
    make -j8


## Installation

The library consists of a single header file with no extra dependency. Just copy
it to where you want.

The cmakefiles are for unittests, and you can ignore them.

### Using InlinedHashTable

See the header file for more details.

```
class Options {
 public:
  static constexpr int EmptyKey() { return -1; }  // required
  static constexpr int DeletedKey() { return -2; }  // optional
  static constexpr double MaxLoadFactor() { return 0.75; } // optional
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

The above example creates an integer→integer hash map. It uses -1 as an empty
key, and -2 as the deleted key (tombstones).  After erasing an existing element,
the bucket is set to -2. You cannot use -1 or -2 as a valid key. `DeletedKey()`
is needed only when `InlinedHashTable::erase()` is going to be used.

The third parameter, `8`, defines the number of elements stored in-line with the
hash map. That is, up to 8 elements can be stored in the hash table without
`new`. It is allowed to set this parameter to 0.

### Iterator invalidation semantics for InlinedHashTable

It's the same as dense\_hash\_map's, and is weaker than std::unordered\_map's:

- Insertion invalidates outstanding iterators.

- Erasure keeps iterators valid, except those referring to the element being
  erased.

## Using HopScotchHashTable

See the header file for more details. The template parameters are the same as
`std::unordered_map`s. Iterator invalidation semantics is the same as
InlinedHashTable.

## Performance

Lookup and insert are faster than std::unordered_map, and in par with
dense\_hash\_map. inlined\_hash\_map has much smaller memory footprint for small
tables. An empty dense\_hash\_map takes 88 bytes, whereas inlined\_hash\_map
takes 24 bytes, so if you create lots of small maps, the latter will start
performing better.

The following tests are done on clang++(4.0) on a Haswell-grade CPU. We used
tcmalloc for memory allocation.  The numbers after "/" are the number of
elements inserted or looked up.

<pre>
BM_Insert_HopScotchMap_Int/4                   943 ns        943 ns     604541
BM_Insert_HopScotchMap_Int/8                  1052 ns       1052 ns     664131
BM_Insert_HopScotchMap_Int/64                 2526 ns       2525 ns     277414
BM_Insert_HopScotchMap_Int/512               29075 ns      29085 ns      24100
BM_Insert_HopScotchMap_Int/4096             282043 ns     282080 ns       2481
BM_Insert_HopScotchMap_Int/32768           2543595 ns    2543481 ns        276
BM_Insert_HopScotchMap_Int/262144         20309576 ns   20308259 ns         34
BM_Insert_HopScotchMap_Int/1048576       101457974 ns  101448429 ns          7
BM_Insert_InlinedMap_Int/4                     845 ns        845 ns     827415
BM_Insert_InlinedMap_Int/8                     868 ns        869 ns     812258
BM_Insert_InlinedMap_Int/64                   1579 ns       1579 ns     443574
BM_Insert_InlinedMap_Int/512                  5946 ns       5947 ns     116834
BM_Insert_InlinedMap_Int/4096                86386 ns      86417 ns       8124
BM_Insert_InlinedMap_Int/32768              826286 ns     826297 ns        848
BM_Insert_InlinedMap_Int/262144           13224904 ns   13224163 ns         53
BM_Insert_InlinedMap_Int/1048576          59225263 ns   59219832 ns         12
BM_Insert_UnorderedMap_Int/4                   811 ns        810 ns     858459
BM_Insert_UnorderedMap_Int/8                   895 ns        894 ns     784535
BM_Insert_UnorderedMap_Int/64                 2420 ns       2420 ns     290581
BM_Insert_UnorderedMap_Int/512               16677 ns      16682 ns      42058
BM_Insert_UnorderedMap_Int/4096             184362 ns     184355 ns       3765
BM_Insert_UnorderedMap_Int/32768           2529515 ns    2529361 ns        277
BM_Insert_UnorderedMap_Int/262144         31579244 ns   31575242 ns         22
BM_Insert_UnorderedMap_Int/1048576       151387016 ns  151371978 ns          5
BM_Insert_DenseHashMap_Int/4                   802 ns        802 ns     872195
BM_Insert_DenseHashMap_Int/8                   847 ns        847 ns     826408
BM_Insert_DenseHashMap_Int/64                 1804 ns       1802 ns     387969
BM_Insert_DenseHashMap_Int/512                9867 ns       9879 ns      70510
BM_Insert_DenseHashMap_Int/4096             110718 ns     110745 ns       6318
BM_Insert_DenseHashMap_Int/32768           1325141 ns    1325071 ns        528
BM_Insert_DenseHashMap_Int/262144         11938077 ns   11937301 ns         58
BM_Insert_DenseHashMap_Int/1048576        63407206 ns   63401394 ns         11
BM_Lookup_HopScotchMap_Int/4                    16 ns         16 ns   45118013
BM_Lookup_HopScotchMap_Int/8                    27 ns         27 ns   25819569
BM_Lookup_HopScotchMap_Int/64                  228 ns        228 ns    3062728
BM_Lookup_HopScotchMap_Int/512                1553 ns       1553 ns     451592
BM_Lookup_HopScotchMap_Int/4096              20555 ns      20553 ns      34254
BM_Lookup_HopScotchMap_Int/32768            300977 ns     300948 ns       2333
BM_Lookup_HopScotchMap_Int/262144          3456593 ns    3456249 ns        226
BM_Lookup_HopScotchMap_Int/1048576        28870478 ns   28867499 ns         24
BM_Lookup_InlinedMap_Int/4                      10 ns         10 ns   69227338
BM_Lookup_InlinedMap_Int/8                      14 ns         14 ns   49921994
BM_Lookup_InlinedMap_Int/64                     91 ns         91 ns    7061233
BM_Lookup_InlinedMap_Int/512                   695 ns        695 ns    1019383
BM_Lookup_InlinedMap_Int/4096                 8734 ns       8733 ns      84831
BM_Lookup_InlinedMap_Int/32768              213709 ns     213687 ns       3284
BM_Lookup_InlinedMap_Int/262144            1058658 ns    1058555 ns        663
BM_Lookup_InlinedMap_Int/1048576          10901019 ns   10899926 ns         65
BM_Lookup_UnorderedMap_Int/4                    24 ns         24 ns   29666537
BM_Lookup_UnorderedMap_Int/8                    53 ns         53 ns   13037353
BM_Lookup_UnorderedMap_Int/64                  356 ns        356 ns    1971142
BM_Lookup_UnorderedMap_Int/512                3099 ns       3098 ns     225246
BM_Lookup_UnorderedMap_Int/4096              40833 ns      40830 ns      17003
BM_Lookup_UnorderedMap_Int/32768            492502 ns     492451 ns       1422
BM_Lookup_UnorderedMap_Int/262144          4854618 ns    4854140 ns        145
BM_Lookup_UnorderedMap_Int/1048576        48787983 ns   48783085 ns         14
BM_Lookup_DenseHashMap_Int/4                     7 ns          7 ns  102440344
BM_Lookup_DenseHashMap_Int/8                    13 ns         13 ns   55428597
BM_Lookup_DenseHashMap_Int/64                  117 ns        117 ns    6017370
BM_Lookup_DenseHashMap_Int/512                 947 ns        947 ns     745749
BM_Lookup_DenseHashMap_Int/4096              19644 ns      19642 ns      35107
BM_Lookup_DenseHashMap_Int/32768            230975 ns     230953 ns       3016
BM_Lookup_DenseHashMap_Int/262144          1903844 ns    1903660 ns        369
BM_Lookup_DenseHashMap_Int/1048576        16903422 ns   16901719 ns         41
BM_Insert_HopScotchMap_String/4               1081 ns       1080 ns     648769
BM_Insert_HopScotchMap_String/8               1384 ns       1383 ns     505411
BM_Insert_HopScotchMap_String/64              5113 ns       5109 ns     136416
BM_Insert_HopScotchMap_String/512            62336 ns      62333 ns      11218
BM_Insert_HopScotchMap_String/4096          654178 ns     654153 ns       1066
BM_Insert_HopScotchMap_String/32768        5926282 ns    5925871 ns        119
BM_Insert_HopScotchMap_String/262144      81605466 ns   81596382 ns          9
BM_Insert_HopScotchMap_String/1048576    398059140 ns  398023573 ns          2
</pre>
