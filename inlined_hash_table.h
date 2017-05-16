// Author: yasushi.saito@gmail.com

#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <type_traits>

// InlinedHashTable is an implementation detail that underlies InlinedHashMap
// and InlinedHashSet. It's not for public use.
//
// NumInlinedElements is the number of elements stored in-line with the table.
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
template <typename Key, typename Elem, int NumInlinedElements, typename Options,
          typename GetKey, typename Hash, typename EqualTo, typename IndexType>
class InlinedHashTable {
 public:
  static_assert((NumInlinedElements & (NumInlinedElements - 1)) == 0,
                "NumInlinedElements must be a power of two");
  InlinedHashTable(IndexType bucket_count, const Options& options,
                   const Hash& hash, const EqualTo& equal_to)
      : size_(0),
        capacity_mask_(ComputeCapacity(bucket_count) - 1),
        options_(options),
        hash_(hash),
        equal_to_(equal_to) {
    num_empty_slots_and_inlined_.t0() = capacity_mask_ + 1;
    assert((capacity & capacity_mask_) == 0);
    for (Elem& elem : inlined()) {
      *GetKey::Mutable(&elem) = options_.EmptyKey();
    }
    if (Capacity() > inlined().size()) {
      const IndexType n = Capacity() - inlined().size();
      outlined_.reset(new Elem[n]);
      for (IndexType i = 0; i < n; ++i) {
        *GetKey::Mutable(&outlined_[i]) = options_.EmptyKey();
      }
    }
  }

  InlinedHashTable(const InlinedHashTable& other) { *this = other; }
  InlinedHashTable(InlinedHashTable&& other) { *this = std::move(other); }

  InlinedHashTable& operator=(const InlinedHashTable& other) {
    size_ = other.size_;
    capacity_mask_ = other.capacity_mask_;
    options_ = other.options_;
    hash_ = other.hash_;
    num_empty_slots_and_inlined_ = other.num_empty_slots_and_inlined_;
    if (other.outlined_ != nullptr) {
      const size_t n = other.Capacity() - inlined().size();
      outlined_.reset(new Elem[n]);
      std::copy(&other.outlined_[0], &other.outlined_[n], &outlined_[0]);
    }
    return *this;
  }

  InlinedHashTable& operator=(InlinedHashTable&& other) {
    size_ = other.size_;
    capacity_mask_ = other.capacity_mask_;
    options_ = std::move(other.options_);
    hash_ = std::move(other.hash_);
    num_empty_slots_and_inlined_ =
        std::move(other.num_empty_slots_and_inlined_);
    outlined_ = std::move(other.outlined_);

    other.outlined_.reset();
    other.size_ = 0;
    other.capacity_mask_ = other.inlined().size() - 1;
    return *this;
  }

  void MoveFrom(InlinedHashTable&& other) {
    assert(size_ == 0);
    for (Elem& e : other) {
      IndexType index;
      const Key& key = GetKey::Get(e);
      if (IsEmptyKey(key) || IsDeletedKey(key)) continue;
      if (FindInArray(GetKey::Get(e), &index)) {
        abort();
      }
      *MutableElem(index) = std::move(e);
      --num_empty_slots();
    }
    size_ = other.size_;
  }

  class iterator {
   public:
    using Table = InlinedHashTable<Key, Elem, NumInlinedElements, Options,
                                   GetKey, Hash, EqualTo, IndexType>;
    iterator(Table* table, IndexType index) : table_(table), index_(index) {}
    iterator(const typename Table::iterator& i)
        : table_(i.table_), index_(i.index_) {}
    bool operator==(const iterator& other) const {
      return index_ == other.index_;
    }
    bool operator!=(const iterator& other) const {
      return index_ != other.index_;
    }

    Elem& operator*() const { return *table_->MutableElem(index_); }
    Elem* operator->() const { return table_->MutableElem(index_); }

    iterator operator++() {  // ++it
      index_ = table_->NextValidElementInArray(index_ + 1);
      return *this;
    }

    iterator operator++(int unused) {  // it++
      iterator r(*this);
      index_ = table_->NextValidElementInArray(index_ + 1);
      return r;
    }

   private:
    friend Table;
    Table* table_;
    IndexType index_;
  };

  class const_iterator {
   public:
    using Table = InlinedHashTable<Key, Elem, NumInlinedElements, Options,
                                   GetKey, Hash, EqualTo, IndexType>;
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

    const Elem& operator*() const { return table_->GetElem(index_); }
    const Elem* operator->() const { return &table_->GetElem(index_); }

    const_iterator operator++() {  // ++it
      index_ = table_->NextValidElementInArray(table_->array_, index_ + 1);
      return *this;
    }

