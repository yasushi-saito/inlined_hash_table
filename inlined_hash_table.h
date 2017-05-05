// Author: yasushi.saito@gmail.com

#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <type_traits>

// InlinedHashTable is an implementation detail that underlies InlinedHashMap
// and InlinedHashSet. It's not for public use.
//
// NumInlinedBuckets is the number of elements stored in-line with the table.
//
// Options is a class that defines one required method, and two optional
// methods.
//
//   const Key& EmptyKey() const;    // required
//   const Key& DeletedKey() const;  // optional
//   double MaxLoadFactor() const;   // optional
//
// EmptyKey() should return a key that represents an unused key.  DeletedKey()
// should return a tombstone key. DeletedKey() needs to be defined iff you use
// erase(). MaxLoadFactor() defines when the hash table is expanded. The default
// value is 0.75, meaning that if the number of non-empty slots in the table
// exceeds 75% of the capacity, the hash table is doubled. The valid range of
// MaxLoadFactor() is (0,1].
//
// Caution: each method must return the same value across multiple invocations.
// Returning a compile-time constant allows the compiler to optimize the code
// well.
//
// TODO: implement bucket reservation.

struct DefaultInlinedHashTableOptions {
  static constexpr double MaxLoadFactor() { return 0.75; }
};

class InlinedHashTableBucketMetadata {
 public:
  InlinedHashTableBucketMetadata() {
    mask_ = 0;
    origin_ = 0;
  }

  class LeafIterator {
   public:
    explicit LeafIterator(const InlinedHashTableBucketMetadata* md)
        : mask_(md->mask_), base_(-1) {}
    int Next() {
      int i = __builtin_ffs(mask_);
      if (i == 0) return -1;

      mask_ >>= i;
      base_ += i;
      return base_;
    }

   private:
    unsigned mask_;
    int base_;
  };

  // bool HasAnyLeaf() const { return mask_ != 0; }
  bool HasLeaf(int index) const { return (mask_ & (1U << index)) != 0; }
  void SetLeaf(int index) {
    assert(!HasLeaf(index));
    mask_ |= (1U << index);
  }

  void ClearLeaf(int index) {
    assert(HasLeaf(index));
    mask_ &= ~(1U << index);
  }

  bool IsOccupied() const { return origin_ != 0; }
  void SetOrigin(int delta_from_origin) {
    assert(delta_from_origin < 32);
    origin_ = delta_from_origin + 1;
  }
  void ClearOrigin() { origin_ = 0; }

  int GetOrigin() const {
    if (origin_ == 0) return -1;
    return origin_ - 1;
  }

  void ClearAll() {
    mask_ = 0;
    origin_ = 0;
  }

 private:
  unsigned mask_ : 27;
  unsigned origin_ : 5;
};

template <typename Bucket, typename Key, typename Value, int NumInlinedBuckets,
          typename Options, typename GetKey, typename Hash, typename EqualTo,
          typename IndexType>
class InlinedHashTable {
 public:
  using BucketMetadata = InlinedHashTableBucketMetadata;
  static_assert((NumInlinedBuckets & (NumInlinedBuckets - 1)) == 0,
                "NumInlinedBuckets must be a power of two");
  InlinedHashTable(IndexType bucket_count, const Options& options,
                   const Hash& hash, const EqualTo& equal_to)
      : options_(options),
        hash_(hash),
        equal_to_(equal_to),
        array_(ComputeCapacity(bucket_count)) {}

  InlinedHashTable(const InlinedHashTable& other) : array_(NumInlinedBuckets) {
    *this = other;
  }
  InlinedHashTable(InlinedHashTable&& other) : array_(NumInlinedBuckets) {
    *this = std::move(other);
  }

