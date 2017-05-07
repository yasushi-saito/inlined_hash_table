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
// Caution: each method must return the same value across multiple invocations.
// Returning a compile-time constant allows the compiler to optimize the code
// well.
//
// TODO: implement bucket reservation.

class InlinedHashTableBucketMetadata {
 public:
  InlinedHashTableBucketMetadata() : mask_(0), origin_(0) {}

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

  bool HasLeaf(int index) const {
    assert(index >= 0 && index < kMaskBits);
    return (mask_ & (1U << index)) != 0;
  }

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
    assert(delta_from_origin < kMaskBits);
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
  static constexpr int kMaskBits = 27;
  // Lower 27 bits is the mask, the upper 5 bits is the origin.
  unsigned mask_ : kMaskBits;
  unsigned origin_ : 32 - kMaskBits;
};

template <typename T>
class __attribute((aligned(sizeof(T)))) InlinedHashTableManualConstructor {
 public:
  InlinedHashTableManualConstructor() {}

  T* Mutable() { return reinterpret_cast<T*>(buf_); }

  const T& Get() const { return *reinterpret_cast<const T*>(buf_); }

  template <typename... Arg>
  void New(Arg&&... values) {
    new (buf_) T(std::forward<T>(values)...);
  }

  template <typename... Arg>
  void New(const Arg&... values) {
    new (buf_) T(values...);
  }

  void New() { new (buf_) T(); }

  void Delete() { Mutable()->~T(); }

 private:
  InlinedHashTableManualConstructor(const InlinedHashTableManualConstructor&) =
      delete;
  void operator=(const InlinedHashTableManualConstructor&) = delete;
  uint8_t buf_[sizeof(T)];
};

template <typename Key, typename Value, int NumInlinedBuckets, typename GetKey,
          typename Hash, typename EqualTo, typename IndexType>
class InlinedHashTable {
 public:
  using BucketMetadata = InlinedHashTableBucketMetadata;
  struct Bucket {
    BucketMetadata md;
    InlinedHashTableManualConstructor<Value> value;

    Bucket() {}
    ~Bucket() {
      if (md.IsOccupied()) {
        value.Delete();
      }
    }

    Bucket(const Bucket& other) {
      md = other.md;
      if (md.IsOccupied()) {
        value.New(other.value.Get());
      }
    }

    Bucket(Bucket&& other) {
      md = other.md;
      other.md.ClearAll();
      if (md.IsOccupied()) {
        value.New(std::move(*other.value.Mutable()));
      }
    }

    Bucket& operator=(Bucket&& other) {
      if (md.IsOccupied()) {
        value.Delete();
      }
      md = other.md;
      other.md.ClearAll();
      if (md.IsOccupied()) {
        value.New(std::move(*other.value.Mutable()));
      }
      return *this;
    }

