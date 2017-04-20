# inlined_hash_table

Fast open-addressed C++ hash table

The API is very close to google's
[dense hash map](http://goog-sparsehash.sourceforge.net/doc/dense_hash_map.html),
but it should be faster (I'd hope).

## Prerequisites

You need a C++-11 compiler. I tested using gcc-4.8/libstdc++ and clang-4.0/libc++.

## Installation

The library consists of a single header file with no extra dependencies. Just
copy it to where you want.

The cmakefiles are for unittests, and you can ignore them.

## Performance

Lookup and insert are both about 2x faster than clang4+libc++ in small
benchmarks, where all the data fits in L2. More to come.