  InlinedHashTable& operator=(const InlinedHashTable& other) {
    array_ = other.array_;
    options_ = other.options_;
    get_key_ = other.get_key_;
    hash_ = other.hash_;
    equal_to_ = other.equal_to_;
    return *this;
  }
  InlinedHashTable& operator=(InlinedHashTable&& other) {
    array_ = std::move(other.array_);
    options_ = std::move(other.options_);
    get_key_ = std::move(other.get_key_);
    hash_ = std::move(other.hash_);
    equal_to_ = std::move(other.equal_to_);
    return *this;
  }

  class iterator {
   public:
    using Table = InlinedHashTable<Bucket, Key, Value, NumInlinedBuckets,
                                   Options, GetKey, Hash, EqualTo, IndexType>;
    iterator(Table* table, IndexType index) : table_(table), index_(index) {}
    iterator(const typename Table::iterator& i)
        : table_(i.table_), index_(i.index_) {}
    bool operator==(const iterator& other) const {
      return index_ == other.index_;
    }
    bool operator!=(const iterator& other) const {
      return index_ != other.index_;
    }

    Value& operator*() const { return table_->Mutable(index_)->value; }
    Value* operator->() const { return &table_->Mutable(index_)->value; }

    iterator operator++() {  // ++it
      index_ = table_->NextValidElementInArray(table_->array_, index_ + 1);
      return *this;
    }

    iterator operator++(int unused) {  // it++
      iterator r(*this);
      index_ = table_->NextValidElementInArray(table_->array_, index_ + 1);
      return r;
    }

   private:
    friend Table;
    Table* table_;
    IndexType index_;
  };

  class const_iterator {
   public:
    using Table = InlinedHashTable<Bucket, Key, Value, NumInlinedBuckets,
                                   Options, GetKey, Hash, EqualTo, IndexType>;
    const_iterator() {}
    const_iterator(const Table::iterator& i)
        : table_(i.table_), index_(i.index_) {}
    const_iterator(const Table::const_iterator& i)
        : table_(i.table_), index_(i.index_) {}
    const_iterator(const Table* table, IndexType index)
        : table_(table), index_(index) {}
    bool operator==(const const_iterator& other) const {
      return index_ == other.index_;
    }
    bool operator!=(const const_iterator& other) const {
      return index_ != other.index_;
    }

    const Value& operator*() const { return table_->Get(index_).value; }
    const Value* operator->() const { return &table_->Get(index_).value; }

    const_iterator operator++() {  // ++it
      index_ = table_->NextValidElementInArray(table_->array_, index_ + 1);
      return *this;
    }

    const_iterator operator++(int unused) {  // it++
      const_iterator r(*this);
      index_ = table_->NextValidElementInArray(table_->array_, index_ + 1);
      return r;
    }

   private:
    friend Table;
    const Table* table_;
    IndexType index_;
  };

  // TODO(saito) Support const_iterator, cbegin, etc.

  iterator begin() {
    return iterator(this, NextValidElementInArray(array_, 0));
  }

  iterator end() { return iterator(this, kEnd); }

