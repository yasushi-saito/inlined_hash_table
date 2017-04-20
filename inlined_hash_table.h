// Author: yasushi.saito@gmail.com

#pragma once

#include <array>
#include <cassert>
#include <functional>
#include <memory>

// TODO: const iterators
// TODO: size reservation
// TODO: move constructors
// TODO: Allow overriding the size type (size_t) to compress the table overhead.

// InlinedHashTable is an implementation detail that underlies InlinedHashMap
// and InlinedHashSet. It's not for public use.
template <typename Key, typename Elem, int NumInlinedElements, typename GetKey,
          typename Hash, typename EqualTo>
class InlinedHashTable {
 public:
  static_assert((NumInlinedElements & (NumInlinedElements - 1)) == 0,
                "NumInlinedElements must be a power of two");
  InlinedHashTable() : array_(NumInlinedElements) {
    set_has_empty_key(false);
    set_has_deleted_key(false);
  }

  // set_empty_key MUST be called before calling any other method.
  void set_empty_key(const Key& k) {
    assert(!has_empty_key());
    set_has_empty_key(true);
    p2_.first = k;
    InitArray(empty_key(), &array_);
  }

  // set_deleted_key MUST be called if you plan to use erase(). Note that if you
  // just use clear(), calling set_deleted_key is unnecessary.
  void set_deleted_key(const Key& k) {
    assert(!has_deleted_key());
    set_has_deleted_key(true);
    deleted_key_ = k;
  }

  class iterator {
   public:
    using Table =
        InlinedHashTable<Key, Elem, NumInlinedElements, GetKey, Hash, EqualTo>;
    iterator(Table* table, size_t index) : table_(table), index_(index) {}

    bool operator==(const iterator& other) const {
      return index_ == other.index_;
    }
    bool operator!=(const iterator& other) const {
      return index_ != other.index_;
    }

    Elem& operator*() const { return *table_->Mutable(index_); }

    // TODO(saito) support both pre and post increment ops.
    iterator operator++() {
      index_ = table_->NextValidElementInArray(table_->array_, index_ + 1);
      return *this;
    }

   private:
    friend Table;
    Table* table_;
    size_t index_;
  };

  // TODO(saito) Support const_iterator, cbegin, etc.

  iterator begin() {
    return iterator(this, NextValidElementInArray(array_, 0));
  }

  iterator end() { return iterator(this, kEnd); }

  iterator find(const Key& k) {
    size_t index;
    if (FindInArray(array_, k, &index)) {
      return iterator(this, index);
    } else {
      return end();
    }
  }

  // Erases the element pointed to by "i". Returns the iterator to the next
  // valid element.
  iterator erase(iterator i) {
    assert(has_deleted_key());
    Elem& elem = *i;
    *ExtractMutableKey(&elem) = deleted_key_;
    --array_.size;
    return iterator(this, NextValidElementInArray(array_, i.index_ + 1));
  }

  // If "k" exists in the table, erase it and return 1. Else return 0.
  size_t erase(const Key& k) {
    iterator i = find(k);
    if (i == end()) return 0;
    erase(i);
    return 1;
  }

  std::pair<iterator, bool> insert(Elem&& value) {
    size_t index;
    InsertResult result = Insert(ExtractKey(value), &index);
    if (result == KEY_FOUND) {
      return std::make_pair(iterator(this, index), false);
    }
    *MutableArraySlot(&array_, index) = std::move(value);
    return std::make_pair(iterator(this, index), true);
  }

  std::pair<iterator, bool> insert(const Elem& value) {
    size_t index;
    InsertResult result = Insert(ExtractKey(value), &index);
    if (result == KEY_FOUND) {
      return std::make_pair(iterator(this, index), false);
    }
    *MutableArraySlot(&array_, index) = value;
    return std::make_pair(iterator(this, index), true);
  }

  bool empty() const { return array_.size == 0; }
  size_t size() const { return array_.size; }
  size_t capacity() const { return array_.capacity; }

  // Backdoor methods used by map operator[].
  Elem* Mutable(size_t index) { return MutableArraySlot(&array_, index); }
  enum InsertResult { KEY_FOUND, EMPTY_SLOT_FOUND, ARRAY_FULL };
  InsertResult Insert(const Key& key, size_t* index) {
    InsertResult result = InsertInArray(&array_, key, index);
    if (result == KEY_FOUND) return result;
    ++array_.size;
    if (result != ARRAY_FULL) return result;
    ExpandTable();
    return InsertInArray(&array_, key, index);
  }

