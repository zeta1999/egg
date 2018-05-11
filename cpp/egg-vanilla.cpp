#include "yolk.h"
#include "lexers.h"
#include "egg-tokenizer.h"
#include "egg-syntax.h"
#include "egg-parser.h"
#include "egg-engine.h"
#include "egg-program.h"

namespace {
  class VanillaBase : public egg::gc::HardReferenceCounted<egg::lang::IObject> {
    EGG_NO_COPY(VanillaBase);
  protected:
    std::string kind;
    egg::lang::ITypeRef type;
  public:
    VanillaBase(const std::string& kind, const egg::lang::IType& type)
      : HardReferenceCounted(0), kind(kind), type(&type) {
    }
    virtual bool dispose() override {
      return false;
    }
    virtual egg::lang::Value getRuntimeType() const override {
      return egg::lang::Value(*this->type);
    }
    virtual egg::lang::Value call(egg::lang::IExecution& execution, const egg::lang::IParameters&) override {
      return execution.raiseFormat(this->kind, "s do not support calling with '()'");
    }
    virtual egg::lang::Value getIndex(egg::lang::IExecution& execution, const egg::lang::Value& index) override {
      if (!index.is(egg::lang::Discriminator::String)) {
        return execution.raiseFormat(this->kind, " index (property name) was expected to be 'string', not '", index.getRuntimeType().toString(), "'");
      }
      return this->getProperty(execution, index.getString());
    }
    virtual egg::lang::Value setIndex(egg::lang::IExecution& execution, const egg::lang::Value& index, const egg::lang::Value& value) override {
      if (!index.is(egg::lang::Discriminator::String)) {
        return execution.raiseFormat(this->kind, " index (property name) was expected to be 'string', not '", index.getRuntimeType().toString(), "'");
      }
      return this->setProperty(execution, index.getString(), value);
    }
  };

  class VanillaIteratorBase : public egg::gc::NotReferenceCounted<egg::lang::IType> {
  public:
    virtual egg::lang::String toString() const override {
      return egg::lang::String::fromUTF8("<iterator>");
    }
    virtual egg::lang::Value promoteAssignment(egg::lang::IExecution& execution, const egg::lang::Value&) const override {
      return execution.raiseFormat("Cannot re-assign iterators"); // TODO
    }
  };

  class VanillaKeyValueType : public egg::gc::NotReferenceCounted<egg::lang::IType> {
  public:
    virtual egg::lang::String toString() const override {
      return egg::lang::String::fromUTF8("<keyvalue>");
    }
    virtual egg::lang::Value promoteAssignment(egg::lang::IExecution& execution, const egg::lang::Value&) const override {
      return execution.raiseFormat("Cannot re-assign key-values"); // TODO
    }
  };
  const VanillaKeyValueType typeVanillaKeyValue;

  class VanillaKeyValue : public VanillaBase {
    EGG_NO_COPY(VanillaKeyValue);
  private:
    egg::lang::Value key;
    egg::lang::Value value;
  public:
    VanillaKeyValue(const egg::lang::Value& key, const egg::lang::Value& value)
      : VanillaBase("Key-value", typeVanillaKeyValue), key(key), value(value) {
    }
    explicit VanillaKeyValue(const std::pair<egg::lang::String, egg::lang::Value>& keyvalue)
      : VanillaKeyValue(egg::lang::Value{ keyvalue.first }, keyvalue.second) {
    }
    virtual egg::lang::Value toString() const override {
      return egg::lang::Value{ egg::lang::String::concat("{key:", this->key.toString(), ",value:", this->value.toString(), "}") };
    }
    virtual egg::lang::Value getProperty(egg::lang::IExecution& execution, const egg::lang::String& property) override {
      auto name = property.toUTF8();
      if (name == "key") {
        return this->key;
      }
      if (name == "value") {
        return this->value;
      }
      return execution.raiseFormat("Key-values do not support property: '.", property, "'");
    }
    virtual egg::lang::Value setProperty(egg::lang::IExecution& execution, const egg::lang::String& property, const egg::lang::Value&) override {
      return execution.raiseFormat("Key-values do not support addition or modification of properties: '.", property, "'");
    }
    virtual egg::lang::Value iterate(egg::lang::IExecution& execution) override {
      return execution.raiseFormat("Key-values do not support iteration");
    }
  };

