// Author: yasushi.saito@gmail.com

#pragma once

#include <array>
#include <cassert>
#include <functional>
#include <memory>

template <typename Key, typename Elem, int NumInlinedElements, typename GetKey,
          typename Hash, typename EqualTo>
class InlinedHashTable {
 public:
  InlinedHashTable() : array_(NumInlinedElements) {}

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

    iterator operator++() {
      index_ = table_->NextValidElementInArray(table_->array_, index_ + 1);
      return *this;
    }

   private:
    friend Table;
    Table* table_;
    size_t index_;
  };

  iterator begin() {
    return iterator(this, NextValidElementInArray(array_, 0));
  }

  iterator end() { return iterator(this, kEnd); }

  void set_empty_key(const Key& k) {
    assert((flags_ & kEmptyKeySet) == 0);
    flags_ |= kEmptyKeySet;
    empty_key_ = k;
    InitArray(empty_key_, &array_);
  }

  void set_deleted_key(const Key& k) {
    assert((flags_ & kDeletedKeySet) == 0);
    flags_ |= kDeletedKeySet;
    deleted_key_ = k;
  }

  iterator find(const Key& k) {
    size_t index;
    if (FindInArray(array_, k, &index)) {
      return iterator(this, index);
    } else {
      return end();
    }
  }

  iterator erase(iterator i) {
    assert((flags_ & kDeletedKeySet) != 0);
    Elem& elem = *i;
    *get_key_.Mutable(&elem) = deleted_key_;
    --array_.size;
    return iterator(this, NextValidElementInArray(array_, i.index_ + 1));
  }

  size_t erase(const Key& k) {
    iterator i = find(k);
    if (i == end()) return 0;
    erase(i);
  }

  std::pair<iterator, bool> insert(Elem&& value) {
    size_t index;
    InsertResult result = Insert(get_key_.Get(value), &index);
    if (result == KEY_FOUND) {
      return std::make_pair(iterator(this, index), false);
    }
    *MutableArraySlot(&array_, index) = std::move(value);
    return std::make_pair(iterator(this, index), true);
  }

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

  bool empty() const { return array_.size == 0; }
  size_t size() const { return array_.size; }
  size_t capacity() const { return array_.capacity; }

 private:
  static constexpr size_t kFull = std::numeric_limits<size_t>::max() - 1;
  static constexpr size_t kEnd = std::numeric_limits<size_t>::max();
  static constexpr int kEmptyKeySet = 1;
  static constexpr int kDeletedKeySet = 2;
  static constexpr double kMaxLoadFactor = 0.75;

  struct Array {
   public:
    explicit Array(size_t capacity_arg)
        : size(0), capacity(capacity_arg), num_empty_slots(capacity) {
      if (capacity > inlined.size()) {
        outlined.reset(new Elem[capacity - inlined.size()]);
      }
    }

    std::array<Elem, NumInlinedElements> inlined;
    std::unique_ptr<Elem[]> outlined;
    size_t size;
    size_t capacity;
    size_t num_empty_slots;
  };

  static size_t NextIndex(const Array& array, size_t current, int retries) {
    return (current + retries) % array.capacity;
  }

  void InitArray(const Key& empty_key, Array* array) const {
    for (Elem& elem : array->inlined) {
      *get_key_.Mutable(&elem) = empty_key;
    }
    if (array->outlined != nullptr) {
      size_t n = array->capacity - array->inlined.size();
      for (size_t i = 0; i < n; ++i) {
        *get_key_.Mutable(&array->outlined[i]) = empty_key;
      }
    }
  }

  static const Elem& ArraySlot(const Array& array, size_t index) {
    if (index < NumInlinedElements) {
      return array.inlined[index];
    }
    return array.outlined[index - NumInlinedElements];
  }

  static Elem* MutableArraySlot(Array* array, size_t index) {
    if (index < NumInlinedElements) {
      return &array->inlined[index];
    }
    return &array->outlined[index - NumInlinedElements];
  }

  size_t NextValidElementInArray(const Array& array, size_t from) const {
    size_t i = from;
    for (;;) {
      if (i >= array.capacity) {
        return kEnd;
      }

      const Key& k = get_key_.Get(ArraySlot(array, i));
      if (!IsEmptyKey(k) && !IsDeletedKey(k)) {
        return i;
      }
      ++i;
    }
  }

  bool FindInArray(const Array& array, const Key& k, size_t* index) const {
    assert((flags_ & kEmptyKeySet) != 0);
    *index = hash_(k) % array.capacity;
    const size_t start_index = *index;
    for (int retries = 1;; ++retries) {
      const Elem& elem = ArraySlot(array, *index);
      const Key& key = get_key_.Get(elem);
      if (equal_to_(key, k)) {
        return true;
      }
      if (IsEmptyKey(key)) {
        return false;
      }
      if (retries >= array.capacity) {
        *index = kFull;
        return false;
      }
      *index = NextIndex(array, *index, retries);
    }
  }

  InsertResult InsertInArray(Array* array, const Key& k, size_t* index) {
    assert((flags_ & kEmptyKeySet) != 0);
    *index = hash_(k) % array->capacity;
    const size_t start_index = *index;
    for (int retries = 1;; ++retries) {
      const Elem& elem = ArraySlot(*array, *index);
      const Key& key = get_key_.Get(elem);
      if (equal_to_(key, k)) {
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
      *index = NextIndex(*array, *index, retries);
    }
  }

  void ExpandTable() {
    size_t new_capacity = array_.capacity * 2;
    Array new_array(new_capacity);
    InitArray(empty_key_, &new_array);
    for (Elem& e : *this) {
      size_t index;
      if (FindInArray(new_array, get_key_.Get(e), &index)) {
        abort();
      }
      *MutableArraySlot(&new_array, index) = std::move(e);
    }
    array_ = std::move(new_array);
  }

  bool IsEmptyKey(const Key& k) const { return equal_to_(empty_key_, k); }

  bool IsDeletedKey(const Key& k) const {
    if (!(flags_ & kDeletedKeySet)) {
      return false;
    }
    return equal_to_(deleted_key_, k);
  }

  unsigned flags_ = 0;
  GetKey get_key_;
  Hash hash_;
  EqualTo equal_to_;
  Key empty_key_;
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

  iterator find(const Elem& k) { return impl_.find(k); }

 private:
  Table impl_;
};