 private:
  static constexpr size_t kEnd = std::numeric_limits<size_t>::max();
  static constexpr double kMaxLoadFactor = 0.75;

  // Representation of the hash table.
  struct Array {
   public:
    explicit Array(size_t capacity_arg)
        : size(0), capacity(capacity_arg), num_empty_slots(capacity) {
      assert((capacity & (capacity - 1)) == 0);
      if (capacity > inlined.size()) {
        outlined.reset(new Elem[capacity - inlined.size()]);
      }
    }
    // First NumInlinedElements are stored in inlined. The rest are stored in
    // outlined.
    std::array<Elem, NumInlinedElements> inlined;
    std::unique_ptr<Elem[]> outlined;
    // # of filled slots.
    size_t size;
    // Capacity of inlined + capacity of outlined. Always a power of two.
    size_t capacity;
    // Number of empty slots, i.e., capacity - (# of filled slots + # of
    // tombstones).
    size_t num_empty_slots;
  };

  static size_t QuadraticProbe(const Array& array, size_t current,
                               int retries) {
    return (current + retries) & (array.capacity - 1);
  }

  // Fill "array" with empty_key.
  void InitArray(const Key& empty_key, Array* array) const {
    for (Elem& elem : array->inlined) {
      *ExtractMutableKey(&elem) = empty_key;
    }
    if (array->outlined != nullptr) {
      size_t n = array->capacity - array->inlined.size();
      for (size_t i = 0; i < n; ++i) {
        *ExtractMutableKey(&array->outlined[i]) = empty_key;
      }
    }
  }

  // Return the index'th slot in array.
  static const Elem& ArraySlot(const Array& array, size_t index) {
    if (index < NumInlinedElements) {
      return array.inlined[index];
    }
    return array.outlined[index - NumInlinedElements];
  }

  // Return the mutable pointer to the index'th slot in array.
  static Elem* MutableArraySlot(Array* array, size_t index) {
    if (index < NumInlinedElements) {
      return &array->inlined[index];
    }
    return &array->outlined[index - NumInlinedElements];
  }

