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
//
// TODO: allow NumInlinedElements to be zero.
// TODO: implement bucket reservation.

template <typename Key, typename Elem, int NumInlinedElements, typename Options,
          typename GetKey, typename Hash, typename EqualTo, typename IndexType>
class InlinedHashTable {
 public:
  static_assert((NumInlinedElements & (NumInlinedElements - 1)) == 0,
                "NumInlinedElements must be a power of two");
  InlinedHashTable(IndexType bucket_count, const Options& options,
                   const Hash& hash, const EqualTo& equal_to)
      : options_(options),
        hash_(hash),
        equal_to_(equal_to),
        array_(ComputeCapacity(bucket_count)) {
    InitArray(options_.EmptyKey(), &array_);
  }

  InlinedHashTable(const InlinedHashTable& other) : array_(NumInlinedElements) {
    *this = other;
  }
  InlinedHashTable(InlinedHashTable&& other) : array_(NumInlinedElements) {
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

    Elem& operator*() const { return *table_->Mutable(index_); }
    Elem* operator->() const { return table_->Mutable(index_); }

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

    const Elem& operator*() const { return table_->Get(index_); }
    const Elem* operator->() const { return &table_->Get(index_); }

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
    for (Elem& elem : array_.inlined) {
      *ExtractMutableKey(&elem) = options_.EmptyKey();
    }
    if (array_.outlined != nullptr) {
      for (size_t i = 0; i < array_.capacity - array_.inlined.size(); ++i) {
        *ExtractMutableKey(&array_.outlined[i]) = options_.EmptyKey();
      }
    }
    array_.size = 0;
    array_.num_empty_slots = array_.num_empty_slots;
  }

  // Erases the element pointed to by "i". Returns the iterator to the next
  // valid element.
  iterator erase(iterator i) {
    Elem& elem = *i;
    *ExtractMutableKey(&elem) = options_.DeletedKey();
    --array_.size;
    return iterator(this, NextValidElementInArray(array_, i.index_ + 1));
  }

  // If "k" exists in the table, erase it and return 1. Else return 0.
  IndexType erase(const Key& k) {
    iterator i = find(k);
    if (i == end()) return 0;
    erase(i);
    return 1;
  }

  std::pair<iterator, bool> insert(Elem&& value) {
    IndexType index;
    InsertResult result = Insert(ExtractKey(value), &index);
    if (result == KEY_FOUND) {
      return std::make_pair(iterator(this, index), false);
    }
    *MutableArraySlot(&array_, index) = std::move(value);
    return std::make_pair(iterator(this, index), true);
  }

  std::pair<iterator, bool> insert(const Elem& value) {
    IndexType index;
    InsertResult result = Insert(ExtractKey(value), &index);
    if (result == KEY_FOUND) {
      return std::make_pair(iterator(this, index), false);
    }
    *MutableArraySlot(&array_, index) = value;
    return std::make_pair(iterator(this, index), true);
  }

  bool empty() const { return array_.size == 0; }
  IndexType size() const { return array_.size; }
  IndexType capacity() const { return array_.capacity; }

  // Backdoor methods used by map operator[].
  Elem* Mutable(IndexType index) { return MutableArraySlot(&array_, index); }
  const Elem& Get(IndexType index) const { return ArraySlot(array_, index); }

  enum InsertResult { KEY_FOUND, EMPTY_SLOT_FOUND, ARRAY_FULL };
  InsertResult Insert(const Key& key, IndexType* index) {
    InsertResult result = InsertInArray(&array_, key, index);
    if (result == KEY_FOUND) return result;
    if (result != ARRAY_FULL) {
      ++array_.size;
      return result;
    }
    ExpandTable(1);
    result = InsertInArray(&array_, key, index);
    assert(result == EMPTY_SLOT_FOUND);
    ++array_.size;
    return result;
  }

 private:
  static constexpr IndexType kEnd = std::numeric_limits<IndexType>::max();