    Bucket& operator=(const Bucket& other) {
      if (md.IsOccupied()) {
        value.Delete();
      }
      md = other.md;
      if (md.IsOccupied()) {
        value.New(other.value.Get());
      }
      return *this;
    }
  };
  static_assert((NumInlinedBuckets & (NumInlinedBuckets - 1)) == 0,
                "NumInlinedBuckets must be a power of two");
  InlinedHashTable(IndexType bucket_count, const Hash& hash,
                   const EqualTo& equal_to)
      : hash_(hash),
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
    get_key_ = other.get_key_;
    hash_ = other.hash_;
    equal_to_ = other.equal_to_;
    return *this;
  }
  InlinedHashTable& operator=(InlinedHashTable&& other) {
    array_ = std::move(other.array_);
    get_key_ = std::move(other.get_key_);
    hash_ = std::move(other.hash_);
    equal_to_ = std::move(other.equal_to_);
    return *this;
  }

  class iterator {
   public:
    using Table = InlinedHashTable<Key, Value, NumInlinedBuckets, GetKey, Hash,
                                   EqualTo, IndexType>;
    iterator(Table* table, IndexType index) : table_(table), index_(index) {}
    iterator(const typename Table::iterator& i)
        : table_(i.table_), index_(i.index_) {}
    bool operator==(const iterator& other) const {
      return index_ == other.index_;
    }
    bool operator!=(const iterator& other) const {
      return index_ != other.index_;
    }

    Value& operator*() const {
      return *table_->MutableBucket(index_)->value.Mutable();
    }
    Value* operator->() const {
      return table_->MutableBucket(index_)->value.Mutable();
    }

    iterator operator++() {  // ++it
      index_ = table_->array_.NextValidElement(index_ + 1);
      return *this;
    }

    iterator operator++(int unused) {  // it++
      iterator r(*this);
      index_ = table_->array_.NextValidElement(index_ + 1);
      return r;
    }

   private:
    friend Table;
    Table* table_;
    IndexType index_;
  };

  class const_iterator {
   public:
    using Table = InlinedHashTable<Key, Value, NumInlinedBuckets, GetKey, Hash,
                                   EqualTo, IndexType>;
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

    const Value& operator*() const {
      return table_->GetBucket(index_).value.Get();
    }
    const Value* operator->() const {
      return &table_->GetBucket(index_).value.Get();
    }

    const_iterator operator++() {  // ++it
      index_ = table_->array_.NextValidElement(index_ + 1);
      return *this;
    }

    const_iterator operator++(int unused) {  // it++
      const_iterator r(*this);
      index_ = table_->array_.NextValidElement(index_ + 1);
      return r;
    }

   private:
    friend Table;
    const Table* table_;
    IndexType index_;
  };

  // TODO(saito) Support const_iterator, cbegin, etc.

  iterator begin() { return iterator(this, array_.NextValidElement(0)); }

  iterator end() { return iterator(nullptr, kEnd); }

  const_iterator cbegin() const {
    return const_iterator(this, NextValidElement(array_, 0));
  }
  const_iterator cend() const { return const_iterator(nullptr, kEnd); }
  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }

  iterator find(const Key& k) {
    IndexType index;
    if (FindInArray(array_, k, hash_(k), &index)) {
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
    for (Bucket& bucket : array_.inlined_) {
      if (bucket.md.IsOccupied()) {
        bucket.value.Delete();
      }
      bucket.md.ClearAll();
    }
    if (array_.outlined_ != nullptr) {
      for (size_t i = 0; i < array_.capacity() - array_.inlined_.size(); ++i) {
        Bucket* bucket = &array_.outlined_[i];
        if (bucket->md.IsOccupied()) {
          bucket->value.Delete();
        }
        bucket->md.ClearAll();
      }
    }
    array_.size_ = 0;
  }

  // Erases the element pointed to by "i". Returns the iterator to the next
  // valid element.
  iterator erase(iterator itr) {
    assert(itr.table_ == this);
    Bucket* bucket = array_.MutableBucket(itr.index_);
    assert(bucket->md.IsOccupied());
    const int delta = bucket->md.GetOrigin();
    bucket->md.ClearOrigin();
    bucket->value.Delete();
    Bucket* origin = array_.MutableBucket(array_.Clamp(itr.index_ - delta));
    origin->md.ClearLeaf(delta);
    --array_.size_;
    return iterator(this, array_.NextValidElement(itr.index_ + 1));
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
    array_.MutableBucket(index)->value.New(std::move(value));
    return std::make_pair(iterator(this, index), true);
  }

  std::pair<iterator, bool> insert(const Value& value) {
    IndexType index;
    InsertResult result = Insert(ExtractKey(value), &index);
    if (result == KEY_FOUND) {
      return std::make_pair(iterator(this, index), false);
    }
    array_.MutableBucket(index)->value.New(value);
    return std::make_pair(iterator(this, index), true);
  }

  bool empty() const { return array_.size_ == 0; }
  IndexType size() const { return array_.size_; }
  IndexType capacity() const { return array_.capacity(); }

  // Backdoor methods used by map operator[].
  Bucket* MutableBucket(IndexType index) { return array_.MutableBucket(index); }
  const Bucket& GetBucket(IndexType index) const {
    return array_.GetBucket(index);
  }

  enum InsertResult { KEY_FOUND, EMPTY_SLOT_FOUND, ARRAY_FULL };
  InsertResult Insert(const Key& key, IndexType* index) {
    const size_t hash = hash_(key);
    if (FindInArray(array_, key, hash, index)) {
      return KEY_FOUND;
    }
    for (int iter = 0; iter < 4; ++iter) {
      InsertResult result = InsertInArray(&array_, key, hash, index);
      if (result == KEY_FOUND) return result;
      if (result != ARRAY_FULL) {
        ++array_.size_;
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
  class Array {
   public:
    explicit Array(IndexType capacity_arg)
        : size_(0), capacity_mask_(capacity_arg - 1) {
      assert((capacity() & capacity_mask()) == 0);
      if (capacity() > inlined_.size()) {
        outlined_.reset(new Bucket[capacity() - inlined_.size()]);
      }
    }

    Array(const Array& other) { *this = other; }

    Array(Array&& other) { *this = std::move(other); }

    Array& operator=(const Array& other) {
      size_ = other.size_;
      capacity_mask_ = other.capacity_mask_;
      inlined_ = other.inlined_;
      if (other.outlined_ != nullptr) {
        const size_t n = other.capacity() - inlined_.size();
        outlined_.reset(new Bucket[n]);
        std::copy(&other.outlined_[0], &other.outlined_[n], &outlined_[0]);
      }
      return *this;
    }

    Array& operator=(Array&& other) {
      size_ = other.size_;
      capacity_mask_ = other.capacity_mask_;
      inlined_ = std::move(other.inlined_);
      outlined_ = std::move(other.outlined_);

      other.outlined_.reset();
      other.size_ = 0;
      other.capacity_mask_ = other.inlined_.size() - 1;
      for (Bucket& bucket : other.inlined_) {
        if (bucket.md.IsOccupied()) {
          bucket.value.Delete();
        }
        bucket.md.ClearAll();
      }
      return *this;
    }

    // Return the index'th slot in array.
    const Bucket& GetBucket(IndexType index) const {
      if (index < NumInlinedBuckets) {
        return inlined_[index];
      }
      return outlined_[index - NumInlinedBuckets];
    }

    // Return the mutable pointer to the index'th slot in array.
    Bucket* MutableBucket(IndexType index) {
      if (index < NumInlinedBuckets) {
        return &inlined_[index];
      }
      return &outlined_[index - NumInlinedBuckets];
    }

    IndexType Clamp(IndexType index) const { return index & capacity_mask_; }

    int Distance(int i0, int i1) const {
      if (i1 >= i0) return i1 - i0;
      return i1 - i0 + capacity();
    }

    // Find the first filled slot at or after "from". For incremenenting an
    // iterator.
    IndexType NextValidElement(IndexType from) const {
      IndexType i = from;
      for (;;) {
        if (i >= capacity()) {
          return kEnd;
        }
        const Bucket& bucket = GetBucket(i);
        if (bucket.md.IsOccupied()) {
          return i;
        }
        ++i;
      }
    }

    IndexType capacity_mask() const { return capacity_mask_; }
    IndexType capacity() const { return capacity_mask_ + 1; }
    IndexType size() const { return size_; }

    // First NumInlinedBuckets are stored in inlined. The rest are stored in
    // outlined.
    std::array<Bucket, NumInlinedBuckets> inlined_;
    std::unique_ptr<Bucket[]> outlined_;
    // # of filled slots.
    IndexType size_;
    // Capacity of inlined + capacity of outlined. Always a power of two.
    IndexType capacity_mask_;
  };

  // Find "k" in the array. If found, set *index to the location of the key in
  // the array.
  bool FindInArray(const Array& array, const Key& k, size_t hash,
                   IndexType* index) const {
    if (__builtin_expect(array.capacity() == 0, 0)) return false;

    const IndexType start_index = array.Clamp(hash);
    const BucketMetadata& md = array.GetBucket(start_index).md;
    BucketMetadata::LeafIterator it(&md);
    int distance;
    while ((distance = it.Next()) >= 0) {
      *index = array.Clamp(start_index + distance);
      const Bucket& elem = array.GetBucket(*index);
      if (equal_to_(k, ExtractKey(elem.value.Get()))) {
        return true;
      }
    }
    return false;
  }

  IndexType ComputeCapacity(IndexType desired) {
    if (desired < NumInlinedBuckets) desired = NumInlinedBuckets;
    if (desired <= 0) return desired;
    return static_cast<IndexType>(1)
           << static_cast<int>(std::ceil(std::log2(desired)));
  }

  static constexpr int MaxHopDistance() { return 27; }
  static constexpr int MaxAddDistance() { return 128; }

  // Either find "k" in the array, or find a slot into which "k" can be
  // inserted.
  InsertResult InsertInArray(Array* array, const Key& k, size_t hash,
                             IndexType* index_found) {
    if (__builtin_expect(array->capacity() == 0, 0)) return ARRAY_FULL;
    const IndexType origin_index = array->Clamp(hash);
    Bucket* origin_bucket = array->MutableBucket(origin_index);
    IndexType free_index = kEnd;
    for (int i = 0;
         i < std::min<IndexType>(MaxAddDistance(), array->capacity()); ++i) {
      const IndexType index = array->Clamp(origin_index + i);
      const Bucket& elem = array->GetBucket(index);
      if (!elem.md.IsOccupied()) {
        free_index = index;
        break;
      }
    }
    if (free_index == kEnd) return ARRAY_FULL;

    do {
      int free_distance = array->Distance(origin_index, free_index);
      if (free_distance < MaxHopDistance()) {
        Bucket* free_bucket = array->MutableBucket(free_index);
        origin_bucket->md.SetLeaf(free_distance);
        free_bucket->md.SetOrigin(free_distance);
        *index_found = free_index;
        return EMPTY_SLOT_FOUND;
      }
      free_index = FindCloserFreeBucket(array, free_index);
    } while (free_index != kEnd);
    return ARRAY_FULL;
  }

  // Try to move free_index closer to the origin by swapping it with another
  // bucket in the interval.
  //
  // REQUIRES: free_index is not occupied, and is MaxHopDistance or more from
  // the origin bucket you are trying to insert into.
  IndexType FindCloserFreeBucket(Array* array, IndexType free_index) {
    Bucket* free_bucket = array->MutableBucket(free_index);

    for (int dist = MaxHopDistance() - 1; dist > 0; --dist) {
      IndexType moved_bucket_index = array->Clamp(free_index - dist);
      Bucket* moved_bucket = array->MutableBucket(moved_bucket_index);

      // Find the first leaf of moved_bucket.
      int new_free_dist = -1;
      {
        BucketMetadata::LeafIterator it(&moved_bucket->md);
        new_free_dist = it.Next();
      }
      if (new_free_dist < 0 || new_free_dist >= dist) {
        // No leaf found before free_index.
        continue;
      }

      // Swap the new_free_bucket_index and free_index.
      IndexType new_free_bucket_index =
          array->Clamp(moved_bucket_index + new_free_dist);
      Bucket* new_free_bucket = array->MutableBucket(new_free_bucket_index);
      moved_bucket->md.SetLeaf(dist);
      moved_bucket->md.ClearLeaf(new_free_dist);
      free_bucket->value.New(std::move(*new_free_bucket->value.Mutable()));
      free_bucket->md.SetOrigin(dist);
      new_free_bucket->md.ClearOrigin();
      return new_free_bucket_index;
    }
    return kEnd;
  }

  // Rehash the hash table. "delta" is the number of elements to add to the
  // current table. It's used to compute the capacity of the new table.
  void ExpandTable(IndexType delta) {
    const IndexType new_capacity = ComputeCapacity(array_.capacity() + delta);
    Array new_array(new_capacity);
    for (IndexType i = 0; i < array_.capacity(); ++i) {
      Bucket* old_bucket = array_.MutableBucket(i);
      if (!old_bucket->md.IsOccupied()) continue;
      const Key& key = ExtractKey(old_bucket->value.Get());

      IndexType new_i;
      InsertResult result = InsertInArray(&new_array, key, hash_(key), &new_i);
      assert(result == EMPTY_SLOT_FOUND);
      new_array.MutableBucket(new_i)->value.New(
          std::move(*old_bucket->value.Mutable()));
    }
    new_array.size_ = array_.size_;
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

  GetKey get_key_;
  Hash hash_;
  EqualTo equal_to_;
  Array array_;
};

template <typename Key, typename Value, int NumInlinedBuckets,
          typename Hash = std::hash<Key>, typename EqualTo = std::equal_to<Key>,
          typename IndexType = size_t>
class InlinedHashMap {
 public:
  using BucketValue = std::pair<Key, Value>;
  using value_type = BucketValue;
  struct GetKey {
    const Key& Get(const BucketValue& elem) const { return elem.first; }
    Key* Mutable(BucketValue* elem) const { return &elem->first; }
  };
  using Table = InlinedHashTable<Key, BucketValue, NumInlinedBuckets, GetKey,
                                 Hash, EqualTo, IndexType>;
  using iterator = typename Table::iterator;
  using const_iterator = typename Table::const_iterator;

  InlinedHashMap() : impl_(0, Hash(), EqualTo()) {}
  InlinedHashMap(IndexType bucket_count, const Hash& hash = Hash(),
                 const EqualTo& equal_to = EqualTo())
      : impl_(bucket_count, hash, equal_to) {}

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
    typename Table::Bucket* bucket = impl_.MutableBucket(index);
    if (result != Table::KEY_FOUND) {
      bucket->value.New();
      // newly inserted. fill the key.
      bucket->value.Mutable()->first = k;
    }
    return bucket->value.Mutable()->second;
  }

  // Non-standard methods, mainly for testing.
  size_t capacity() const { return impl_.capacity(); }

  // For unittests only
  void CheckConsistency() { impl_.CheckConsistency(); }

 private:
  Table impl_;
};

template <typename Value, int NumInlinedBuckets,
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
  using Table = InlinedHashTable<Value, Value, NumInlinedBuckets, GetKey, Hash,
                                 EqualTo, IndexType>;
  using iterator = typename Table::iterator;
  using const_iterator = typename Table::const_iterator;

  InlinedHashSet() : impl_(0, Hash(), EqualTo()) {}
  InlinedHashSet(IndexType bucket_count, const Hash& hash = Hash(),
                 const EqualTo& equal_to = EqualTo())
      : impl_(bucket_count, hash, equal_to) {}
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
