namespace egg::ovum {
  class Variant;
  class VariantFactory;
  class VariantSoft;

  // Void, Null, Bool, Int, Float, String, Object, Pointer, Indirect, Break, Continue, Return, Yield, Throw, Hard (plus Arithmetic, Any, AnyQ, FlowControl)
  enum class VariantBits {
#define EGG_OVUM_VARIANT_ENUM(name, text) name = 1 << impl::name,
    EGG_OVUM_VARIANT(EGG_OVUM_VARIANT_ENUM)
#undef EGG_OVUM_VARIANT_ENUM
    Arithmetic = Int | Float,
    Any = Bool | Int | Float | String | Object,
    AnyQ = Any | Null,
    FlowControl = Break | Continue | Return | Yield | Throw
  };
#undef EGG_OVUM_BASAL_ENUM
  inline VariantBits operator|(VariantBits lhs, VariantBits rhs) {
    return Bits::set(lhs, rhs);
  }

  class VariantKind {
  protected:
    VariantBits kind;
  public:
    explicit VariantKind(VariantBits bits) : kind(bits) {}
    bool hasOne(VariantBits mask) const { return Bits::hasOneSet(this->kind, mask); }
    bool hasAny(VariantBits mask) const { return Bits::hasAnySet(this->kind, mask); }
    bool hasAll(VariantBits mask) const { return Bits::hasAllSet(this->kind, mask); }
    bool hasBool() const { return Bits::hasAnySet(this->kind, VariantBits::Bool); }
    bool hasString() const { return Bits::hasAnySet(this->kind, VariantBits::String); }
    bool hasObject() const { return Bits::hasAnySet(this->kind, VariantBits::Object); }
    bool hasPointer() const { return Bits::hasAnySet(this->kind, VariantBits::Pointer); }
    bool hasIndirect() const { return Bits::hasAnySet(this->kind, VariantBits::Indirect); }
    bool hasThrow() const { return Bits::hasAnySet(this->kind, VariantBits::Throw); }
    bool hasYield() const { return Bits::hasAnySet(this->kind, VariantBits::Yield); }
    bool hasFlowControl() const { return Bits::hasAnySet(this->kind, VariantBits::FlowControl); }
    bool is(VariantBits value) const { return this->kind == value; }
    bool isVoid() const { return this->kind == VariantBits::Void; }
    bool isNull() const { return this->kind == VariantBits::Null; }
    bool isBool() const { return this->kind == VariantBits::Bool; }
    bool isInt() const { return this->kind == VariantBits::Int; }
    bool isFloat() const { return this->kind == VariantBits::Float; }
    bool isString() const { return this->kind == (VariantBits::String | VariantBits::Hard); }
    VariantBits getKind() const {
      return this->kind;
    }
    static void printTo(std::ostream& stream, VariantBits kind);
  };

  class IVariantSoft : public ICollectable {
  public:
    virtual Variant& getVariant() = 0;
    virtual Type getPointerType() const = 0;
  };