  // Find the first filled slot at or after "from". For incremenenting an
  // iterator.
  size_t NextValidElementInArray(const Array& array, size_t from) const {
    size_t i = from;
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
  bool FindInArray(const Array& array, const Key& k, size_t* index) const {
    assert(has_empty_key());
    *index = ComputeHash(k) & (array.capacity - 1);
    const size_t start_index = *index;
    for (int retries = 1;; ++retries) {
      const Elem& elem = ArraySlot(array, *index);
      const Key& key = ExtractKey(elem);
      if (KeysEqual(key, k)) {
        return true;
      }
      if (IsEmptyKey(key)) {
        return false;
      }
      if (retries >= array.capacity) {
        return false;
      }
      *index = QuadraticProbe(array, *index, retries);
    }
  }

  // Either find "k" in the array, or find a slot into which "k" can be
  // inserted.
  InsertResult InsertInArray(Array* array, const Key& k, size_t* index) {
    assert(has_empty_key());
    *index = ComputeHash(k) & (array->capacity - 1);
    const size_t start_index = *index;
    for (int retries = 1;; ++retries) {
      const Elem& elem = ArraySlot(*array, *index);
      const Key& key = ExtractKey(elem);
      if (KeysEqual(key, k)) {
        return KEY_FOUND;
      }
      if (IsDeletedKey(key)) {
        return EMPTY_SLOT_FOUND;
      }
      if (IsEmptyKey(key)) {
        if (array->num_empty_slots < array->capacity * kMaxLoadFactor) {
          return ARRAY_FULL;
        } else {
          --array->num_empty_slots;
          return EMPTY_SLOT_FOUND;
        }
      }
      *index = QuadraticProbe(*array, *index, retries);
    }
  }

  // Double the hash table size. Culls tombstones and move all the existing
  // elements and
  void ExpandTable() {
    size_t new_capacity = array_.capacity * 2;
    Array new_array(new_capacity);
    InitArray(empty_key(), &new_array);
    for (Elem& e : *this) {
      size_t index;
      if (FindInArray(new_array, ExtractKey(e), &index)) {
        abort();
      }
      *MutableArraySlot(&new_array, index) = std::move(e);
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

  bool has_empty_key() const { return p0_.first; }
  void set_has_empty_key(bool v) { p0_.first = v; }

  bool has_deleted_key() const { return p1_.first; }
  void set_has_deleted_key(bool v) { p1_.first = v; }
  const Key& ExtractKey(const Elem& elem) const {
    return p0_.second().Get(elem);
  }
  Key* ExtractMutableKey(Elem* elem) const {
    return p0_.second().Mutable(elem);
  }
  size_t ComputeHash(const Key& key) const { return p1_.second()(key); }
  bool KeysEqual(const Key& k0, const Key& k1) const {
    return p2_.second()(k0, k1);
  }
  const Key& empty_key() const { return p2_.first; }
  bool IsEmptyKey(const Key& k) const { return KeysEqual(p2_.first, k); }

  bool IsDeletedKey(const Key& k) const {
    if (!has_deleted_key()) return false;
    return KeysEqual(deleted_key_, k);
  }

  // GetKey, Hash, and EqualTo are often empty, so use a pidgin compressed pair
  // to save space. Google "empty base optimization" for more details.
  CompressedPair<bool, GetKey> p0_;  // combo of has_empty_key and GetKey
  CompressedPair<bool, Hash> p1_;    // combo of has_deleted_key and Hash.
  CompressedPair<Key, EqualTo> p2_;  // combo of empty_key and equal_to functor.
  Key deleted_key_;
  Array array_;
};

template <typename Key, typename Value, int NumInlinedElements,
          typename Hash = std::hash<Key>, typename EqualTo = std::equal_to<Key>>
class InlinedHashMap {
 public:
  using Elem = std::pair<Key, Value>;
  struct GetKey {
    const Key& Get(const Elem& elem) const { return elem.first; }
    Key* Mutable(Elem* elem) const { return &elem->first; }
  };
  using Table =
      InlinedHashTable<Key, Elem, NumInlinedElements, GetKey, Hash, EqualTo>;
  using iterator = typename Table::iterator;

  void set_empty_key(const Key& k) { impl_.set_empty_key(k); }
  void set_deleted_key(const Key& k) { impl_.set_deleted_key(k); }
  bool empty() const { return impl_.empty(); }
  iterator begin() { return impl_.begin(); }
  iterator end() { return impl_.end(); }
  size_t size() const { return impl_.size(); }
  iterator erase(iterator i) { return impl_.erase(i); }
  size_t erase(const Key& k) { return impl_.erase(k); }
  std::pair<iterator, bool> insert(Elem&& value) {
    return impl_.insert(std::move(value));
  }
  iterator find(const Key& k) { return impl_.find(k); }
  Value& operator[](const Key& k) {
    size_t index;
    typename Table::InsertResult result = impl_.Insert(k, &index);
    Elem* slot = impl_.Mutable(index);
    if (result != Table::KEY_FOUND) {
      // newly inserted. fill the key.
      slot->first = k;
    }
    return slot->second;
  }

 private:
  Table impl_;
};

template <typename Elem, int NumInlinedElements,
          typename Hash = std::hash<Elem>,
          typename EqualTo = std::equal_to<Elem>>
class InlinedHashSet {
 public:
  struct GetKey {
    const Elem& Get(const Elem& elem) const { return elem; }
    Elem* Mutable(Elem* elem) const { return elem; }
  };
  using Table =
      InlinedHashTable<Elem, Elem, NumInlinedElements, GetKey, Hash, EqualTo>;
  using iterator = typename Table::iterator;

  void set_empty_key(const Elem& k) { impl_.set_empty_key(k); }
  void set_deleted_key(const Elem& k) { impl_.set_deleted_key(k); }
  bool empty() const { return impl_.empty(); }
  iterator begin() { return impl_.begin(); }
  iterator end() { return impl_.end(); }
  size_t size() const { return impl_.size(); }
  std::pair<iterator, bool> insert(Elem&& value) {
    return impl_.insert(std::move(value));
  }
  std::pair<iterator, bool> insert(const Elem& value) {
    return impl_.insert(value);
  }

  iterator find(const Elem& k) { return impl_.find(k); }
  iterator erase(iterator i) { return impl_.erase(i); }
  size_t erase(const Elem& k) { return impl_.erase(k); }

 private:
  Table impl_;
};