  class VanillaArrayType : public egg::gc::NotReferenceCounted<egg::lang::IType> {
  public:
    virtual egg::lang::String toString() const override {
      return egg::lang::String::fromUTF8("any?[]");
    }
    virtual egg::lang::Value promoteAssignment(egg::lang::IExecution& execution, const egg::lang::Value&) const override {
      return execution.raiseFormat("Cannot re-assign arrays"); // TODO
    }
  };
  const VanillaArrayType typeVanillaArray;

  class VanillaArray : public VanillaBase {
    EGG_NO_COPY(VanillaArray);
  private:
    std::vector<egg::lang::Value> values;
  public:
    VanillaArray()
      : VanillaBase("Array", typeVanillaArray) {
    }
    virtual egg::lang::Value toString() const override {
      if (this->values.empty()) {
        return egg::lang::Value{ egg::lang::String::fromUTF8("[]") };
      }
      egg::lang::StringBuilder sb;
      const char* between = "[";
      for (auto& value : this->values) {
        sb.add(between).add(value.toUTF8());
        between = ",";
      }
      sb.add("]");
      return egg::lang::Value{ sb.str() };
    }
    virtual egg::lang::Value getProperty(egg::lang::IExecution& execution, const egg::lang::String& property) override {
      auto name = property.toUTF8();
      if (name == "length") {
        return egg::lang::Value{ int64_t(this->values.size()) };
      }
      return execution.raiseFormat("Arrays do not support property '.", property, "'");
    }
    virtual egg::lang::Value setProperty(egg::lang::IExecution& execution, const egg::lang::String& property, const egg::lang::Value& value) override {
      auto name = property.toUTF8();
      if (name == "length") {
        return this->setLength(execution, value);
      }
      return execution.raiseFormat("Arrays do not support property '.", property, "'");
    }
    virtual egg::lang::Value getIndex(egg::lang::IExecution& execution, const egg::lang::Value& index) override {
      if (!index.is(egg::lang::Discriminator::Int)) {
        return execution.raiseFormat("Array index was expected to be 'int', not '", index.getRuntimeType().toString(), "'");
      }
      auto i = index.getInt();
      if ((i < 0) || (uint64_t(i) >= uint64_t(this->values.size()))) {
        return execution.raiseFormat("Invalid array index for an array with ", this->values.size(), " element(s): ", i);
      }
      auto& element = this->values.at(size_t(i));
      assert(!element.is(egg::lang::Discriminator::Void));
      return element;
    }
    virtual egg::lang::Value setIndex(egg::lang::IExecution& execution, const egg::lang::Value& index, const egg::lang::Value& value) override {
      if (!index.is(egg::lang::Discriminator::Int)) {
        return execution.raiseFormat("Array index was expected to be 'int', not '", index.getRuntimeType().toString(), "'");
      }
      auto i = index.getInt();
      if ((i < 0) || (i >= 0x7FFFFFFF)) {
        return execution.raiseFormat("Invalid array index: ", i);
      }
      auto u = size_t(i);
      if (u >= this->values.size()) {
        this->values.resize(u + 1, egg::lang::Value::Null);
      }
      this->values[u] = value;
      return egg::lang::Value::Void;
    }
    virtual egg::lang::Value iterate(egg::lang::IExecution& execution) override;
    egg::lang::Value iterateNext(size_t& index) const {
      // Used by VanillaArrayIterator
      // TODO What if the array has been modified?
      if (index < this->values.size()) {
        return this->values.at(index++);
      }
      return egg::lang::Value::Void;
    }
  private:
    egg::lang::Value setLength(egg::lang::IExecution& execution, const egg::lang::Value& value) {
      if (!value.is(egg::lang::Discriminator::Int)) {
        return execution.raiseFormat("Array length was expected to be set to an 'int', not '", value.getRuntimeType().toString(), "'");
      }
      auto n = value.getInt();
      if ((n < 0) || (n >= 0x7FFFFFFF)) {
        return execution.raiseFormat("Invalid array length: ", n);
      }
      auto u = size_t(n);
      this->values.resize(u, egg::lang::Value::Null);
      return egg::lang::Value::Void;
    }
  };