    const_iterator operator++(int unused) {  // it++
      const_iterator r(*this);
      index_ = table_->NextValidElementInArray(index_ + 1);
      return r;
    }

   private:
    friend Table;
    const Table* table_;
    IndexType index_;
  };

  iterator begin() { return iterator(this, NextValidElementInArray(0)); }
  iterator end() { return iterator(this, kEnd); }
  const_iterator cbegin() const {
    return const_iterator(this, NextValidElementInArray(0));
  }
  const_iterator cend() const { return const_iterator(this, kEnd); }
  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }

  iterator find(const Key& k) {
    IndexType index;
    if (FindInArray(k, &index)) {
      return iterator(this, index);
    } else {
      return end();
    }
  }

  const_iterator find(const Key& k) const {
    IndexType index;
    if (FindInArray(k, &index)) {
      return const_iterator(this, index);
    } else {
      return cend();
    }
  }

  // Erases the element pointed to by "i". Returns the iterator to the next
  // valid element.
  iterator Erase(iterator i) {
    Elem& elem = *i;
    *GetKey::Mutable(&elem) = options_.DeletedKey();
    --size_;
    return iterator(this, NextValidElementInArray(i.index_ + 1));
  }

  // If "k" exists in the table, erase it and return 1. Else return 0.
  IndexType Erase(const Key& k) {
    iterator i = find(k);
    if (i == end()) return 0;
    Erase(i);
    return 1;
  }

  bool Empty() const { return size_ == 0; }
  IndexType Size() const { return size_; }
  IndexType Capacity() const { return capacity_mask_ + 1; }

  // Return the mutable pointer to the index'th slot in array.
  Elem* MutableElem(IndexType index) {
    if (index < NumInlinedElements) {
      return &inlined()[index];
    }
    return &outlined_[index - NumInlinedElements];
  }

  // Return the index'th slot in array.
  const Elem& GetElem(IndexType index) const {
    if (index < NumInlinedElements) {
      return inlined()[index];
    }
    return outlined_[index - NumInlinedElements];
  }

  enum InsertResult { KEY_FOUND, EMPTY_SLOT_FOUND, ARRAY_FULL };

  // Either find "k" in the array, or find a slot into which "k" can be
  // inserted.
  InsertResult Insert(const Key& k, IndexType* index) {
    constexpr IndexType kInvalidIndex = std::numeric_limits<IndexType>::max();

    if (Capacity() == 0) return ARRAY_FULL;
    *index = Clamp(hash_(k));
    const IndexType start_index = *index;
    bool found_empty_slot = false;
    IndexType empty_index = kInvalidIndex;
    for (int retries = 1;; ++retries) {
      const Elem& elem = GetElem(*index);
      const Key& key = GetKey::Get(elem);
      if (empty_index == kInvalidIndex && IsDeletedKey(key)) {
        // Remember the first tombstone, in case we need to insert here.
        empty_index = *index;
      } else if (IsEmptyKey(key)) {
        if (empty_index != kInvalidIndex) {
          // Found a tombstone earlier. Take it.
          *index = empty_index;
          ++size_;
          return EMPTY_SLOT_FOUND;
        }
        if (num_empty_slots() >= Capacity() * (1 - MaxLoadFactor())) {
          --num_empty_slots();
          ++size_;
          return EMPTY_SLOT_FOUND;
        }
        return ARRAY_FULL;
      } else if (equal_to()(key, k)) {
        return KEY_FOUND;
      }
      if (retries > Capacity()) {
        return ARRAY_FULL;
      }
      *index = QuadraticProbe(*index, retries);
    }
  }

  void Clear() {
    for (Elem& elem : inlined()) {
      *GetKey::Mutable(&elem) = options_.EmptyKey();
    }
    if (outlined_ != nullptr) {
      for (size_t i = 0; i < Capacity() - inlined().size(); ++i) {
        *GetKey::Mutable(&outlined_[i]) = options_.EmptyKey();
      }
    }
    size_ = 0;
    num_empty_slots() = 0;
  }

  const Options& options() const { return options_; }
  const Hash& hash() const { return hash_; }
  const EqualTo& equal_to() const { return equal_to_; }

  IndexType ComputeCapacity(IndexType desired) {
    if (desired == 1 && NumInlinedElements == 0) {
      // When the user doesn't specify the initial table size, use the same
      // default as the dense_hash_map.
      return 32;
    }
    desired /= MaxLoadFactor();
    if (desired < NumInlinedElements) desired = NumInlinedElements;
    if (desired <= 0) return desired;
    return static_cast<IndexType>(1)
           << static_cast<int>(std::ceil(std::log2(desired)));
  }

 private:
  static constexpr IndexType kEnd = std::numeric_limits<IndexType>::max();

  IndexType QuadraticProbe(IndexType current, int retries) const {
    return Clamp((current + retries));
  }

  // Find the first filled slot at or after "from". For incremenenting an
  // iterator.
  IndexType NextValidElementInArray(IndexType from) const {
    IndexType i = from;
    for (;;) {
      if (i >= Capacity()) {
        return kEnd;
      }
      const Key& k = GetKey::Get(GetElem(i));
      if (!IsEmptyKey(k) && !IsDeletedKey(k)) {
        return i;
      }
      ++i;
    }
  }

  // Find "k" in the array. If found, set *index to the location of the key in
  // the array.
  bool FindInArray(const Key& k, IndexType* index) const {
    if (Capacity() == 0) return false;
    *index = Clamp(hash_(k));
    for (int retries = 1;; ++retries) {
      const Elem& elem = GetElem(*index);
      const Key& key = GetKey::Get(elem);
      if (equal_to()(key, k)) {
        return true;
      }
      if (IsEmptyKey(key)) {
        return false;
      }
      if (retries > Capacity()) {
        return false;
      }
      *index = QuadraticProbe(*index, retries);
    }
  }

  // Simpler implementation of boost::compressed_pair.
  template <typename T0, typename T1, bool T1Empty>
  class CompressedPairImpl {
   public:
    T0& t0() { return t0_; }
    const T0& t0() const { return t0_; }
    T1& t1() { return t1_; }
    const T1& t1() const { return t1_; }

    T0 t0_;
    T1 t1_;
  };

  template <typename T0, typename T1>
  class CompressedPairImpl<T0, T1, true> {
   public:
    T0& t0() { return t0_; }
    const T0& t0() const { return t0_; }

    T1& t1() { return reinterpret_cast<T1&>(t0_); }
    const T1& t1() const { return reinterpret_cast<const T1&>(t0_); }

    T0 t0_;
  };

  bool IsEmptyKey(const Key& k) const {
    return equal_to()(options_.EmptyKey(), k);
  }

  template <typename TOptions>
  static auto SfinaeIsDeletedKey(const Key* k, const TOptions* options,
                                 const EqualTo* equal_to)
      -> decltype((*equal_to)(options->DeletedKey(), *k)) {
    return (*equal_to)(options->DeletedKey(), *k);
  }

  static auto SfinaeIsDeletedKey(...) -> bool { return false; }

  template <typename TOptions>
  static auto SfinaeMaxLoadFactor(const TOptions* options)
      -> decltype(options->MaxLoadFactor()) {
    return options->MaxLoadFactor();
  }

  static auto SfinaeMaxLoadFactor(...) -> double { return 0.5; }

  bool IsDeletedKey(const Key& k) const {
    return SfinaeIsDeletedKey(&k, &options_, &equal_to_);
  }

  double MaxLoadFactor() const { return SfinaeMaxLoadFactor(&options_); }

  IndexType Clamp(IndexType v) const { return v & capacity_mask_; }

  using InlinedArray = std::array<Elem, NumInlinedElements>;

  // # of filled slots.
  IndexType size_;
  // Capacity-1 of inlined + capacity of outlined. Always a power of two.
  IndexType capacity_mask_;
  // First NumInlinedElements are stored in inlined. The rest are stored in
  // outlined.
  CompressedPairImpl<IndexType, InlinedArray, NumInlinedElements == 0>
      num_empty_slots_and_inlined_;

  Options options_;
  Hash hash_;
  EqualTo equal_to_;
  std::unique_ptr<Elem[]> outlined_;

  const InlinedArray& inlined() const {
    return num_empty_slots_and_inlined_.t1();
  }
  InlinedArray& inlined() { return num_empty_slots_and_inlined_.t1(); }

  // Number of empty slots, i.e., capacity - (# of filled slots + # of
  // tombstones).
  IndexType num_empty_slots() const {
    return num_empty_slots_and_inlined_.t0();
  }
  IndexType& num_empty_slots() { return num_empty_slots_and_inlined_.t0(); }
};

