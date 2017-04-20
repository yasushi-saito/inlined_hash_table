// Author: yasushi.saito@gmail.com

#pragma once

#include <array>
#include <functional>
#include <memory>

template <typename Key, typename Elem, int NumInlinedElements, typename GetKey,
          typename Hash = std::hash<Key>, typename EqualTo = std::equal_to<Key>>
class InlinedHashTable {
 public:
  InlinedHashTable()
      : size_(0), capacity_(NumInlinedElements), outlined_(nullptr) {}

  class iterator {
    using Table =
        InlinedHashTable<Key, Elem, NumInlinedElements, Hasher, EqualTo>;
    iterator(Table* table, size_t index) : table_(table), index_(index) {}

    bool operator==(const iterator& other) { return index_ == other.index_; }

    T& operator*() const { return *table_->Mutable(index_); }

    iterator operator++() {
      for (;;) {
        ++index_;
        if (index_ >= table_->capacity()) {
          index_ = kEnd;
          return;
        }
        Key& k = table_->get_key_(table_->array_.Slot(index_));
        if (!table_->equal_to_(table_->empty_key_) &&
            !table_->equal_to_(table_->deleted_key_)) {
          break;
        }
      }
      return this;
    }

   private:
    InlinedHashTable<Key, Elem, NumInlinedElements, Hasher, EqualTo>* table_;
    size_t index_;
  };

  iterator begin() {
    iterator i;
    i.table_ = this;
    i.index_ = 0;
    return i;
  }

  iterator end() {
    iterator i;
    i.table_ = this;
    i.index_ = capacity_;
    return i;
  }

  void set_empty_key(const T& k) { empty_key_ = k; }
  void set_deleted_key(const T& k) { deleted_key_ = k; }

  iterator end() { return iterator(this, kEnd); }

  iterator find(const Key& k) {
    size_t index;
    if (Find(array_, k, &index)) {
      return iterator(this, index);
    } else {
      return end();
    }
  }

  std::pair<iterator, bool> insert(Elem&& value) {
    size_t index;
    if (Find(array_, k, &index)) {
      return std::make_pair(iterator(this, index), false);
    }
    if (index == kEmpty) {
      *array_.Mutable(index) = std::move(value);
      return std::make_pair(iterator(this, index), false);
    }
    CHECK_EQ(index, kFull);
    ExpandTable();
    CHECK(!Find(array_, k, &index));
    CHECK_EQ(index, kEmpty);
    *array_.Mutable(index) = std::move(value);
    return std::make_pair(iterator(this, index), false);
  }

 private:
  using InlineArray = std::array<T, NumInlinedElements>;
  using OutlineArray = std::unique_ptr<T[]>;

  class Array {
   public:
    explicit Array(size_t capacity) : size_(0), capacity_(capacity) {
      if (capacity_ > inlined_.size()) {
        outlined_.reset(new T[capcity - inlined_.size()]);
      }
    }

    void Init(const Key& empty_key) {
      std::fill_n(inlined_.data(), inlined_.size(), empty_key);
      if (outlined_ != nullptr) {
        std::fill_n(outlined_.get(), capacity_ - inlined_.size(), empty_key);
      }
    }

    const T& Slot(size_t index) const {
      if (index < NumInlinedElements) {
        return inlined_[index];
      }
      return outlined_[index - NumInlinedElements];
    }

    T* Mutable(size_t index) {
      if (index < NumInlinedElements) {
        return &inlined_[index];
      }
      return &outlined_[index - NumInlinedElements];
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

   private:
    InlineArray inlined_;
    OutlineArray outlined_;
    size_t size_;
    size_t capacity_;
  };

  bool Find(const Array& array, const Key& k, size_t* index) const {
    *index = hash_(k) % array.capacity();
    const size_t start_index = *index;
    for (int retry = 1;; ++retry) {
      const T& elem = Slot(index);
      const Key& key = get_key_(elem);
      if (equal_to_(key, k)) {
        return true;
      }
      if (equal_to_(key, empty_key_)) {
        return false;
      }
      if (retry >= array.capacity()) {
        *index = kFull;
        return false;
      }
      *index = (*index + retry) % array.capacity();
    }
  }

  void ExpandTable() {
    size_t new_capacity = capacity_ * 2;
    Array new_array(new_capacity);
    for (Element& e : *this) {
      size_t index;
      if (Find(new_array, get_key_(e), &index)) {
        abort();
      }
      *new_array.Mutable(index) = std::move(e);
    }
    array_ = std::move(new_array);
  }

  inline constexpr size_t kFull = std::numeric_limits<size_t>::max() - 1;
  inline constexpr size_t kEnd = std::numeric_limits<size_t>::max();
  GetKey get_key_;
  Hash hasher_;
  EqualTo equal_to_;
  T empty_key_;
  T deleted_key_;
  Array array_;
};