  const_iterator cbegin() const {
    return const_iterator(this, NextValidElementInArray(array_, 0));
  }
  const_iterator cend() const { return const_iterator(this, kEnd); }

  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }

  iterator find(const Key& k) {
    IndexType index;
    if (FindInArray(array_, k, &index)) {
      return iterator(this, index);
    } else {
      return end();
    }
  }

  const_iterator find(const Key& k) const {
    IndexType index;
    if (FindInArray(array_, k, &index)) {
      return const_iterator(this, index);
    } else {
      return cend();
    }
  }

  void clear() {
    for (Bucket& elem : array_.inlined) {
      elem.md.ClearAll();
      elem.value = Value();
    }
    if (array_.outlined != nullptr) {
      for (size_t i = 0; i < array_.capacity - array_.inlined.size(); ++i) {
        array_.outlined[i].md.ClearAll();
        array_.outlined[i].value = Value();
      }
    }
    array_.size = 0;
    array_.num_empty_slots = array_.num_empty_slots;
  }

  // Erases the element pointed to by "i". Returns the iterator to the next
  // valid element.
  iterator erase(iterator itr) {
    assert(itr.table_ == this);
    Bucket* bucket = MutableBucket(&array_, itr.index_);
    assert(bucket->md.IsOccupied());
    const int delta = bucket->md.GetOrigin();
    Bucket* origin = Mutable((itr.index_ - delta) % (array_.capacity - 1));
    origin->md.ClearLeaf(delta);
    bucket->md.ClearOrigin();
    bucket->value = Value();
    --array_.size;
    return iterator(this, NextValidElementInArray(array_, itr.index_ + 1));
  }

  // If "k" exists in the table, erase it and return 1. Else return 0.
  IndexType erase(const Key& k) {
    iterator i = find(k);
    if (i == end()) return 0;
    erase(i);
    return 1;
  }

  std::pair<iterator, bool> insert(Value&& value) {
    IndexType index;
    InsertResult result = Insert(ExtractKey(value), &index);
    if (result == KEY_FOUND) {
      return std::make_pair(iterator(this, index), false);
    }
    MutableBucket(&array_, index)->value = std::move(value);
    return std::make_pair(iterator(this, index), true);
  }

  std::pair<iterator, bool> insert(const Value& value) {
    IndexType index;
    InsertResult result = Insert(ExtractKey(value), &index);
    if (result == KEY_FOUND) {
      return std::make_pair(iterator(this, index), false);
    }
    MutableBucket(&array_, index)->value = value;
    return std::make_pair(iterator(this, index), true);
  }

  bool empty() const { return array_.size == 0; }
  IndexType size() const { return array_.size; }
  IndexType capacity() const { return array_.capacity; }

  // Backdoor methods used by map operator[].
  Bucket* Mutable(IndexType index) { return MutableBucket(&array_, index); }
  const Bucket& Get(IndexType index) const { return GetBucket(array_, index); }

  enum InsertResult { KEY_FOUND, EMPTY_SLOT_FOUND, ARRAY_FULL };
  InsertResult Insert(const Key& key, IndexType* index) {
    if (FindInArray(array_, key, index)) {
      return KEY_FOUND;
    }
    for (int iter = 0; iter < 4; ++iter) {
      InsertResult result = InsertInArray(&array_, key, index);
      if (result == KEY_FOUND) return result;
      if (result != ARRAY_FULL) {
        ++array_.size;
        return result;
      }
      ExpandTable(1);
    }
    abort();
  }

  // For unittests only
  void CheckConsistency();

 private:
  static constexpr IndexType kEnd = std::numeric_limits<IndexType>::max();

  // Representation of the hash table.
  struct Array {
   public:
    explicit Array(IndexType capacity_arg)
        : size(0), capacity(capacity_arg), num_empty_slots(capacity) {
      assert((capacity & (capacity - 1)) == 0);
      if (capacity > inlined.size()) {
        outlined.reset(new Bucket[capacity - inlined.size()]);
      }
    }

    Array(const Array& other) { *this = other; }

    Array(Array&& other) { *this = std::move(other); }

    Array& operator=(const Array& other) {
      size = other.size;
      capacity = other.capacity;
      num_empty_slots = other.num_empty_slots;
      inlined = other.inlined;
      if (other.outlined != nullptr) {
        const size_t n = other.capacity - inlined.size();
        outlined.reset(new Bucket[n]);
        std::copy(&other.outlined[0], &other.outlined[n], &outlined[0]);
      }
      return *this;
    }

    Array& operator=(Array&& other) {
      size = other.size;
      capacity = other.capacity;
      num_empty_slots = other.num_empty_slots;
      inlined = std::move(other.inlined);
      outlined = std::move(other.outlined);

      other.outlined.reset();
      other.size = 0;
      other.num_empty_slots = 0;
      other.capacity = other.inlined.size();
      return *this;
    }

    // First NumInlinedBuckets are stored in inlined. The rest are stored in
    // outlined.
    std::array<Bucket, NumInlinedBuckets> inlined;
    std::unique_ptr<Bucket[]> outlined;
    // # of filled slots.
    IndexType size;
    // Capacity of inlined + capacity of outlined. Always a power of two.
    IndexType capacity;
    // Number of empty slots, i.e., capacity - (# of filled slots + # of
    // tombstones).
    IndexType num_empty_slots;
  };

  static IndexType NextProbe(const Array& array, IndexType current,
                             int retries) {
    return (current + retries) & (array.capacity - 1);
  }

  IndexType ComputeCapacity(IndexType desired) {
    desired /= options_.MaxLoadFactor();
    if (desired < NumInlinedBuckets) desired = NumInlinedBuckets;
    if (desired <= 0) return desired;
    return static_cast<IndexType>(1)
           << static_cast<int>(std::ceil(std::log2(desired)));
  }

  // Return the index'th slot in array.
  static const Bucket& GetBucket(const Array& array, IndexType index) {
    if (index < NumInlinedBuckets) {
      return array.inlined[index];
    }
    return array.outlined[index - NumInlinedBuckets];
  }

  // Return the mutable pointer to the index'th slot in array.
  static Bucket* MutableBucket(Array* array, IndexType index) {
    if (index < NumInlinedBuckets) {
      return &array->inlined[index];
    }
    return &array->outlined[index - NumInlinedBuckets];
  }

  // Find the first filled slot at or after "from". For incremenenting an
  // iterator.
  IndexType NextValidElementInArray(const Array& array, IndexType from) const {
    IndexType i = from;
    for (;;) {
      if (i >= array.capacity) {
        return kEnd;
      }
      const Bucket& bucket = GetBucket(array, i);
      if (bucket.md.IsOccupied()) {
        return i;
      }
      ++i;
    }
  }

  // Find "k" in the array. If found, set *index to the location of the key in
  // the array.
  bool FindInArray(const Array& array, const Key& k, IndexType* index) const {
    if (__builtin_expect(array.capacity == 0, 0)) return false;

    const IndexType start_index = ComputeHash(k) & (array.capacity - 1);
    *index = start_index;
    const BucketMetadata& md = GetBucket(array, start_index).md;
    BucketMetadata::LeafIterator it(&md);
    int distance;
    while ((distance = it.Next()) >= 0) {
      *index = (start_index + distance) & (array.capacity - 1);
      const Bucket& elem = GetBucket(array, *index);
      const Key& key = ExtractKey(elem.value);
      if (KeysEqual(key, k)) {
        return true;
      }
    }
    return false;
  }

  static constexpr int MaxHopDistance() { return 27; }
  static constexpr int MaxAddDistance() { return 27; }

  // Either find "k" in the array, or find a slot into which "k" can be
  // inserted.
  InsertResult InsertInArray(Array* array, const Key& k,
                             IndexType* index_found) {
    if (__builtin_expect(array->capacity == 0, 0)) return ARRAY_FULL;
    const IndexType origin_index = ComputeHash(k) & (array->capacity - 1);
    Bucket* origin_bucket = MutableBucket(array, origin_index);
    IndexType free_index = kEnd;
    for (int i = 0; i < std::min<IndexType>(MaxAddDistance(), array->capacity);
         ++i) {
      const IndexType index = (origin_index + i) & (array->capacity - 1);
      Bucket* elem = MutableBucket(array, index);
      if (!elem->md.IsOccupied()) {
        free_index = index;
        break;
      }
    }
    if (free_index == kEnd) return ARRAY_FULL;

    do {
      int free_distance = Distance(*array, origin_index, free_index);
      if (free_distance < MaxHopDistance()) {
        Bucket* free_bucket = MutableBucket(array, free_index);
        origin_bucket->md.SetLeaf(free_distance);
        free_bucket->md.SetOrigin(free_distance);
        *index_found = free_index;
        return EMPTY_SLOT_FOUND;
      }
      free_index = FindCloserFreeBucket(array, free_index);
    } while (free_index != kEnd);
    return ARRAY_FULL;
  }

  static int Distance(const Array& array, int i0, int i1) {
    if (i1 >= i0) return i1 - i0;
    return (i1 - i0 + array.capacity);
  }

  IndexType FindCloserFreeBucket(Array* array, IndexType free_index) {
    Bucket* free_bucket = MutableBucket(array, free_index);

    for (int dist = MaxHopDistance() - 1; dist > 0; --dist) {
      IndexType moved_bucket_index =
          (free_index - dist) % (array->capacity - 1);
      Bucket* moved_elem = MutableBucket(array, moved_bucket_index);

      IndexType new_free_bucket_index = kEnd;
      int new_free_dist = -1;
      for (int i = 0; i < dist; ++i) {
        if (moved_elem->md.HasLeaf(i)) {
          new_free_dist = dist;
          break;
        }
      }
      if (new_free_dist >= 0) {
        IndexType new_free_bucket_index =
            (moved_bucket_index + new_free_dist) % (array->capacity - 1);
        Bucket* new_free_bucket = MutableBucket(array, new_free_bucket_index);
        moved_elem->md.SetLeaf(dist);
        moved_elem->md.ClearLeaf(new_free_dist);
        free_bucket->value = std::move(new_free_bucket->value);
        free_bucket->md.SetOrigin(dist);
        new_free_bucket->md.ClearOrigin();
        return new_free_bucket_index;
      }
    }
    return kEnd;
  }

  // Rehash the hash table. "delta" is the number of elements to add to the
  // current table. It's used to compute the capacity of the new table.  Culls
  // tombstones and move all the existing elements and
  void ExpandTable(IndexType delta) {
    const IndexType new_capacity = ComputeCapacity(array_.capacity + delta);
    Array new_array(new_capacity);
    for (IndexType i = 0; i < array_.capacity; ++i) {
      Bucket* old_bucket = MutableBucket(&array_, i);
      if (!old_bucket->md.IsOccupied()) continue;
      const Key& key = ExtractKey(old_bucket->value);

      IndexType new_i;
      InsertResult result = InsertInArray(&new_array, key, &new_i);
      assert(result == EMPTY_SLOT_FOUND);
      MutableBucket(&new_array, new_i)->value = std::move(old_bucket->value);
    }
    new_array.size = array_.size;
    array_ = std::move(new_array);
  }

  template <typename T0, typename T1>
  class CompressedPair : public T1 {
   public:
    T1& second() { return *this; }
    const T1& second() const { return *this; }
    T0 first;
  };

  const Key& ExtractKey(const Value& elem) const { return get_key_.Get(elem); }
  Key* ExtractMutableKey(Value* elem) const { return get_key_.Mutable(elem); }
  IndexType ComputeHash(const Key& key) const { return hash_(key); }
  bool KeysEqual(const Key& k0, const Key& k1) const {
    return equal_to_(k0, k1);
  }
  bool IsEmptyKey(const Key& k) const {
    return KeysEqual(options_.EmptyKey(), k);
  }

  Options options_;
  GetKey get_key_;
  Hash hash_;
  EqualTo equal_to_;
  Array array_;
};

