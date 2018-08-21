namespace egg::ovum {
  class Variant;
  class VariantFactory;
  class VariantSoft;

#define EGG_VM_VARIANT(X) \
  EGG_VM_BASAL(X) \
  X(Pointer, "pointer") \
  X(Indirect, "indirect") \
  X(Break, "break") \
  X(Continue, "continue") \
  X(Return, "return") \
  X(Yield, "yield") \
  X(Throw, "throw") \
  X(Hard, "hard")

  namespace impl {
    enum {
      _ = -1 // We want the next element to start at zero
#define EGG_VM_VARIANT_ENUM(name, text) , name
      EGG_VM_VARIANT(EGG_VM_VARIANT_ENUM)
#undef EGG_VM_VARIANT_ENUM
    };
  }

  // None, Void, Null, Bool, Int, Float, String, Object (plus Arithmetic, Any)
  enum class BasalBits {
    None = 0,
#define EGG_VM_BASAL_ENUM(name, text) name = 1 << impl::name,
    EGG_VM_BASAL(EGG_VM_BASAL_ENUM)
#undef EGG_VM_BASAL_ENUM
    Arithmetic = Int | Float,
    Any = Bool | Int | Float | String | Object
  };
  inline BasalBits operator|(BasalBits lhs, BasalBits rhs) {
    return Bits::set(lhs, rhs);
  }

  // None, Void, Null, Bool, Int, Float, String, Object, Pointer, Indirect, Break, Continue, Return, Yield, Throw, Hard (plus Arithmetic, Any, FlowControl)
  enum class VariantBits {
#define EGG_VM_VARIANT_ENUM(name, text) name = 1 << impl::name,
    EGG_VM_VARIANT(EGG_VM_VARIANT_ENUM)
#undef EGG_VM_VARIANT_ENUM
    Arithmetic = Int | Float,
    Any = Bool | Int | Float | String | Object,
    FlowControl = Break | Continue | Return | Yield | Throw
  };
#undef EGG_VM_BASAL_ENUM
  inline VariantBits operator|(VariantBits lhs, VariantBits rhs) {
    return Bits::set(lhs, rhs);
  }

  class VariantKind {
  protected:
    VariantBits kind;
  public:
    explicit VariantKind(VariantBits bits) : kind(bits) {
    }
    bool hasOne(VariantBits mask) const {
      return Bits::hasOneSet(this->kind, mask);
    }
    bool hasAny(VariantBits mask) const {
      return Bits::hasAnySet(this->kind, mask);
    }
    bool hasAll(VariantBits mask) const {
      return Bits::hasAllSet(this->kind, mask);
    }
    bool is(VariantBits value) const {
      return this->kind == value;
    }
    VariantBits getKind() const {
      return this->kind;
    }
    static void printTo(std::ostream& stream, VariantBits kind);
  protected:
    void swapKind(VariantKind& other) {
      std::swap(this->kind, other.kind);
    }
  };

  class IVariantSoft : public ICollectable {
  public:
    virtual Variant& getVariant() = 0;
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
      const IMemory* s; // String
      IObject* o; // Object
      IVariantSoft* p; // Pointer|Indirect
    } u;
    explicit Variant(VariantBits kind) : VariantKind(kind) {
      this->u.s = nullptr; // keep valgrind happy
    }
  public:
    // Construction/destruction
    Variant() : Variant(VariantBits::Void) {
    }
    Variant(const Variant& rhs) : VariantKind(rhs.kind) {
      Variant::copyInternals(*this, rhs);
    }
    Variant(Variant&& rhs) : VariantKind(rhs.kind) { // WIBBLE noexcept
      Variant::moveInternals(*this, rhs);
      rhs.kind = VariantBits::Void;
    }
    ~Variant() {
      Variant::destroyInternals(*this);
    }
    // Assignment
    Variant& operator=(const Variant& rhs) {
      if (this != &rhs) {
        // The resources of 'before' will be cleaned up after the assignment
        Variant before{ std::move(*this) };
        this->kind = rhs.kind;
        Variant::copyInternals(*this, rhs);
      }
      return *this;
    }
    Variant& operator=(Variant&& rhs) {
      // See https://stackoverflow.com/a/9322542
      if (this != &rhs) {
        // Need to make sure the resources of the original 'this' are cleaned up last
        this->swap(rhs);
        Variant::destroyInternals(rhs);
        rhs.kind = VariantBits::Void;
      }
      return *this;
    }
    // Equality
    bool operator==(const Variant& other) const {
      return Variant::equals(*this, other);
    }
    bool operator!=(const Variant& other) const {
      return !Variant::equals(*this, other);
    }
    // Null
    Variant(nullptr_t) : Variant(VariantBits::Null) {
    }
    // Bool
    Variant(bool value) : VariantKind(VariantBits::Bool) {
      this->u.b = value;
    }
    bool getBool() const {
      assert(this->hasAny(VariantBits::Bool));
      return this->u.b;
    }
    // Int (support automatic promotion of 32-bit integers)
    Variant(int32_t value) : VariantKind(VariantBits::Int) {
      this->u.i = value;
    }
    Variant(int64_t value) : VariantKind(VariantBits::Int) {
      this->u.i = value;
    }
    Int getInt() const {
      assert(this->hasAny(VariantBits::Int));
      return this->u.i;
    }
    // Float (support automatic promotion of 32-bit IEEE)
    Variant(float value) : VariantKind(VariantBits::Float) {
      this->u.f = value;
    }
    Variant(double value) : VariantKind(VariantBits::Float) {
      this->u.f = value;
    }
    Float getFloat() const {
      assert(this->hasAny(VariantBits::Float));
      return this->u.f;
    }
    // String
    Variant(const String& value) : VariantKind(VariantBits::String | VariantBits::Hard) {
      this->u.s = String::hardAcquire(value.get());
    }
    Variant(const std::string& value) : VariantKind(VariantBits::String | VariantBits::Hard) {
      // We've got to create a string without an allocator
      this->u.s = Variant::acquireFallbackString(value.data(), value.size());
    }
    Variant(const char* value) : VariantKind(VariantBits::String | VariantBits::Hard) {
      if (value == nullptr) {
        this->kind = VariantBits::Null;
        this->u.s = nullptr;
      } else {
        // We've got to create a string without an allocator
        this->u.s = Variant::acquireFallbackString(value, std::strlen(value));
      }
    }
    String getString() const {
      assert(this->hasAny(VariantBits::String));
      return String(this->u.s);
    }
    // Object
    Variant(const Object& value) : VariantKind(VariantBits::Object | VariantBits::Hard) {
      this->u.o = Object::hardAcquire(value.get());
      assert(this->u.o != nullptr);
    }
    Object getObject() const {
      assert(this->hasAny(VariantBits::Object));
      assert(this->u.o != nullptr);
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
      assert(this->u.p != nullptr);
    }
    Variant& getPointee() const {
      assert(this->hasOne(VariantBits::Pointer | VariantBits::Indirect));
      assert(this->u.p != nullptr);
      return this->u.p->getVariant();
    }
  private:
    void swap(Variant& other) {
      std::swap(this->kind, other.kind);
      std::swap(this->u, other.u);
    }
    static void copyInternals(Variant& dst, const Variant& src) {
      // dst:INVALID,src:VALID => dst:VALID,src:VALID
      assert(dst.kind == src.kind);
      if (src.hasAny(VariantBits::Hard)) {
        if (src.hasAny(VariantBits::Object)) {
          dst.u.o = Object::hardAcquire(src.u.o);
          return;
        }
        if (src.hasAny(VariantBits::String)) {
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
        } else if (dst.hasAny(VariantBits::String)) {
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


  public:
    // WIBBLE LEGACY
    static const Variant Void;
    static const Variant Null;
    static const Variant False;
    static const Variant True;
    static const Variant Break;
    static const Variant EmptyString;
    static const Variant Continue;
    static const Variant Rethrow;
    static const Variant ReturnVoid;
    // WIBBLE LEGACY
    static Variant builtinString(IAllocator& allocator);
    static Variant builtinType(IAllocator& allocator);
    static Variant builtinAssert(IAllocator& allocator);
    static Variant builtinPrint(IAllocator& allocator);
    template<typename T, typename... ARGS>
    static Variant makeObject(IAllocator& allocator, ARGS&&... args) {
      // Use perfect forwarding
      return Variant(Object(*allocator.make<T>(std::forward<ARGS>(args)...)));
    }
    // WIBBLE LEGACY
    static bool equals(const Variant& lhs, const Variant& rhs);
    ITypeRef getRuntimeType() const;
    String toString() const;
    bool isString() const { return this->is(VariantBits::String | VariantBits::Hard); }
    bool isNull() const { return this->is(VariantBits::Null); }
    bool isBool() const { return this->is(VariantBits::Bool); }
    bool isInt() const { return this->is(VariantBits::Int); }
    bool isFloat() const { return this->is(VariantBits::Float); }
    bool isVoid() const { return this->is(VariantBits::Void); }
    bool isBreak() const { return this->is(VariantBits::Break); }
    bool isContinue() const { return this->is(VariantBits::Continue); }
    bool isRethrow() const { return this->is(VariantBits::Throw | VariantBits::Void); }
    bool stripFlowControl(VariantBits bits);
    void addFlowControl(VariantBits bits);
    bool hasFlowControl() const { return this->hasAny(VariantBits::FlowControl); }
    bool hasNull() const { return this->hasAny(VariantBits::Null); }
    bool hasBool() const { return this->hasAny(VariantBits::Bool); }
    bool hasArithmetic() const { return this->hasAny(VariantBits::Arithmetic); }
    bool hasObject() const { return this->hasAny(VariantBits::Object); }
    bool hasThrow() const { return this->hasAny(VariantBits::Throw); }
    bool hasPointer() const { return this->hasAny(VariantBits::Pointer); }
    bool hasString() const { return this->hasAny(VariantBits::String); }
    bool hasIndirect() const { return this->hasAny(VariantBits::Indirect); }
    const Variant& direct() const;
    Variant& direct();
    void soften(IBasket& basket);
    void softVisitLink(const ICollectable::Visitor& visitor) const;
    void indirect(IAllocator& allocator, IBasket& basket);
    Variant address() const;
    static std::string getBasalString(BasalBits basal);
  };
}
