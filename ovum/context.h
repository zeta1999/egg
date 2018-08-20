// WIBBLE retire
namespace egg::lang {
  class ValueLegacy;
}

namespace egg::ovum {
  struct LocationSource {
    String file;
    size_t line;
    size_t column;

    inline LocationSource(const LocationSource& rhs)
      : file(rhs.file), line(rhs.line), column(rhs.column) {
    }
    inline LocationSource(const String& file, size_t line, size_t column)
      : file(file), line(line), column(column) {
    }
    String toSourceString() const;
  };

  struct LocationRuntime : public LocationSource {
    String function;
    const LocationRuntime* parent;

    inline LocationRuntime(const LocationRuntime& rhs)
      : LocationSource(rhs), function(rhs.function), parent(rhs.parent) {
    }
    inline LocationRuntime(const LocationSource& source, const String& function, const LocationRuntime* parent = nullptr)
      : LocationSource(source), function(function), parent(parent) {
    }
    String toRuntimeString() const;
  };

  class IPreparation {
  public:
    virtual ~IPreparation() {}
    virtual void raise(ILogger::Severity severity, const String& message) = 0;

    // Useful helpers
    template<typename... ARGS>
    void raiseWarning(ARGS... args);
    template<typename... ARGS>
    void raiseError(ARGS... args);
  };

  class IExecution {
  public:
    virtual ~IExecution() {}
    virtual IAllocator& getAllocator() const = 0;
    virtual egg::lang::ValueLegacy raise(const String& message) = 0;
    virtual egg::lang::ValueLegacy assertion(const egg::lang::ValueLegacy& predicate) = 0;
    virtual void print(const std::string& utf8) = 0;

    // Useful helper
    template<typename... ARGS>
    egg::lang::ValueLegacy raiseFormat(ARGS... args);
  };
}

template<typename... ARGS>
inline void egg::ovum::IPreparation::raiseWarning(ARGS... args) {
  auto message = StringBuilder::concat(args...);
  this->raise(ILogger::Severity::Warning, message);
}

template<typename... ARGS>
inline void egg::ovum::IPreparation::raiseError(ARGS... args) {
  auto message = StringBuilder::concat(args...);
  this->raise(ILogger::Severity::Error, message);
}

template<typename... ARGS>
inline egg::lang::ValueLegacy egg::ovum::IExecution::raiseFormat(ARGS... args) {
  auto message = StringBuilder::concat(args...);
  return this->raise(message);
}