template <typename Key, typename Value, int NumInlinedBuckets,
          typename Options = DefaultInlinedHashTableOptions,
          typename Hash = std::hash<Key>, typename EqualTo = std::equal_to<Key>,
          typename IndexType = size_t>
class InlinedHashMap {
 public:
  using BucketValue = std::pair<Key, Value>;
  using value_type = BucketValue;
  struct Bucket {
    InlinedHashTableBucketMetadata md;
    BucketValue value;
  };

  struct GetKey {
    const Key& Get(const BucketValue& elem) const { return elem.first; }
    Key* Mutable(BucketValue* elem) const { return &elem->first; }
  };
  using Table = InlinedHashTable<Bucket, Key, BucketValue, NumInlinedBuckets,
                                 Options, GetKey, Hash, EqualTo, IndexType>;
  using iterator = typename Table::iterator;
  using const_iterator = typename Table::const_iterator;

  InlinedHashMap() : impl_(0, Options(), Hash(), EqualTo()) {}
  InlinedHashMap(IndexType bucket_count, const Options& options = Options(),
                 const Hash& hash = Hash(), const EqualTo& equal_to = EqualTo())
      : impl_(bucket_count, options, hash, equal_to) {}

  bool empty() const { return impl_.empty(); }
  iterator begin() { return impl_.begin(); }
  iterator end() { return impl_.end(); }
  const_iterator cbegin() const { return impl_.cbegin(); }
  const_iterator cend() const { return impl_.cend(); }
  const_iterator begin() const { return impl_.cbegin(); }
  const_iterator end() const { return impl_.cend(); }
  IndexType size() const { return impl_.size(); }
  iterator find(const Key& k) { return impl_.find(k); }
  const_iterator find(const Key& k) const { return impl_.find(k); }

