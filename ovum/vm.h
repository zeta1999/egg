namespace egg::ovum {
  using Bool = bool;
  using Byte = uint8_t;
  using Int = uint64_t;
  using Float = double;

  template<typename T>
  class Atomic {
    Atomic(Atomic&) = delete;
    Atomic& operator=(Atomic&) = delete;
  public:
    using Underlying = T;
  private:
    std::atomic<Underlying> atomic;
  public:
    explicit Atomic(Underlying value) : atomic(value) {
    }
    Underlying get() const {
      // Get the current value
      return std::atomic_load(&this->atomic);
    }
    Underlying add(Underlying value) {
      // Return the value AFTER the addition
      return std::atomic_fetch_add(&this->atomic, value) + value;
    }
    Underlying increment() {
      // The result should be strictly positive
      auto result = this->add(1);
      assert(result > 0);
      return result;
    }
    Underlying decrement() {
      // The result should not be negative
      auto result = this->add(-1);
      assert(result >= 0);
      return result;
    }
  };

  class IHardAcquireRelease {
  public:
    virtual ~IHardAcquireRelease() {}
    virtual IHardAcquireRelease* hardAcquire() const = 0;
    virtual void hardRelease() const = 0;
  };

  template<class T>
  class HardPtr {
  private:
    T* ptr;
  public:
    HardPtr() : ptr(nullptr) {
    }
    explicit HardPtr(const T* rhs) : ptr(HardPtr::hardAcquire(rhs)) {
    }
    HardPtr(const HardPtr& rhs) : ptr(rhs.hardAcquire()) {
    }
    HardPtr(HardPtr&& rhs) : ptr(rhs.get()) {
      rhs.ptr = nullptr;
    }
    template<typename U>
    HardPtr(const HardPtr<U>& rhs) : ptr(rhs.hardAcquire()) {
    }
    HardPtr& operator=(const HardPtr& rhs) {
      this->set(rhs.get());
      return *this;
    }
    HardPtr& operator=(HardPtr&& rhs) {
      if (this->ptr != nullptr) {
        this->ptr->hardRelease();
      }
      this->ptr = rhs.get();
      rhs.ptr = nullptr;
      return *this;
    }
    template<typename U>
    HardPtr& operator=(const HardPtr<U>& rhs) {
      this->set(rhs.get());
      return *this;
    }
    ~HardPtr() {
      if (this->ptr != nullptr) {
        this->ptr->hardRelease();
      }
    }
    T* hardAcquire() const {
      return HardPtr::hardAcquire(this->ptr);
    }
    T* get() const {
      return this->ptr;
    }
    void set(T* rhs) {
      auto* old = this->ptr;
      this->ptr = HardPtr::hardAcquire(rhs);
      if (old != nullptr) {
        old->hardRelease();
      }
    }
    void swap(HardPtr<T>& rhs) {
      std::swap(this->ptr, rhs.ptr);
    }
    T& operator*() const {
      assert(this->ptr != nullptr);
      return *this->ptr;
    }
    T* operator->() const {
      assert(this->ptr != nullptr);
      return this->ptr;
    }
    bool operator==(nullptr_t) const {
      return this->ptr == nullptr;
    }
    bool operator!=(nullptr_t) const {
      return this->ptr != nullptr;
    }
    static T* hardAcquire(const T* ptr) {
      if (ptr != nullptr) {
        return static_cast<T*>(ptr->hardAcquire());
      }
      return nullptr;
    }
  };
  template<typename T>
  bool operator==(nullptr_t, const HardPtr<T>& ptr) {
    // Yoda equality comparison used by GoogleTest
    return ptr == nullptr;
  }
  template<typename T>
  bool operator!=(nullptr_t, const HardPtr<T>& ptr) {
    // Yoda inequality comparison used by GoogleTest
    return ptr != nullptr;
  }

  template<class T>
  class HardRef {
  private:
    T* ptr;
  public:
    HardRef() : ptr(T::defval()) {
    }
    explicit HardRef(const T& rhs) : ptr(&HardRef::hardAcquire(rhs)) {
    }
    HardRef(const HardRef& rhs) : ptr(&rhs.hardAcquire()) {
    }
    HardRef(HardRef&& rhs) : ptr(&rhs.get()) {
    }
    template<typename U>
    HardRef(const HardRef<U>& rhs) : ptr(&rhs.hardAcquire()) {
    }
    HardRef& operator=(const HardRef& rhs) {
      this->set(rhs.get());
      return *this;
    }
    HardRef& operator=(HardRef&& rhs) {
      assert(this->ptr != nullptr);
      this->ptr->hardRelease();
      this->ptr = &rhs.get();
      rhs.ptr = nullptr;
      return *this;
    }
    template<typename U>
    HardRef& operator=(const HardRef<U>& rhs) {
      this->set(rhs.get());
      return *this;
    }
    ~HardRef() {
      // The pointer may be null after a move operation
      if (this->ptr != nullptr) {
        this->ptr->hardRelease();
      }
    }
    T& hardAcquire() const {
      assert(this->ptr != nullptr);
      return HardRef::hardAcquire(*this->ptr);
    }
    T& get() const {
      assert(this->ptr != nullptr);
      return *this->ptr;
    }
    void set(T& rhs) {
      assert(this->ptr != nullptr);
      auto* old = this->ptr;
      this->ptr = &HardRef::hardAcquire(rhs);
      old->hardRelease();
    }
    void swap(HardRef<T>& rhs) {
      std::swap(this->ptr, rhs.ptr);
    }
    T& operator*() const {
      assert(this->ptr != nullptr);
      return *this->ptr;
    }
    T* operator->() const {
      assert(this->ptr != nullptr);
      return this->ptr;
    }
    static T& hardAcquire(const T& ref) {
      return *static_cast<T*>(ref.hardAcquire());
    }
  };

  class IAllocator {
  public:
    struct Statistics {
      uint64_t totalBlocksAllocated;
      uint64_t totalBytesAllocated;
      uint64_t currentBlocksAllocated;
      uint64_t currentBytesAllocated;
    };

    virtual void* allocate(size_t bytes, size_t alignment) = 0;
    virtual void deallocate(void* allocated, size_t alignment) = 0;
    virtual bool statistics(Statistics& out) const = 0;

    template<typename T, typename... ARGS>
    T* create(size_t extra, ARGS&&... args) {
      // Use perfect forwarding to in-place new
      size_t bytes = sizeof(T) + extra;
      void* allocated = this->allocate(bytes, alignof(T));
      assert(allocated != nullptr);
      return new(allocated) T(std::forward<ARGS>(args)...);
    }
    template<typename T>
    void destroy(const T* allocated) {
      assert(allocated != nullptr);
      allocated->~T();
      this->deallocate(const_cast<T*>(allocated), alignof(T));
    }
  };

  class IMemory : public IHardAcquireRelease {
  public:
    virtual const Byte* begin() const = 0;
    virtual const Byte* end() const = 0;

    size_t bytes() const {
      return size_t(this->end() - this->begin());
    }
  };
  using IMemoryPtr = HardPtr<const IMemory>;

  class IString : public IHardAcquireRelease {
  public:
    virtual size_t length() const = 0;
    virtual IMemoryPtr memoryUTF8(size_t codePointOffset = 0, size_t codePointLength = SIZE_MAX) const = 0;
  };
  using String = HardRef<const IString>;

  class ICollectable : public IHardAcquireRelease {
  public:
    using Visitor = std::function<void(const ICollectable& from, const ICollectable& to)>;
    virtual void visitSoftLinks(const ICollectable& from, const ICollectable& to) const = 0;
  };

  enum class VariantBits {
    Void = 1 << 0,
    Null = 1 << 1,
    Bool = 1 << 2,
    Int = 1 << 3,
    Float = 1 << 4,
    String = 1 << 5,
    Object = 1 << 6,
    Memory = 1 << 7,
    Pointer = 1 << 8,
    Indirect = 1 << 9,
    Exception = 1 << 10
  };

  class VariantKind {
  public:
    using Underlying = std::underlying_type_t<VariantBits>;
  private:
    Underlying bits;
  public:
    explicit VariantKind(VariantBits bits) : bits(static_cast<Underlying>(bits)) {
    }
    bool hasAny(VariantBits mask) const {
      auto underlying = static_cast<Underlying>(mask);
      return (this->bits & underlying) != 0;
    }
    bool hasAll(VariantBits mask) const {
      auto underlying = static_cast<Underlying>(mask);
      return (this->bits & underlying) == underlying;
    }
  };
  inline VariantBits operator|(VariantBits lhs, VariantBits rhs) {
    return static_cast<VariantBits>(static_cast<VariantKind::Underlying>(lhs) | static_cast<VariantKind::Underlying>(rhs));
  }

  class Variant {
  private:
    VariantKind kind;
    union {
      Bool b;
      Int i;
      Float f;
    };
  public:
    Variant() : kind(VariantBits::Void) {
    }
  };
}
