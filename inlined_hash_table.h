// Author: yasushi.saito@gmail.com

#pragma once

#include <array>
#include <functional>
#include <memory>

template <typename Key, typename Elem, int NumInlinedElements, typename GetKey,
          typename Hash, typename EqualTo>
class InlinedHashTable {
 public:
  InlinedHashTable() : size_(0), array_(NumInlinedElements) {}

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
      for (;;) {
        ++index_;
        if (index_ >= table_->capacity()) {
          index_ = kEnd;
          return *this;
        }
        const Key& k = table_->get_key_(table_->array_.Slot(index_));
        if (!table_->equal_to_(k, table_->empty_key_) &&
            !table_->equal_to_(k, table_->deleted_key_)) {
          break;
        }
      }
      return *this;
    }

   private:
    Table* table_;
    size_t index_;
  };

  iterator begin() {
    size_t i = 0;
    for (;;) {
      const Key& k = get_key_(array_.Slot(i));
      if (!equal_to_(empty_key_, k) && !equal_to_(deleted_key_, k)) {
        return iterator(this, i);
      }
      if (++i >= array_.capacity()) {
        return end();
      }
    }
  }

  iterator end() { return iterator(this, kEnd); }

  void set_empty_key(const Key& k) { empty_key_ = k; }
  void set_deleted_key(const Key& k) { deleted_key_ = k; }

  iterator find(const Key& k) {
    size_t index;
    if (Find(array_, k, &index)) {
      return iterator(this, index);
    } else {
      return end();
    }
  }

  std::pair<iterator, bool> insert(Elem&& value) {
    std::pair<size_t, bool> result = Insert(get_key_(value));
    if (!result.second) {
      return std::make_pair(iterator(this, result.first), false);
    }
    *array_.Mutable(result.first) = std::move(value);
    return std::make_pair(iterator(this, result.first), true);
  }

  Elem* Mutable(size_t index) { return array_.Mutable(index); }

  std::pair<size_t, bool> Insert(const Key& k) {
    size_t index;
    if (Find(array_, k, &index)) {
      return std::make_pair(index, false);
    }
    ++size_;
    if (index != kFull) {
      return std::make_pair(index, true);
    }
    ExpandTable();
    Find(array_, k, &index);
    return std::make_pair(index, true);
  }

  bool empty() const { return size_ == 0; }
  size_t size() const { return size_; }
  size_t capacity() const { return array_.capacity(); }

 private:
  using InlineArray = std::array<Elem, NumInlinedElements>;
  using OutlineArray = std::unique_ptr<Elem[]>;

  class Array {
   public:
    explicit Array(size_t capacity) : size_(0), capacity_(capacity) {
      if (capacity_ > inlined_.size()) {
        outlined_.reset(new Elem[capacity - inlined_.size()]);
      }
    }

    void Init(const Key& empty_key) {
      std::fill_n(inlined_.data(), inlined_.size(), empty_key);
      if (outlined_ != nullptr) {
        std::fill_n(outlined_.get(), capacity_ - inlined_.size(), empty_key);
      }
    }

    const Elem& Slot(size_t index) const {
      if (index < NumInlinedElements) {
        return inlined_[index];
      }
      return outlined_[index - NumInlinedElements];
    }

    Elem* Mutable(size_t index) {
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
      const Elem& elem = array.Slot(*index);
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
    size_t new_capacity = array_.capacity() * 2;
    Array new_array(new_capacity);
    for (Elem& e : *this) {
      size_t index;
      if (Find(new_array, get_key_(e), &index)) {
        abort();
      }
      *new_array.Mutable(index) = std::move(e);
    }
    array_ = std::move(new_array);
  }

  static constexpr size_t kFull = std::numeric_limits<size_t>::max() - 1;
  static constexpr size_t kEnd = std::numeric_limits<size_t>::max();
  GetKey get_key_;
  Hash hash_;
  EqualTo equal_to_;
  Key empty_key_;
  Key deleted_key_;
  size_t size_;
  Array array_;
};

template <typename Key, typename Value, int NumInlinedElements,
          typename Hash = std::hash<Key>, typename EqualTo = std::equal_to<Key>>
class InlinedHashMap {
 public:
  using Elem = std::pair<Key, Value>;
  struct GetKey {
    const Key& operator()(const Elem& elem) const { return elem.first; }
  };
  using Table =
      InlinedHashTable<Key, Elem, NumInlinedElements, GetKey, Hash, EqualTo>;

  using iterator = typename Table::iterator;
  void set_empty_key(const Key& k) { impl_.set_empty_key(k); }
  void set_deleted_key(const Key& k) { impl_.set_deleted_key(k); }
  bool empty() const { return impl_.empty(); }
  iterator begin() { return impl_. begin(); }
  iterator end() { return impl_. end(); }
  size_t size() const { return impl_.size(); }

  std::pair<iterator, bool> insert(Elem&& value) {
    return impl_.insert(std::move(value));
  }

  iterator find(const Key& k) { return impl_.find(k); }
  Value& operator[](const Key& k) {
    std::pair<size_t, bool> result = impl_.Insert(k);
    Elem* slot = impl_.Mutable(result.first);
    if (result.second) {
      // newly inserted. fill the key.
      slot->first = k;
    }
    return slot->second;
  }

 private:
  Table impl_;
};