  std::pair<iterator, bool> insert(value_type&& value) {
    return impl_.insert(std::move(value));
  }
  iterator erase(iterator i) { return impl_.erase(i); }
  IndexType erase(const Key& k) { return impl_.erase(k); }
  void clear() { impl_.clear(); }
  Value& operator[](const Key& k) {
    IndexType index;
    typename Table::InsertResult result = impl_.Insert(k, &index);
    Bucket* bucket = impl_.Mutable(index);
    if (result != Table::KEY_FOUND) {
      // newly inserted. fill the key.
      bucket->value.first = k;
    }
    return bucket->value.second;
  }

  // Non-standard methods, mainly for testing.
  size_t capacity() const { return impl_.capacity(); }

  // For unittests only
  void CheckConsistency() { impl_.CheckConsistency(); }

 private:
  Table impl_;
};

template <typename Value, int NumInlinedBuckets,
          typename Options = DefaultInlinedHashTableOptions,
          typename Hash = std::hash<Value>,
          typename EqualTo = std::equal_to<Value>, typename IndexType = size_t>
class InlinedHashSet {
 public:
  struct Bucket {
    InlinedHashTableBucketMetadata md;
    Value value;
  };

  struct GetKey {
    const Value& Get(const Value& elem) const { return elem; }
    Value* Mutable(Value* elem) const { return elem; }
  };
  using Table = InlinedHashTable<Bucket, Value, Value, NumInlinedBuckets,
                                 Options, GetKey, Hash, EqualTo, IndexType>;
  using iterator = typename Table::iterator;
  using const_iterator = typename Table::const_iterator;

