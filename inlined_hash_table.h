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
// and InlinedHashSet. Not for public use.
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
// erase(). These keys must be different from any key that may be passed in
// insert or lookup.
// Caution: each method must return the same value across multiple invocations.
// Returning a compile-time constant allows the compiler to optimize the code
// well.
//
// MaxLoadFactor() defines when the hash table is expanded. The default value is
// 0.5, meaning that if the number of non-empty slots in the table exceeds 50%
// of the capacity, the hash table is doubled. The valid range of
// MaxLoadFactor() is (0,1].
//
// Parameters Hash and EqualTo are the functors used by
// std::unordered_{map,set}.
//
// IndexType is used to index the bucket array. The default is size_t, but if
// you can guarantee the table size doesn't exceed 2³² you can use uint32_t to
// save memory.
template <typename Key, typename Elem, int NumInlinedElements, typename Options,
          typename GetKey, typename Hash, typename EqualTo, typename IndexType>
class InlinedHashTable {
 public:
  static_assert((NumInlinedElements & (NumInlinedElements - 1)) == 0,
                "NumInlinedElements must be a power of two");
  InlinedHashTable(IndexType bucket_count, const Options& options,
                   const Hash& hash, const EqualTo& equal_to)
      : size_(0), options_(options), hash_(hash), equal_to_(equal_to) {
    const IndexType capacity = ComputeCapacity(bucket_count);
    capacity_mask_ = capacity - 1;
    assert((capacity & capacity_mask_) == 0);
    num_free_slots_and_inlined_.t0() = capacity * MaxLoadFactor();
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
    num_free_slots_and_inlined_ = other.num_free_slots_and_inlined_;
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
    num_free_slots_and_inlined_ = std::move(other.num_free_slots_and_inlined_);
    outlined_ = std::move(other.outlined_);

    other.outlined_.reset();
    other.size_ = 0;
    other.capacity_mask_ = other.inlined().size() - 1;
    other.num_free_slots() = other.Capacity() * MaxLoadFactor();
    return *this;
  }

  // Move the contents of "other" over to this table.  "other" will be in an
  // unspecified state after the call, and the only safe operation is to destroy
  // it.
  void MoveFrom(InlinedHashTable&& other) {
    assert(size_ == 0);
    for (Elem& e : other) {
      IndexType index;
      const Key& key = GetKey::Get(e);
      if (IsEmptyKey(key) || IsDeletedKey(key)) continue;
      if (Find(key, hash_(key), &index)) {
        abort();
      }
      *MutableElem(index) = std::move(e);
      --num_free_slots();
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
      index_ = table_->NextValidElement(index_ + 1);
      return *this;
    }

    iterator operator++(int unused) {  // it++
      iterator r(*this);
      index_ = table_->NextValidElement(index_ + 1);
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
      index_ = table_->NextValidElement(table_->array_, index_ + 1);
      return *this;
    }

    const_iterator operator++(int unused) {  // it++
      const_iterator r(*this);
      index_ = table_->NextValidElement(index_ + 1);
      return r;
    }

   private:
    friend Table;
    const Table* table_;
    IndexType index_;
  };