  // Representation of the hash table.
  struct Array {
   public:
    explicit Array(IndexType capacity_arg)
        : size(0), capacity(capacity_arg), num_empty_slots(capacity) {
      assert((capacity & (capacity - 1)) == 0);
      if (capacity > inlined.size()) {
        outlined.reset(new Elem[capacity - inlined.size()]);
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
        outlined.reset(new Elem[n]);
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

    // First NumInlinedElements are stored in inlined. The rest are stored in
    // outlined.
    std::array<Elem, NumInlinedElements> inlined;
    std::unique_ptr<Elem[]> outlined;
    // # of filled slots.
    IndexType size;
    // Capacity of inlined + capacity of outlined. Always a power of two.
    IndexType capacity;
    // Number of empty slots, i.e., capacity - (# of filled slots + # of
    // tombstones).
    IndexType num_empty_slots;
  };

  static IndexType QuadraticProbe(const Array& array, IndexType current,
                                  int retries) {
    return (current + retries) & (array.capacity - 1);
  }

  IndexType ComputeCapacity(IndexType desired) {
    desired /= MaxLoadFactor();
    if (desired < NumInlinedElements) desired = NumInlinedElements;
    if (desired <= 0) return desired;
    return static_cast<IndexType>(1)
           << static_cast<int>(std::ceil(std::log2(desired)));
  }

  // Fill "array" with empty_key.
  void InitArray(const Key& empty_key, Array* array) const {
    for (Elem& elem : array->inlined) {
      *ExtractMutableKey(&elem) = empty_key;
    }
    if (array->outlined != nullptr) {
      IndexType n = array->capacity - array->inlined.size();
      for (IndexType i = 0; i < n; ++i) {
        *ExtractMutableKey(&array->outlined[i]) = empty_key;
      }
    }
  }

  // Return the index'th slot in array.
  static const Elem& ArraySlot(const Array& array, IndexType index) {
    if (index < NumInlinedElements) {
      return array.inlined[index];
    }
    return array.outlined[index - NumInlinedElements];
  }

  // Return the mutable pointer to the index'th slot in array.
  static Elem* MutableArraySlot(Array* array, IndexType index) {
    if (index < NumInlinedElements) {
      return &array->inlined[index];
    }
    return &array->outlined[index - NumInlinedElements];
  }

  // Find the first filled slot at or after "from". For incremenenting an
  // iterator.
  IndexType NextValidElementInArray(const Array& array, IndexType from) const {
    IndexType i = from;
    for (;;) {
      if (i >= array.capacity) {
        return kEnd;
      }
      const Key& k = ExtractKey(ArraySlot(array, i));
      if (!IsEmptyKey(k) && !IsDeletedKey(k)) {
        return i;
      }
      ++i;
    }
  }

  // Find "k" in the array. If found, set *index to the location of the key in
  // the array.
  bool FindInArray(const Array& array, const Key& k, IndexType* index) const {
    if (array.capacity == 0) return false;
    *index = ComputeHash(k) & (array.capacity - 1);
    for (int retries = 1;; ++retries) {
      const Elem& elem = ArraySlot(array, *index);
      const Key& key = ExtractKey(elem);
      if (KeysEqual(key, k)) {
        return true;
      }
      if (IsEmptyKey(key)) {
        return false;
      }
      if (retries > array.capacity) {
        return false;
      }
      *index = QuadraticProbe(array, *index, retries);
    }
  }

  // Either find "k" in the array, or find a slot into which "k" can be
  // inserted.
  InsertResult InsertInArray(Array* array, const Key& k, IndexType* index) {
    constexpr IndexType kInvalidIndex = std::numeric_limits<IndexType>::max();

    if (array->capacity == 0) return ARRAY_FULL;
    *index = ComputeHash(k) & (array->capacity - 1);
    const IndexType start_index = *index;
    bool found_empty_slot = false;
    IndexType empty_index = kInvalidIndex;
    for (int retries = 1;; ++retries) {
      const Elem& elem = ArraySlot(*array, *index);
      const Key& key = ExtractKey(elem);
      if (KeysEqual(key, k)) {
        return KEY_FOUND;
      }
      if (IsDeletedKey(key)) {
        if (empty_index == kInvalidIndex) empty_index = *index;
      } else if (IsEmptyKey(key)) {
        if (empty_index != kInvalidIndex) {
          // Found a deleted slot earlier. Take it.
          *index = empty_index;
          return EMPTY_SLOT_FOUND;
        }
        if (array->num_empty_slots >= array->capacity * (1 - MaxLoadFactor())) {
          --array->num_empty_slots;
          return EMPTY_SLOT_FOUND;
        }
        return ARRAY_FULL;
      }
      if (retries > array->capacity) {
        return ARRAY_FULL;
      }
      *index = QuadraticProbe(*array, *index, retries);
    }
  }
  // Rehash the hash table. "delta" is the number of elements to add to the
  // current table. It's used to compute the capacity of the new table.  Culls
  // tombstones and move all the existing elements and
  void ExpandTable(IndexType delta) {
    const IndexType new_capacity = ComputeCapacity(array_.size + delta);
    // std::cout << "Expand: " << new_capacity << "\n";
    Array new_array(new_capacity);
    InitArray(options_.EmptyKey(), &new_array);
    for (Elem& e : *this) {
      IndexType index;
      const Key& key = ExtractKey(e);
      if (IsEmptyKey(key) || IsDeletedKey(key)) continue;
      if (FindInArray(new_array, ExtractKey(e), &index)) {
        abort();
      }
      *MutableArraySlot(&new_array, index) = std::move(e);
      --new_array.num_empty_slots;
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

  const Key& ExtractKey(const Elem& elem) const { return get_key_.Get(elem); }
  Key* ExtractMutableKey(Elem* elem) const { return get_key_.Mutable(elem); }
  IndexType ComputeHash(const Key& key) const { return hash_(key); }
  bool KeysEqual(const Key& k0, const Key& k1) const {
    return equal_to_(k0, k1);
  }
  bool IsEmptyKey(const Key& k) const {
    return KeysEqual(options_.EmptyKey(), k);
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
    // return KeysEqual(options_.DeletedKey(), k);
    return SfinaeIsDeletedKey(&k, &options_, &equal_to_);
  }

  double MaxLoadFactor() const { return SfinaeMaxLoadFactor(&options_); }

  Options options_;
  GetKey get_key_;
  Hash hash_;
  EqualTo equal_to_;
  Array array_;
};

template <typename Key, typename Value, int NumInlinedElements,
          typename Options, typename Hash = std::hash<Key>,
          typename EqualTo = std::equal_to<Key>, typename IndexType = size_t>
class InlinedHashMap {
 public:
  using Elem = std::pair<Key, Value>;
  using value_type = Elem;
  struct GetKey {
    const Key& Get(const Elem& elem) const { return elem.first; }
    Key* Mutable(Elem* elem) const { return &elem->first; }
  };
  using Table = InlinedHashTable<Key, Elem, NumInlinedElements, Options, GetKey,
                                 Hash, EqualTo, IndexType>;
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

  std::pair<iterator, bool> insert(Elem&& value) {
    return impl_.insert(std::move(value));
  }
  iterator erase(iterator i) { return impl_.erase(i); }
  IndexType erase(const Key& k) { return impl_.erase(k); }
  void clear() { impl_.clear(); }
  Value& operator[](const Key& k) {
    IndexType index;
    typename Table::InsertResult result = impl_.Insert(k, &index);
    Elem* slot = impl_.Mutable(index);
    if (result != Table::KEY_FOUND) {
      // newly inserted. fill the key.
      slot->first = k;
    }
    return slot->second;
  }

  // Non-standard methods, mainly for testing.
  size_t capacity() const { return impl_.capacity(); }

 private:
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
  Table impl_;
};