  InlinedHashSet() : impl_(0, Options(), Hash(), EqualTo()) {}
  InlinedHashSet(IndexType bucket_count, const Options& options = Options(),
                 const Hash& hash = Hash(), const EqualTo& equal_to = EqualTo())
      : impl_(bucket_count, options, hash, equal_to) {}
  bool empty() const { return impl_.empty(); }
  iterator begin() { return impl_.begin(); }
  iterator end() { return impl_.end(); }
  const_iterator cbegin() const { return impl_.cbegin(); }
  const_iterator cend() const { return impl_.cend(); }
  const_iterator begin() const { return impl_.cbegin(); }
  const_iterator end() const { return impl_.cend(); }
  IndexType size() const { return impl_.size(); }
  std::pair<iterator, bool> insert(Value&& value) {
    return impl_.insert(std::move(value));
  }
  std::pair<iterator, bool> insert(const Value& value) {
    return impl_.insert(value);
  }

  iterator find(const Value& k) { return impl_.find(k); }
  const_iterator find(const Value& k) const { return impl_.find(k); }
  void clear() { impl_.clear(); }
  iterator erase(iterator i) { return impl_.erase(i); }
  IndexType erase(const Value& k) { return impl_.erase(k); }

  // Non-standard methods, mainly for testing.
  size_t capacity() const { return impl_.capacity(); }

  // For unittests only
  void CheckConsistency() { impl_.CheckConsistency(); }

 private:
  Table impl_;
};