template <typename Key, typename Value, int NumInlinedElements,
          typename Options, typename Hash = std::hash<Key>,
          typename EqualTo = std::equal_to<Key>, typename IndexType = size_t>
class InlinedHashMap {
 public:
  using Elem = std::pair<Key, Value>;
  using value_type = Elem;
  struct GetKey {
    static const Key& Get(const Elem& elem) { return elem.first; }
    static Key* Mutable(Elem* elem) { return &elem->first; }
  };
  using Table = InlinedHashTable<Key, Elem, NumInlinedElements, Options, GetKey,
                                 Hash, EqualTo, IndexType>;
  using iterator = typename Table::iterator;
  using const_iterator = typename Table::const_iterator;

  InlinedHashMap() : impl_(0, Options(), Hash(), EqualTo()) {}
  InlinedHashMap(IndexType bucket_count, const Options& options = Options(),
                 const Hash& hash = Hash(), const EqualTo& equal_to = EqualTo())
      : impl_(bucket_count, options, hash, equal_to) {}

  bool empty() const { return impl_.Empty(); }
  iterator begin() { return impl_.begin(); }
  iterator end() { return impl_.end(); }
  const_iterator cbegin() const { return impl_.cbegin(); }
  const_iterator cend() const { return impl_.cend(); }
  const_iterator begin() const { return impl_.cbegin(); }
  const_iterator end() const { return impl_.cend(); }
  IndexType size() const { return impl_.Size(); }
  iterator find(const Key& k) { return impl_.find(k); }
  const_iterator find(const Key& k) const { return impl_.find(k); }

