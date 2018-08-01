#include "ovum/ovum.h"

namespace {
  class MemoryEmpty : public egg::ovum::NotReferenceCounted<egg::ovum::IMemory> {
    MemoryEmpty(const MemoryEmpty&) = delete;
    MemoryEmpty& operator=(const MemoryEmpty&) = delete;
  private:
    static const egg::ovum::Byte empty;
    MemoryEmpty() {}
  public:
    virtual const egg::ovum::Byte* begin() const override {
      return &empty;
    }
    virtual const egg::ovum::Byte* end() const override {
      return &empty;
    }
    static const MemoryEmpty instance;
  };
  const egg::ovum::Byte MemoryEmpty::empty{};
  const MemoryEmpty MemoryEmpty::instance{};

  class MemoryContiguous : public egg::ovum::HardReferenceCounted<egg::ovum::IMemory> {
    MemoryContiguous(const MemoryContiguous&) = delete;
    MemoryContiguous& operator=(const MemoryContiguous&) = delete;
  private:
    size_t size;
  public:
    MemoryContiguous(egg::ovum::IAllocator& allocator, size_t size) : HardReferenceCounted(allocator), size(size) {
    }
    virtual const egg::ovum::Byte* begin() const override {
      return this->base();
    }
    virtual const egg::ovum::Byte* end() const override {
      return this->base() + this->size;
    }
  private:
    egg::ovum::Byte* base() const {
      return const_cast<egg::ovum::Byte*>(reinterpret_cast<const egg::ovum::Byte*>(this + 1));
    }
  };
}

egg::ovum::MemoryMutable egg::ovum::MemoryFactory::create(IAllocator& allocator, size_t bytes) {
  if (bytes == 0) {
    return egg::ovum::MemoryMutable(&MemoryEmpty::instance);
  }
  return egg::ovum::MemoryMutable(allocator.create<MemoryContiguous>(bytes, allocator, bytes));
}

egg::ovum::MemoryBuilder::MemoryBuilder(egg::ovum::IAllocator& allocator)
  : allocator(allocator),
    chunks(),
    bytes(0) {
}

void egg::ovum::MemoryBuilder::add(const Byte* begin, const Byte* end) {
  assert(begin != nullptr);
  assert(end >= begin);
  auto size = size_t(end - begin);
  if (size > 0) {
    this->chunks.emplace_back(nullptr, begin, size);
    this->bytes += size;
  }
}

void egg::ovum::MemoryBuilder::add(const IMemory& memory) {
  auto size = memory.bytes();
  if (size > 0) {
    this->chunks.emplace_back(&memory, memory.begin(), size);
    this->bytes += size;
  }
}

egg::ovum::IMemoryPtr egg::ovum::MemoryBuilder::bake() {
  if (this->chunks.size() == 1) {
    // There's only a single chunk in the list
    auto front = this->chunks.front().memory;
    if (front != nullptr) {
      // Simply re-use the memory block
      this->reset();
      return front;
    }
  }
  auto created = MemoryFactory::create(this->allocator, this->bytes);
  auto* ptr = created.begin();
  for (auto& chunk : this->chunks) {
    std::memcpy(ptr, chunk.base, chunk.bytes);
    ptr += chunk.bytes;
  }
  assert(ptr == created.end());
  this->reset();
  return created.bake();
}

void egg::ovum::MemoryBuilder::reset() {
  this->chunks.clear();
  this->bytes = 0;
}
