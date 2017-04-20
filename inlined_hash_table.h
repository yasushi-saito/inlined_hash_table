// Author: yasushi.saito@gmail.com

#pragma once

#include <array>
#include <cassert>
#include <functional>
#include <memory>
#include <type_traits>

// TODO: const iterators
// TODO: size reservation
// TODO: move constructors
// TODO: allow passing Hash, EqualTo, Options through the constructor.
// TODO: Allow overriding the size type (size_t) to compress the table overhead.
// TODO: Do empty base optimization when NumInlinedElements==0.

// InlinedHashTable is an implementation detail that underlies InlinedHashMap
// and InlinedHashSet. It's not for public use.
//
// NumInlinedElements is the number of elements stored in-line with the table.
//
// Options is a class that defines two methods:
//
//   const Key& EmptyKey() const;
//   const Key& DeletedKey() const;
//
// EmptyKey() should return a key that represents an unused key.  DeletedKey()
// should return a tombstone key. DeletedKey() needs to be defined iff you use
// erase().
template <typename Key, typename Elem, int NumInlinedElements, typename Options,
          typename GetKey, typename Hash, typename EqualTo, typename IndexType>
class InlinedHashTable {
 public:
  static_assert((NumInlinedElements & (NumInlinedElements - 1)) == 0,
                "NumInlinedElements must be a power of two");
  InlinedHashTable()
      : array_(NumInlinedElements > 0 ? NumInlinedElements : 16) {
    InitArray(options_.EmptyKey(), &array_);
  }

  class iterator {
   public:
    using Table = InlinedHashTable<Key, Elem, NumInlinedElements, Options,
                                   GetKey, Hash, EqualTo, IndexType>;
    iterator(Table* table, IndexType index) : table_(table), index_(index) {}

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
    IndexType index_;
  };

  // TODO(saito) Support const_iterator, cbegin, etc.

  iterator begin() {
    return iterator(this, NextValidElementInArray(array_, 0));
  }

  iterator end() { return iterator(this, kEnd); }

  iterator find(const Key& k) {
    IndexType index;
    if (FindInArray(array_, k, &index)) {
      return iterator(this, index);
    } else {
      return end();
    }
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
  enum InsertResult { KEY_FOUND, EMPTY_SLOT_FOUND, ARRAY_FULL };
  InsertResult Insert(const Key& key, IndexType* index) {
    InsertResult result = InsertInArray(&array_, key, index);
    if (result == KEY_FOUND) return result;
    ++array_.size;
    if (result != ARRAY_FULL) return result;
    ExpandTable();
    return InsertInArray(&array_, key, index);
  }

 private:
  static constexpr IndexType kEnd = std::numeric_limits<IndexType>::max();
  static constexpr double kMaxLoadFactor = 0.75;

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
    *index = ComputeHash(k) & (array.capacity - 1);
    const IndexType start_index = *index;
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
  InsertResult InsertInArray(Array* array, const Key& k, IndexType* index) {
    *index = ComputeHash(k) & (array->capacity - 1);
    const IndexType start_index = *index;
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
    IndexType new_capacity = array_.capacity * 2;
    Array new_array(new_capacity);
    InitArray(options_.EmptyKey(), &new_array);
    for (Elem& e : *this) {
      IndexType index;
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

  const Key& ExtractKey(const Elem& elem) const { return get_key_.Get(elem); }
  Key* ExtractMutableKey(Elem* elem) const { return get_key_.Mutable(elem); }
  IndexType ComputeHash(const Key& key) const { return hash_(key); }
  bool KeysEqual(const Key& k0, const Key& k1) const {
    return equal_to_(k0, k1);
  }
  bool IsEmptyKey(const Key& k) const {
    return KeysEqual(options_.EmptyKey(), k);
  }

  template <typename TOptions, typename TEqualTo>
  static auto SfinaeIsDeletedKey(const Key* k, const TOptions* options,
                                 const TEqualTo* equal_to)
      -> decltype(KeysEqual(options->DeletedKey(), *k)) {
    return equal_to(options->DeletedKey(), *k);
  }

  static auto SfinaeIsDeletedKey(...) -> bool { return false; }

  bool IsDeletedKey(const Key& k) const {
    // return KeysEqual(options_.DeletedKey(), k);
    return SfinaeIsDeletedKey(&k, &options_, &equal_to_);
  }

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
  struct GetKey {
    const Key& Get(const Elem& elem) const { return elem.first; }
    Key* Mutable(Elem* elem) const { return &elem->first; }
  };
  using Table = InlinedHashTable<Key, Elem, NumInlinedElements, Options, GetKey,
                                 Hash, EqualTo, IndexType>;
  using iterator = typename Table::iterator;

  bool empty() const { return impl_.empty(); }
  iterator begin() { return impl_.begin(); }
  iterator end() { return impl_.end(); }
  IndexType size() const { return impl_.size(); }
  iterator erase(iterator i) { return impl_.erase(i); }
  IndexType erase(const Key& k) { return impl_.erase(k); }
  std::pair<iterator, bool> insert(Elem&& value) {
    return impl_.insert(std::move(value));
  }
  iterator find(const Key& k) { return impl_.find(k); }
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

  bool empty() const { return impl_.empty(); }
  iterator begin() { return impl_.begin(); }
  iterator end() { return impl_.end(); }
  IndexType size() const { return impl_.size(); }
  std::pair<iterator, bool> insert(Elem&& value) {
    return impl_.insert(std::move(value));
  }
  std::pair<iterator, bool> insert(const Elem& value) {
    return impl_.insert(value);
  }

  iterator find(const Elem& k) { return impl_.find(k); }
  iterator erase(iterator i) { return impl_.erase(i); }
  IndexType erase(const Elem& k) { return impl_.erase(k); }

 private:
  Table impl_;
};