  class Variant final : public VariantKind {
    // Stop type promotion for implicit constructors
    template<typename T> Variant(T rhs) = delete;
    friend class VariantFactory;
    friend class VariantSoft;
  private:
    union {
      Bool b; // Bool
      Int i; // Int
      Float f; // Float
      const IMemory* s; // String|Memory
      IObject* o; // Object
      IVariantSoft* p; // Pointer|Indirect
    } u;
    explicit Variant(VariantBits kind) : VariantKind(kind) {
      this->u.s = nullptr; // keep valgrind happy
    }
  public:
    // Construction/destruction
    Variant() : Variant(VariantBits::Void) {
      assert(this->validate());
    }
    Variant(const Variant& rhs) : VariantKind(rhs.kind) {
      Variant::copyInternals(*this, rhs);
      assert(this->validate());
      assert(rhs.validate());
    }
    Variant(Variant&& rhs) noexcept : VariantKind(rhs.kind) {
      Variant::moveInternals(*this, rhs);
      rhs.kind = VariantBits::Void;
      assert(this->validate());
      assert(rhs.validate());
    }
    ~Variant() {
      // Don't validate because we may be soft
      Variant::destroyInternals(*this);
    }
    // Assignment
    Variant& operator=(const Variant& rhs) {
      assert(this->validate());
      assert(rhs.validate());
      if (this != &rhs) {
        // The resources of 'before' will be cleaned up after the assignment
        Variant before{ std::move(*this) };
        this->kind = rhs.kind;
        Variant::copyInternals(*this, rhs);
      }
      assert(this->validate());
      assert(rhs.validate());
      return *this;
    }
    Variant& operator=(Variant&& rhs) {
      // See https://stackoverflow.com/a/9322542
      assert(this->validate());
      assert(rhs.validate());
      if (this != &rhs) {
        // Need to make sure the resources of the original 'this' are cleaned up last
        std::swap(this->kind, rhs.kind);
        std::swap(this->u, rhs.u);
        Variant::destroyInternals(rhs);
        rhs.kind = VariantBits::Void;
      }
      assert(this->validate());
      assert(rhs.validate());
      return *this;
    }
    // Equality
    bool operator==(const Variant& other) const {
      return Variant::equals(*this, other);
    }
    bool operator!=(const Variant& other) const {
      return !Variant::equals(*this, other);
    }
    static bool equals(const Variant& lhs, const Variant& rhs);
    // Null
    Variant(std::nullptr_t) : Variant(VariantBits::Null) {
      assert(this->validate());
    }
    // Bool
    Variant(bool value) : VariantKind(VariantBits::Bool) {
      this->u.b = value;
      assert(this->validate());
    }
    bool getBool() const {
      assert(this->validate());
      assert(this->hasAny(VariantBits::Bool));
      return this->u.b;
    }
    // Int (support automatic promotion of 32-bit integers)
    Variant(int32_t value) : VariantKind(VariantBits::Int) {
      this->u.i = value;
      assert(this->validate());
    }
    Variant(int64_t value) : VariantKind(VariantBits::Int) {
      this->u.i = value;
      assert(this->validate());
    }
    Int getInt() const {
      assert(this->validate());
      assert(this->hasAny(VariantBits::Int));
      return this->u.i;
    }
    // Float (support automatic promotion of 32-bit IEEE)
    Variant(float value) : VariantKind(VariantBits::Float) {
      this->u.f = value;
      assert(this->validate());
    }
    Variant(double value) : VariantKind(VariantBits::Float) {
      this->u.f = value;
      assert(this->validate());
    }
    Float getFloat() const {
      assert(this->validate());
      assert(this->hasAny(VariantBits::Float));
      return this->u.f;
    }
    // String
    Variant(const String& value) : VariantKind(VariantBits::String | VariantBits::Hard) {
      this->u.s = String::hardAcquire(value.get());
      assert(this->validate());
    }
    Variant(const std::string& value) : VariantKind(VariantBits::String | VariantBits::Hard) {
      // We've got to create a string without an allocator
      this->u.s = Variant::acquireFallbackString(value.data(), value.size());
      assert(this->validate());
    }
    Variant(const char* value) : VariantKind(VariantBits::String | VariantBits::Hard) {
      if (value == nullptr) {
        this->kind = VariantBits::Null;
        this->u.s = nullptr;
      } else {
        // We've got to create a string without an allocator
        this->u.s = Variant::acquireFallbackString(value, std::strlen(value));
      }
      assert(this->validate());
    }
    String getString() const {
      assert(this->validate());
      assert(this->hasAny(VariantBits::String));
      return String(this->u.s);
    }
    // Memory
    Variant(const IMemory& value) : VariantKind(VariantBits::Memory | VariantBits::Hard) {
      this->u.s = Memory::hardAcquire(&value);
      assert(this->validate());
    }
    Memory getMemory() const {
      assert(this->validate());
      assert(this->hasAny(VariantBits::Memory));
      return Memory(this->u.s);
    }
    // Object
    Variant(const Object& value) : VariantKind(VariantBits::Object | VariantBits::Hard) {
      this->u.o = Object::hardAcquire(value.get());
      assert(this->validate());
    }
    Object getObject() const {
      assert(this->validate());
      assert(this->hasAny(VariantBits::Object));
      return Object(*this->u.o);
    }
    // Pointer/Indirect
    Variant(VariantBits flavour, IVariantSoft& value) : VariantKind(flavour) {
      // Create a hard/soft pointer/indirect to a soft value
      assert(this->hasOne(VariantBits::Pointer | VariantBits::Indirect));
      assert(!value.getVariant().hasOne(VariantBits::String | VariantBits::Hard)); // must be either a string or soft
      if (this->hasAny(VariantBits::Hard)) {
        this->u.p = HardPtr<IVariantSoft>::hardAcquire(&value);
      } else {
        this->u.p = &value;
      }
      assert(this->validate());
    }
    Variant& getPointee() const {
      assert(this->validate());
      assert(this->hasOne(VariantBits::Pointer | VariantBits::Indirect));
      return this->u.p->getVariant();
    }
    // Properties
    Type getRuntimeType() const;
    String toString() const;
    bool stripFlowControl(VariantBits bits);
    void addFlowControl(VariantBits bits);
    const Variant& direct() const;
    Variant& direct();
    void soften(IBasket& basket);
    void softVisitLink(const ICollectable::Visitor& visitor) const;
    void indirect(IAllocator& allocator, IBasket& basket);
    Variant address() const;
    bool validate(bool soft = false) const;
    // Constants
    static const Variant Void;
    static const Variant Null;
    static const Variant False;
    static const Variant True;
    static const Variant EmptyString;
    static const Variant Break;
    static const Variant Continue;
    static const Variant Rethrow;
    static const Variant ReturnVoid;
  private:
    static void copyInternals(Variant& dst, const Variant& src) {
      // dst:INVALID,src:VALID => dst:VALID,src:VALID
      assert(dst.kind == src.kind);
      if (src.hasAny(VariantBits::Hard)) {
        if (src.hasAny(VariantBits::Object)) {
          dst.u.o = Object::hardAcquire(src.u.o);
          return;
        }
        if (src.hasAny(VariantBits::String | VariantBits::Memory)) {
          dst.u.s = String::hardAcquire(src.u.s);
          return;
        }
        if (src.hasAny(VariantBits::Pointer | VariantBits::Indirect)) {
          dst.u.p = HardPtr<IVariantSoft>::hardAcquire(src.u.p);
          return;
        }
      }
      dst.u = src.u;
    }
    static void moveInternals(Variant& dst, const Variant& src) {
      // dst:INVALID,src:VALID => dst:VALID,src:INVALID
      assert(dst.kind == src.kind);
      dst.u = src.u;
    }
    static void destroyInternals(Variant& dst) {
      // dst:VALID => dst:INVALID
      if (dst.hasAny(VariantBits::Hard)) {
        if (dst.hasAny(VariantBits::Object)) {
          assert(dst.u.o != nullptr);
          dst.u.o->hardRelease();
        } else if (dst.hasAny(VariantBits::String | VariantBits::Memory)) {
          if (dst.u.s != nullptr) {
            dst.u.s->hardRelease();
          }
        } else {
          assert(dst.u.p != nullptr);
          dst.u.p->hardRelease();
        }
      }
    }
    static const IMemory* acquireFallbackString(const char* utf8, size_t bytes);
  };
}