  const VanillaIteratorBase typeVanillaArrayIterator;

  class VanillaArrayIterator : public VanillaBase {
    EGG_NO_COPY(VanillaArrayIterator);
  private:
    egg::gc::HardRef<VanillaArray> array;
    size_t next;
  public:
    VanillaArrayIterator(egg::lang::IExecution&, const VanillaArray& array)
      : VanillaBase("Iterator", typeVanillaArrayIterator), array(&array), next(0) {
    }
    virtual egg::lang::Value toString() const override {
      return egg::lang::Value{ this->type->toString() };
    }
    virtual egg::lang::Value call(egg::lang::IExecution&, const egg::lang::IParameters&) override {
      // TODO check parameters?
      return this->array->iterateNext(this->next);
    }
    virtual egg::lang::Value getProperty(egg::lang::IExecution& execution, const egg::lang::String& property) override {
      return execution.raiseFormat("Iterators do not support properties: '.", property, "'");
    }
    virtual egg::lang::Value setProperty(egg::lang::IExecution& execution, const egg::lang::String& property, const egg::lang::Value&) override {
      return execution.raiseFormat("Iterators do not support properties: '.", property, "'");
    }
    virtual egg::lang::Value iterate(egg::lang::IExecution& execution) override {
      return execution.raiseFormat("Iterators do not support iteration"); // TODO forEachRemaining?
    }
  };

  egg::lang::Value VanillaArray::iterate(egg::lang::IExecution& execution) {
    return egg::lang::Value::make<VanillaArrayIterator>(execution, *this);
  }

  class VanillaDictionaryIteratorType : public VanillaIteratorBase {
  public:
    virtual const egg::lang::ISignature* callable() const override {
      return nullptr; // WIBBLE why isn't this called?
    }
  };
  const VanillaDictionaryIteratorType typeVanillaDictionaryIterator;

  class VanillaDictionaryIterator : public VanillaBase {
    EGG_NO_COPY(VanillaDictionaryIterator);
  private:
    typedef egg::yolk::Dictionary<egg::lang::String, egg::lang::Value> Dictionary;
    Dictionary::KeyValues keyvalues;
    size_t next;
  public:
    VanillaDictionaryIterator(egg::lang::IExecution&, const Dictionary& dictionary)
      : VanillaBase("Iterator", typeVanillaDictionaryIterator), next(0) {
      (void)dictionary.getKeyValues(this->keyvalues);
    }
    virtual egg::lang::Value toString() const override {
      return egg::lang::Value{ this->type->toString() };
    }
    virtual egg::lang::Value call(egg::lang::IExecution&, const egg::lang::IParameters&) override {
      // TODO check parameters?
      if (this->next < this->keyvalues.size()) {
        return egg::lang::Value::make<VanillaKeyValue>(keyvalues[this->next++]);
      }
      return egg::lang::Value::Void;
    }
    virtual egg::lang::Value getProperty(egg::lang::IExecution& execution, const egg::lang::String& property) override {
      return execution.raiseFormat("Iterators do not support properties: '.", property, "'");
    }
    virtual egg::lang::Value setProperty(egg::lang::IExecution& execution, const egg::lang::String& property, const egg::lang::Value&) override {
      return execution.raiseFormat("Iterators do not support properties: '.", property, "'");
    }
    virtual egg::lang::Value iterate(egg::lang::IExecution& execution) override {
      return execution.raiseFormat("Iterators do not support iteration");
    }
  };