  std::pair<iterator, bool> insert(Elem&& value) {
    IndexType index;
    typename Table::InsertResult result = Insert(value.first, &index);
    Elem* slot = impl_.MutableElem(index);
    if (result != Table::KEY_FOUND) {
      // newly inserted. fill the key.
      *slot = std::move(value);
      return std::make_pair(typename Table::iterator(&impl_, index), true);
    }
    return std::make_pair(typename Table::iterator(&impl_, index), false);
  }

  iterator erase(iterator i) { return impl_.erase(i); }
  IndexType erase(const Key& k) { return impl_.Erase(k); }
  void clear() { impl_.Clear(); }
  Value& operator[](const Key& k) {
    IndexType index;
    typename Table::InsertResult result = Insert(k, &index);
    Elem* slot = impl_.MutableElem(index);
    if (result != Table::KEY_FOUND) {
      // newly inserted. fill the key.
      slot->first = k;
    }
    return slot->second;
  }

  // Non-standard methods, mainly for testing.
  size_t capacity() const { return impl_.Capacity(); }

 private:
  // Rehash the hash table. "delta" is the number of elements to add to the
  // current table. It's used to compute the capacity of the new table.  Culls
  // tombstones and move all the existing elements and
  typename Table::InsertResult Insert(const Key& key, IndexType* index) {
    typename Table::InsertResult result = impl_.Insert(key, index);
    if (result == Table::KEY_FOUND) return result;
    if (result != Table::ARRAY_FULL) {
      return result;
    }

    const IndexType new_capacity = impl_.ComputeCapacity(size() + 1);
    Table new_impl(new_capacity, impl_.options(), impl_.hash(),
                   impl_.equal_to());
    new_impl.MoveFrom(std::move(impl_));
    impl_ = std::move(new_impl);
    result = Insert(key, index);
    assert(result == EMPTY_SLOT_FOUND);
    return result;
  }

  Table impl_;
};

template <typename Elem, int NumInlinedElements, typename Options,
          typename Hash = std::hash<Elem>,
          typename EqualTo = std::equal_to<Elem>, typename IndexType = size_t>
class InlinedHashSet {
 public:
  struct GetKey {
    const Elem& Get(const Elem& elem) const { return elem; }
    Elem* Mutable(Elem* elem) const { return elem; }
  };
  using Table = InlinedHashTable<Elem, Elem, NumInlinedElements, Options,
                                 GetKey, Hash, EqualTo, IndexType>;
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
  std::pair<iterator, bool> insert(Elem&& value) {
    return impl_.insert(std::move(value));
  }
  std::pair<iterator, bool> insert(const Elem& value) {
    return impl_.insert(value);
  }

  iterator find(const Elem& k) { return impl_.find(k); }
  const_iterator find(const Elem& k) const { return impl_.find(k); }
  void clear() { impl_.clear(); }
  iterator erase(iterator i) { return impl_.erase(i); }
  IndexType erase(const Elem& k) { return impl_.erase(k); }

  // Non-standard methods, mainly for testing.
  size_t capacity() const { return impl_.capacity(); }

 private:
  // Rehash the hash table. "delta" is the number of elements to add to the
  // current table. It's used to compute the capacity of the new table.  Culls
  // tombstones and move all the existing elements and
  typename Table::InsertResult Insert(const Elem& elem,
                                      typename Table::IndexType* index) {
    typename Table::InsertResult result = impl_.InsertInArray(elem, index);
    if (result == Table::KEY_FOUND) return result;
    if (result != Table::ARRAY_FULL) {
      return result;
    }

    const IndexType new_capacity = impl_.ComputeCapacity(size() + 1);
    Table new_impl(new_capacity, impl_.options(), impl_.hash(),
                   impl_.equal_to());
    new_impl.MoveFrom(impl_);
    impl_ = std::move(new_impl);
    result = InsertInArray(elem, index);
    assert(result == EMPTY_SLOT_FOUND);
    return result;
  }
  Table impl_;
};