  iterator begin() { return iterator(this, NextValidElement(0)); }
  iterator end() { return iterator(this, kEnd); }
  const_iterator cbegin() const {
    return const_iterator(this, NextValidElement(0));
  }
  const_iterator cend() const { return const_iterator(this, kEnd); }
  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }

  iterator find(const Key& k) {
    IndexType index;
    if (Find(k, hash_(k), &index)) {
      return iterator(this, index);
    } else {
      return end();
    }
  }

  const_iterator find(const Key& k) const {
    IndexType index;
    if (Find(k, hash_(k), &index)) {
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
    return iterator(this, NextValidElement(i.index_ + 1));
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

  // Find "k" in the array. If found, set *index to the location of the key in
  // the array.
  bool Find(const Key& k, size_t hash, IndexType* index) const {
    if (Capacity() == 0) return false;
    *index = Clamp(hash);
    for (int retries = 1;; ++retries) {
      const Elem& elem = GetElem(*index);
      const Key& key = GetKey::Get(elem);
      if (equal_to_(key, k)) {
        return true;
      } else if (IsEmptyKey(key)) {
        return false;
      }
      if (retries > Capacity()) {
        return false;
      }
      *index = Probe(*index, retries);
    }
  }

  enum InsertResult { KEY_FOUND, EMPTY_SLOT_FOUND, ARRAY_FULL };

  // Either find "k" in the array, or find a slot into which "k" can be
  // inserted.
  InsertResult Insert(const Key& k, size_t hash, IndexType* index) {
    constexpr IndexType kInvalidIndex = std::numeric_limits<IndexType>::max();
    if (Capacity() == 0) return ARRAY_FULL;
    *index = Clamp(hash);
    IndexType empty_index = kInvalidIndex;
    for (int retries = 1;; ++retries) {
      const Elem& elem = GetElem(*index);
      const Key& key = GetKey::Get(elem);
      if (equal_to_(key, k)) {
        return KEY_FOUND;
      } else if (IsEmptyKey(key)) {
        if (empty_index != kInvalidIndex) {
          // Found a tombstone earlier. Take it.
          *index = empty_index;
          ++size_;
          return EMPTY_SLOT_FOUND;
        }
        if (num_free_slots() > 0) {
          --num_free_slots();
          ++size_;
          return EMPTY_SLOT_FOUND;
        }
        return ARRAY_FULL;
      } else if (empty_index == kInvalidIndex && IsDeletedKey(key)) {
        // Remember the first tombstone, in case we need to insert here.
        empty_index = *index;
      }
      if (retries > Capacity()) {
        return ARRAY_FULL;
      }
      *index = Probe(*index, retries);
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
    num_free_slots() = Capacity();
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
  using InlinedArray = std::array<Elem, NumInlinedElements>;
  static constexpr IndexType kEnd = std::numeric_limits<IndexType>::max();

  // Compute the next bucket index to probe on collision.
  IndexType Probe(IndexType current, int retries) const {
    return Clamp((current + retries));
  }

  // Find the first filled slot at or after "from". For incremenenting an
  // iterator.
  IndexType NextValidElement(IndexType from) const {
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
    return equal_to_(options_.EmptyKey(), k);
  }

  template <typename TOptions>
  static auto SfinaeIsDeletedKey(const Key* k, const TOptions* options,
                                 const EqualTo* equal_to)
      -> decltype((*equal_to)(options->DeletedKey(), *k)) {
    return (*equal_to)(options->DeletedKey(), *k);
  }

  static auto SfinaeIsDeletedKey(...) -> bool { return false; }

  // A template hack to call Options::MaxLoadFactor only when it's defined.
  template <typename TOptions>
  static auto SfinaeMaxLoadFactor(const TOptions* options)
      -> decltype(options->MaxLoadFactor()) {
    return options->MaxLoadFactor();
  }
  static auto SfinaeMaxLoadFactor(...) -> double { return 0.5; }
  bool IsDeletedKey(const Key& k) const {
    return SfinaeIsDeletedKey(&k, &options_, &equal_to_);
  }
  // Returns the value of Options::MaxLoadFactor(), or 0.5 if it's not defined.
  double MaxLoadFactor() const { return SfinaeMaxLoadFactor(&options_); }

  // Clamp the "v" in the array bucket  index range.
  IndexType Clamp(IndexType v) const { return v & capacity_mask_; }

  // # of filled slots.
  IndexType size_;
  // Capacity-1 of inlined + capacity of outlined. Always a power of two.
  IndexType capacity_mask_;

  // Combo of num_free_slots and inlined. num_free_slots is the # of remaining
  // free (empty) slots that can be claimed by insert(). inlined_ is the list of
  // elements stored in line with this table.
  CompressedPairImpl<IndexType, InlinedArray, NumInlinedElements == 0>
      num_free_slots_and_inlined_;

  Options options_;
  Hash hash_;
  EqualTo equal_to_;
  std::unique_ptr<Elem[]> outlined_;

  const InlinedArray& inlined() const {
    return num_free_slots_and_inlined_.t1();
  }
  InlinedArray& inlined() { return num_free_slots_and_inlined_.t1(); }

  // Number of free slots that can be claimed by insert(). It's initialize to
  // capacity * MaxLoadFactor, and is decremented every time a slot is taken by
  // insert().
  IndexType num_free_slots() const { return num_free_slots_and_inlined_.t0(); }
  IndexType& num_free_slots() { return num_free_slots_and_inlined_.t0(); }
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
  typename Table::InsertResult Insert(const Key& key, IndexType* index) {
    const size_t hash = impl_.hash()(key);
    typename Table::InsertResult result = impl_.Insert(key, hash, index);
    if (result != Table::ARRAY_FULL) return result;

    const IndexType new_capacity = impl_.ComputeCapacity(size() + 1);
    Table new_impl(new_capacity, impl_.options(), impl_.hash(),
                   impl_.equal_to());
    new_impl.MoveFrom(std::move(impl_));
    impl_ = std::move(new_impl);
    result = impl_.Insert(key, hash, index);
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
    static const Elem& Get(const Elem& elem) { return elem; }
    static Elem* Mutable(Elem* elem) { return elem; }
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
  IndexType size() const { return impl_.Size(); }
  std::pair<iterator, bool> insert(Elem&& value) {
    IndexType index;
    typename Table::InsertResult result = Insert(value, &index);
    Elem* slot = impl_.MutableElem(index);
    if (result != Table::KEY_FOUND) {
      // newly inserted. fill the key.
      *slot = std::move(value);
      return std::make_pair(typename Table::iterator(&impl_, index), true);
    }
    return std::make_pair(typename Table::iterator(&impl_, index), false);
  }

  iterator find(const Elem& k) { return impl_.find(k); }
  const_iterator find(const Elem& k) const { return impl_.Find(k); }
  void clear() { impl_.Clear(); }
  iterator erase(iterator i) { return impl_.Erase(i); }
  IndexType erase(const Elem& k) { return impl_.Erase(k); }

  // Non-standard methods, mainly for testing.
  size_t capacity() const { return impl_.capacity(); }

 private:
  typename Table::InsertResult Insert(const Elem& elem, IndexType* index) {
    const size_t hash = impl_.hash()(elem);
    typename Table::InsertResult result = impl_.Insert(elem, hash, index);
    if (result != Table::ARRAY_FULL) return result;

    const IndexType new_capacity = impl_.ComputeCapacity(size() + 1);
    Table new_impl(new_capacity, impl_.options(), impl_.hash(),
                   impl_.equal_to());
    new_impl.MoveFrom(std::move(impl_));
    impl_ = std::move(new_impl);
    result = impl_.Insert(elem, hash, index);
    assert(result == EMPTY_SLOT_FOUND);
    return result;
  }
  Table impl_;
};