  class VanillaDictionary : public VanillaBase {
    EGG_NO_COPY(VanillaDictionary);
  protected:
    typedef egg::yolk::Dictionary<egg::lang::String, egg::lang::Value> Dictionary;
    Dictionary dictionary;
  public:
    VanillaDictionary(const std::string& kind, const egg::lang::IType& type)
      : VanillaBase(kind, type) {
    }
    virtual egg::lang::Value toString() const override {
      Dictionary::KeyValues keyvalues;
      if (this->dictionary.getKeyValues(keyvalues) == 0) {
        return egg::lang::Value{ egg::lang::String::fromUTF8("{}") };
      }
      egg::lang::StringBuilder sb;
      const char* between = "{";
      for (auto& keyvalue : keyvalues) {
        sb.add(between).add(keyvalue.first.toUTF8()).add(":").add(keyvalue.second.toUTF8());
        between = ",";
      }
      sb.add("}");
      return egg::lang::Value{ sb.str() };
    }
    virtual egg::lang::Value getProperty(egg::lang::IExecution& execution, const egg::lang::String& property) override {
      egg::lang::Value value;
      if (this->dictionary.tryGet(property, value)) {
        return value;
      }
      return execution.raiseFormat(this->kind, " does not support property '", property, "'");
    }
    virtual egg::lang::Value setProperty(egg::lang::IExecution&, const egg::lang::String& property, const egg::lang::Value& value) override {
      (void)this->dictionary.addOrUpdate(property, value);
      return egg::lang::Value::Void;
    }
    virtual egg::lang::Value iterate(egg::lang::IExecution& execution) override {
      return egg::lang::Value::make<VanillaDictionaryIterator>(execution, this->dictionary);
    }
  };

  class VanillaObjectType : public egg::gc::NotReferenceCounted<egg::lang::IType> {
  public:
    virtual egg::lang::String toString() const override {
      return egg::lang::String::fromUTF8("any?{string}");
    }
    virtual egg::lang::Value promoteAssignment(egg::lang::IExecution& execution, const egg::lang::Value&) const override {
      return execution.raiseFormat("Cannot re-assign objects"); // TODO
    }
  };
  const VanillaObjectType typeVanillaObject;

  class VanillaObject : public VanillaDictionary {
    EGG_NO_COPY(VanillaObject);
  public:
    VanillaObject()
      : VanillaDictionary("Object", typeVanillaObject) {
    }
  };

  class VanillaExceptionType : public egg::gc::NotReferenceCounted<egg::lang::IType> {
  public:
    virtual egg::lang::String toString() const override {
      return egg::lang::String::fromUTF8("<exception>");
    }
    virtual egg::lang::Value promoteAssignment(egg::lang::IExecution& execution, const egg::lang::Value&) const override {
      return execution.raiseFormat("Cannot re-assign exceptions");
    }
  };
  const VanillaExceptionType typeVanillaException{};

  class VanillaException : public VanillaDictionary {
    EGG_NO_COPY(VanillaException);
  private:
    static const egg::lang::String keyMessage;
    static const egg::lang::String keyLocation;
  public:
    explicit VanillaException(const egg::lang::LocationRuntime& location, const egg::lang::String& message)
      : VanillaDictionary("Exception", typeVanillaException) {
      this->dictionary.addOrUpdate(keyMessage, egg::lang::Value{ message });
      this->dictionary.addOrUpdate(keyLocation, egg::lang::Value{ location.toSourceString() }); // TODO use toRuntimeString
    }
    virtual egg::lang::Value toString() const override {
      egg::lang::StringBuilder sb;
      egg::lang::Value value;
      if (this->dictionary.tryGet(keyLocation, value)) {
        sb.add(value.toString()).add(": ");
      }
      if (this->dictionary.tryGet(keyMessage, value)) {
        sb.add(value.toString());
      } else {
        sb.add("Exception (no message)");
      }
      return egg::lang::Value{ sb.str() };
    }
  };
  const egg::lang::String VanillaException::keyMessage = egg::lang::String::fromUTF8("message");
  const egg::lang::String VanillaException::keyLocation = egg::lang::String::fromUTF8("location");
}

// Vanilla types
const egg::lang::IType& egg::yolk::EggProgram::VanillaArray = typeVanillaArray;
const egg::lang::IType& egg::yolk::EggProgram::VanillaObject = typeVanillaObject;
const egg::lang::IType& egg::yolk::EggProgram::VanillaException = typeVanillaException;

egg::lang::Value egg::yolk::EggProgramContext::raise(const egg::lang::String& message) {
  auto exception = egg::lang::Value::make<VanillaException>(this->location, message);
  exception.addFlowControl(egg::lang::Discriminator::Exception);
  return exception;
}

egg::lang::Value egg::yolk::EggProgramContext::createVanillaArray() {
  return egg::lang::Value::make<VanillaArray>();
}

egg::lang::Value egg::yolk::EggProgramContext::createVanillaObject() {
  return egg::lang::Value::make<VanillaObject>();
}
