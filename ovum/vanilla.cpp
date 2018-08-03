#include "ovum/ovum.h"

namespace {
  using namespace egg::ovum;

  class VanillaBase : public HardReferenceCounted<IObject> {
    VanillaBase(const VanillaBase&) = delete;
    VanillaBase& operator=(const VanillaBase&) = delete;
  private:
  public:
    VanillaBase(IAllocator& allocator)
      : HardReferenceCounted(allocator) {
    }
  };

  class VanillaObject : public VanillaBase {
    VanillaObject(const VanillaObject&) = delete;
    VanillaObject& operator=(const VanillaObject&) = delete;
  private:
  public:
    VanillaObject(IAllocator& allocator)
      : VanillaBase(allocator) {
    }
    virtual void visitSoftLinks(const ICollectable&, const ICollectable&) const override {
      // WIBBLE
    }
  };
}

egg::ovum::Object egg::ovum::ObjectFactory::createVanillaObject(egg::ovum::IAllocator& allocator) {
  return Object(*allocator.create<VanillaObject>(0, allocator));
}
