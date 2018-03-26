#include "yolk.h"
#include "lexers.h"
#include "egg-tokenizer.h"
#include "egg-syntax.h"
#include "egg-parser.h"

namespace {
  using namespace egg::yolk;

  // This is the integer representation of the enum bit-mask
  typedef std::underlying_type<EggParserAllowed>::type EggParserAllowedUnderlying;
  inline EggParserAllowed operator|(EggParserAllowed lhs, EggParserAllowed rhs) {
    return static_cast<EggParserAllowed>(static_cast<EggParserAllowedUnderlying>(lhs) | static_cast<EggParserAllowedUnderlying>(rhs));
  }

  // Constants
  const EggParserType typeVoid{ egg::lang::TypeStorage::Void };
  const EggParserType typeBool{ egg::lang::TypeStorage::Bool };
  const EggParserType typeInt{ egg::lang::TypeStorage::Int };
  const EggParserType typeFloat{ egg::lang::TypeStorage::Float };
  const EggParserType typeString{ egg::lang::TypeStorage::String };

  SyntaxException exceptionFromLocation(const IEggParserContext& context, const std::string& reason, const EggSyntaxNodeBase& node) {
    return SyntaxException(reason, context.getResource(), node);
  }

  SyntaxException exceptionFromToken(const IEggParserContext& context, const std::string& reason, const EggSyntaxNodeBase& node) {
    auto token = node.token();
    return SyntaxException(reason + ": '" + token, context.getResource(), node, token);
  }

  void tagToStringComponent(std::string& dst, const char* text, bool bit) {
    if (bit) {
      if (!dst.empty()) {
        dst.append("|");
      }
      dst.append(text);
    }
  }

  class ParserDump {
  private:
    std::ostream* os;
  public:
    ParserDump(std::ostream& os, const char* text)
      :os(&os) {
      *this->os << '(' << text;
    }
    ~ParserDump() {
      *this->os << ')';
    }
    ParserDump& add(const std::string& text) {
      *this->os << ' ' << '\'' << text << '\'';
      return *this;
    }
    ParserDump& add(const std::shared_ptr<IEggParserNode>& child) {
      if (child != nullptr) {
        *this->os << ' ';
        child->dump(*this->os);
      } else {
        *this->os << ' ' << '-';
      }
      return *this;
    }
    ParserDump& add(const std::vector<std::shared_ptr<IEggParserNode>>& children) {
      for (auto& child : children) {
        this->add(child);
      }
      return *this;
    }
    template<size_t N>
    ParserDump& add(const std::shared_ptr<IEggParserNode>(&children)[N]) {
      // TODO remove?
      for (auto& child : children) {
        this->add(child);
      }
      return *this;
    }
  };

  class EggParserNodeBase : public IEggParserNode {
  public:
    virtual ~EggParserNodeBase() {
    }
    virtual const EggParserType& getType() const override {
      return typeVoid;
    }
    static std::string unaryToString(EggParserUnary op);
    static std::string binaryToString(EggParserBinary op);
    static std::string assignToString(EggParserAssign op);
    static std::string mutateToString(EggParserMutate op);
  };

  class EggParserNode_Module : public EggParserNodeBase {
  private:
    std::vector<std::shared_ptr<IEggParserNode>> child;
  public:
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "module").add(this->child);
    }
    void addChild(const std::shared_ptr<IEggParserNode>& statement) {
      assert(statement != nullptr);
      this->child.push_back(statement);
    }
  };

  class EggParserNode_Block : public EggParserNodeBase {
  private:
    std::vector<std::shared_ptr<IEggParserNode>> child;
  public:
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "block").add(this->child);
    }
    void addChild(const std::shared_ptr<IEggParserNode>& statement) {
      assert(statement != nullptr);
      this->child.push_back(statement);
    }
  };

  class EggParserNode_Type : public EggParserNodeBase {
  private:
    EggParserType type;
  public:
    explicit EggParserNode_Type(EggParserType type)
      : type(type) {
    }
    virtual const EggParserType& getType() const override {
      return this->type;
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "type").add(this->type.tagToString());
    }
  };

  class EggParserNode_Declare : public EggParserNodeBase {
  private:
    std::string name;
    std::shared_ptr<IEggParserNode> type;
    std::shared_ptr<IEggParserNode> init;
  public:
    EggParserNode_Declare(const std::string& name, const std::shared_ptr<IEggParserNode>& type, const std::shared_ptr<IEggParserNode>& init = nullptr)
      : name(name), type(type), init(init) {
      assert(type != nullptr);
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "declare").add(this->name).add(this->type).add(this->init);
    }
  };

  class EggParserNode_Set : public EggParserNodeBase {
  private:
    EggParserAssign op;
    std::shared_ptr<IEggParserNode> lhs;
    std::shared_ptr<IEggParserNode> rhs;
  public:
    EggParserNode_Set(EggParserAssign op, const std::shared_ptr<IEggParserNode>& lhs, const std::shared_ptr<IEggParserNode>& rhs)
      : op(op), lhs(lhs), rhs(rhs) {
      assert(lhs != nullptr);
      assert(rhs != nullptr);
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "set").add(EggParserNodeBase::assignToString(this->op)).add(this->lhs).add(this->rhs);
    }
  };

  class EggParserNode_Mutate : public EggParserNodeBase {
  private:
    EggParserMutate op;
    std::shared_ptr<IEggParserNode> lvalue;
  public:
    EggParserNode_Mutate(EggParserMutate op, const std::shared_ptr<IEggParserNode>& lvalue)
      : op(op), lvalue(lvalue) {
      assert(lvalue != nullptr);
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "mutate").add(EggParserNodeBase::mutateToString(this->op)).add(this->lvalue);
    }
  };

  class EggParserNode_Break : public EggParserNodeBase {
  public:
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "break");
    }
  };

  class EggParserNode_Catch : public EggParserNodeBase {
  private:
    std::string name;
    std::shared_ptr<IEggParserNode> type;
    std::shared_ptr<IEggParserNode> block;
  public:
    EggParserNode_Catch(const std::string& name, const std::shared_ptr<IEggParserNode>& type, const std::shared_ptr<IEggParserNode>& block)
      : name(name), type(type), block(block) {
      assert(type != nullptr);
      assert(block != nullptr);
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "catch").add(this->name).add(this->type).add(this->block);
    }
  };

  class EggParserNode_Continue : public EggParserNodeBase {
  public:
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "continue");
    }
  };

  class EggParserNode_Do : public EggParserNodeBase {
  private:
    std::shared_ptr<IEggParserNode> condition;
    std::shared_ptr<IEggParserNode> block;
  public:
    EggParserNode_Do(const std::shared_ptr<IEggParserNode>& condition, const std::shared_ptr<IEggParserNode>& block)
      : condition(condition), block(block) {
      assert(condition != nullptr);
      assert(block != nullptr);
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "do").add(this->condition).add(this->block);
    }
  };

  class EggParserNode_If : public EggParserNodeBase {
  private:
    std::shared_ptr<IEggParserNode> condition;
    std::shared_ptr<IEggParserNode> trueBlock;
    std::shared_ptr<IEggParserNode> falseBlock;
  public:
    EggParserNode_If(const std::shared_ptr<IEggParserNode>& condition, const std::shared_ptr<IEggParserNode>& trueBlock, const std::shared_ptr<IEggParserNode>& falseBlock)
      : condition(condition), trueBlock(trueBlock), falseBlock(falseBlock) {
      assert(condition != nullptr);
      assert(trueBlock != nullptr);
      // falseBlock may be null if the 'else' clause is missing
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "if").add(this->condition).add(this->trueBlock).add(this->falseBlock);
    }
  };

  class EggParserNode_For : public EggParserNodeBase {
  private:
    std::shared_ptr<IEggParserNode> pre;
    std::shared_ptr<IEggParserNode> cond;
    std::shared_ptr<IEggParserNode> post;
    std::shared_ptr<IEggParserNode> block;
  public:
    EggParserNode_For(const std::shared_ptr<IEggParserNode>& pre, const std::shared_ptr<IEggParserNode>& cond, const std::shared_ptr<IEggParserNode>& post, const std::shared_ptr<IEggParserNode>& block)
      : pre(pre), cond(cond), post(post), block(block) {
      // pre/cond/post may be null
      assert(block != nullptr);
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "for").add(this->pre).add(this->cond).add(this->post).add(this->block);
    }
  };

  class EggParserNode_Foreach : public EggParserNodeBase {
  private:
    std::shared_ptr<IEggParserNode> target;
    std::shared_ptr<IEggParserNode> expr;
    std::shared_ptr<IEggParserNode> block;
  public:
    EggParserNode_Foreach(const std::shared_ptr<IEggParserNode>& target, const std::shared_ptr<IEggParserNode>& expr, const std::shared_ptr<IEggParserNode>& block)
      : target(target), expr(expr), block(block) {
      assert(target != nullptr);
      assert(expr != nullptr);
      assert(block != nullptr);
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "foreach").add(this->target).add(this->expr).add(this->block);
    }
  };

  class EggParserNode_Return : public EggParserNodeBase {
  private:
    std::vector<std::shared_ptr<IEggParserNode>> child;
  public:
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "return").add(this->child);
    }
    void addChild(const std::shared_ptr<IEggParserNode>& value) {
      assert(value != nullptr);
      this->child.push_back(value);
    }
  };

  class EggParserNode_Identifier : public EggParserNodeBase {
  private:
    std::string name;
  public:
    explicit EggParserNode_Identifier(const std::string& name)
      : name(name) {
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "identifier").add(this->name);
    }
  };

  class EggParserNode_LiteralInteger : public EggParserNodeBase {
  private:
    int64_t value;
  public:
    explicit EggParserNode_LiteralInteger(int64_t value)
      : value(value) {
    }
    virtual const EggParserType& getType() const override {
      return typeInt;
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "literal int").add(std::to_string(this->value));
    }
  };

  class EggParserNode_LiteralFloat : public EggParserNodeBase {
  private:
    double value;
  public:
    explicit EggParserNode_LiteralFloat(double value)
      : value(value) {
    }
    virtual const EggParserType& getType() const override {
      return typeFloat;
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "literal float").add(std::to_string(this->value));
    }
  };

  class EggParserNode_LiteralString : public EggParserNodeBase {
  private:
    std::string value;
  public:
    explicit EggParserNode_LiteralString(const std::string& value)
      : value(value) {
    }
    virtual const EggParserType& getType() const override {
      return typeString;
    }
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "literal string").add(this->value);
    }
  };

  class EggParserNode_Unary : public EggParserNodeBase {
  protected:
    EggParserUnary op;
    std::shared_ptr<IEggParserNode> expr;
    EggParserNode_Unary(EggParserUnary op, const std::shared_ptr<IEggParserNode>& expr)
      : op(op), expr(expr) {
      assert(expr != nullptr);
    }
  public:
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "unary").add(EggParserNodeBase::unaryToString(this->op)).add(this->expr);
    }
  };

#define EGG_PARSER_UNARY_OPERATOR_DEFINE(name, text) \
  class EggParserNode_Unary##name : public EggParserNode_Unary { \
  public: \
    EggParserNode_Unary##name(const std::shared_ptr<IEggParserNode>& expr) \
      : EggParserNode_Unary(EggParserUnary::name, expr) { \
    } \
  };
  EGG_PARSER_UNARY_OPERATORS(EGG_PARSER_UNARY_OPERATOR_DEFINE)

  class EggParserNode_Binary : public EggParserNodeBase {
  protected:
    EggParserBinary op;
    std::shared_ptr<IEggParserNode> lhs;
    std::shared_ptr<IEggParserNode> rhs;
    EggParserNode_Binary(EggParserBinary op, const std::shared_ptr<IEggParserNode>& lhs, const std::shared_ptr<IEggParserNode>& rhs)
      : op(op), lhs(lhs), rhs(rhs) {
      assert(lhs != nullptr);
      assert(rhs != nullptr);
    }
  public:
    virtual void dump(std::ostream & os) const override {
      ParserDump(os, "binary").add(EggParserNodeBase::binaryToString(this->op)).add(this->lhs).add(this->rhs);
    }
  };

#define EGG_PARSER_BINARY_OPERATOR_DEFINE(name, text) \
  class EggParserNode_Binary##name : public EggParserNode_Binary { \
  public: \
    EggParserNode_Binary##name(const std::shared_ptr<IEggParserNode>& lhs, const std::shared_ptr<IEggParserNode>& rhs) \
      : EggParserNode_Binary(EggParserBinary::name, lhs, rhs) { \
    } \
  };
  EGG_PARSER_BINARY_OPERATORS(EGG_PARSER_BINARY_OPERATOR_DEFINE)

  class EggParserContextBase : public IEggParserContext {
  private:
    EggParserAllowedUnderlying allowed;
  public:
    explicit EggParserContextBase(EggParserAllowed allowed)
      : allowed(static_cast<EggParserAllowedUnderlying>(allowed)) {
    }
    virtual ~EggParserContextBase() {
    }
    virtual bool isAllowed(EggParserAllowed bit) const override {
      return (this->allowed & static_cast<EggParserAllowedUnderlying>(bit)) != 0;
    }
    virtual EggParserAllowed inheritAllowed(EggParserAllowed allow, EggParserAllowed inherit) const {
      auto inherited = this->allowed & static_cast<EggParserAllowedUnderlying>(inherit);
      return static_cast<EggParserAllowed>(inherited | static_cast<EggParserAllowedUnderlying>(allow));
    }
  };


  class EggParserContext : public EggParserContextBase {
  private:
    std::string resource;
  public:
    explicit EggParserContext(const std::string& resource, EggParserAllowed allowed = EggParserAllowed::None)
      : EggParserContextBase(allowed), resource(resource) {
    }
    virtual std::string getResource() const {
      return this->resource;
    }
  };

  class EggParserContextNested : public EggParserContextBase {
  private:
    IEggParserContext* parent;
  public:
    EggParserContextNested(IEggParserContext& parent, EggParserAllowed allowed, EggParserAllowed inherited = EggParserAllowed::None)
      : EggParserContextBase(parent.inheritAllowed(allowed, inherited)), parent(&parent) {
      assert(this->parent != nullptr);
    }
    virtual std::string getResource() const {
      return this->parent->getResource();
    }
  };

  class EggParserModule : public IEggParser {
  public:
    virtual std::shared_ptr<IEggParserNode> parse(IEggTokenizer& tokenizer) override {
      auto syntax = EggParserFactory::createModuleSyntaxParser();
      auto ast = syntax->parse(tokenizer);
      EggParserContext context(tokenizer.resource());
      return ast->promote(context);
    }
  };
}

std::string egg::yolk::EggParserType::tagToString(Tag tag) {
  using egg::lang::TypeStorage;
  if (tag == TypeStorage::Inferred) {
    return "var";
  }
  if (tag == TypeStorage::Void) {
    return "void";
  }
  if (tag == TypeStorage::Any) {
    return "any";
  }
  if (tag == (TypeStorage::Null | TypeStorage::Any)) {
    return "any?";
  }
  std::string result;
  tagToStringComponent(result, "bool", tag.hasBit(TypeStorage::Bool));
  tagToStringComponent(result, "int", tag.hasBit(TypeStorage::Int));
  tagToStringComponent(result, "float", tag.hasBit(TypeStorage::Float));
  tagToStringComponent(result, "string", tag.hasBit(TypeStorage::String));
  tagToStringComponent(result, "type", tag.hasBit(TypeStorage::Type));
  tagToStringComponent(result, "object", tag.hasBit(TypeStorage::Object));
  if (tag.hasBit(TypeStorage::Void)) {
    result.append("?");
  }
  return result;
}

#define EGG_PARSER_OPERATOR_STRING(name, text) text,

std::string EggParserNodeBase::unaryToString(egg::yolk::EggParserUnary op) {
  static const char* const table[] = {
    EGG_PARSER_UNARY_OPERATORS(EGG_PARSER_OPERATOR_STRING)
  };
  auto index = static_cast<size_t>(op);
  assert(index < EGG_NELEMS(table));
  return table[index];
}

std::string EggParserNodeBase::binaryToString(egg::yolk::EggParserBinary op) {
  static const char* const table[] = {
    EGG_PARSER_BINARY_OPERATORS(EGG_PARSER_OPERATOR_STRING)
  };
  auto index = static_cast<size_t>(op);
  assert(index < EGG_NELEMS(table));
  return table[index];
}

std::string EggParserNodeBase::assignToString(egg::yolk::EggParserAssign op) {
  static const char* const table[] = {
    EGG_PARSER_ASSIGN_OPERATORS(EGG_PARSER_OPERATOR_STRING)
  };
  auto index = static_cast<size_t>(op);
  assert(index < EGG_NELEMS(table));
  return table[index];
}

std::string EggParserNodeBase::mutateToString(egg::yolk::EggParserMutate op) {
  static const char* const table[] = {
    EGG_PARSER_MUTATE_OPERATORS(EGG_PARSER_OPERATOR_STRING)
  };
  auto index = static_cast<size_t>(op);
  assert(index < EGG_NELEMS(table));
  return table[index];
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Empty::promote(egg::yolk::IEggParserContext& context) {
  if (!context.isAllowed(EggParserAllowed::Empty)) {
    throw exceptionFromLocation(context, "Empty statements are not permitted in this context", *this);
  }
  return nullptr;
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Module::promote(egg::yolk::IEggParserContext& context) {
  auto module = std::make_shared<EggParserNode_Module>();
  for (auto& statement : this->child) {
    module->addChild(statement->promote(context));
  }
  return module;
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Block::promote(egg::yolk::IEggParserContext& context) {
  auto module = std::make_shared<EggParserNode_Block>();
  for (auto& statement : this->child) {
    module->addChild(statement->promote(context));
  }
  return module;
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Type::promote(egg::yolk::IEggParserContext&) {
  auto type = std::make_shared<EggParserNode_Type>(EggParserType(this->tag));
  return type;
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_VariableDeclaration::promote(egg::yolk::IEggParserContext& context) {
  return std::make_shared<EggParserNode_Declare>(this->name, this->child->promote(context));
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_VariableInitialization::promote(egg::yolk::IEggParserContext& context) {
  return std::make_shared<EggParserNode_Declare>(this->name, this->child[0]->promote(context), this->child[1]->promote(context));
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Assignment::promote(egg::yolk::IEggParserContext& context) {
  EggParserAssign aop;
  switch (this->op) {
    case EggTokenizerOperator::PercentEqual:
      aop = EggParserAssign::Remainder;
      break;
    case EggTokenizerOperator::AmpersandEqual:
      aop = EggParserAssign::BitwiseAnd;
      break;
    case EggTokenizerOperator::StarEqual:
      aop = EggParserAssign::Multiply;
      break;
    case EggTokenizerOperator::PlusEqual:
      aop = EggParserAssign::Plus;
      break;
    case EggTokenizerOperator::MinusEqual:
      aop = EggParserAssign::Minus;
      break;
    case EggTokenizerOperator::SlashEqual:
      aop = EggParserAssign::Divide;
      break;
    case EggTokenizerOperator::ShiftLeftEqual:
      aop = EggParserAssign::ShiftLeft;
      break;
    case EggTokenizerOperator::Equal:
      aop = EggParserAssign::Assign;
      break;
    case EggTokenizerOperator::ShiftRightEqual:
      aop = EggParserAssign::ShiftRight;
      break;
    case EggTokenizerOperator::ShiftRightUnsignedEqual:
      aop = EggParserAssign::ShiftRightUnsigned;
      break;
    case EggTokenizerOperator::CaretEqual:
      aop = EggParserAssign::BitwiseXor;
      break;
    case EggTokenizerOperator::BarEqual:
      aop = EggParserAssign::BitwiseOr;
      break;
    case EggTokenizerOperator::Bang:
    case EggTokenizerOperator::BangEqual:
    case EggTokenizerOperator::Percent:
    case EggTokenizerOperator::Ampersand:
    case EggTokenizerOperator::AmpersandAmpersand:
    case EggTokenizerOperator::ParenthesisLeft:
    case EggTokenizerOperator::ParenthesisRight:
    case EggTokenizerOperator::Star:
    case EggTokenizerOperator::Plus:
    case EggTokenizerOperator::PlusPlus:
    case EggTokenizerOperator::Comma:
    case EggTokenizerOperator::Minus:
    case EggTokenizerOperator::MinusMinus:
    case EggTokenizerOperator::Lambda:
    case EggTokenizerOperator::Dot:
    case EggTokenizerOperator::Ellipsis:
    case EggTokenizerOperator::Slash:
    case EggTokenizerOperator::Colon:
    case EggTokenizerOperator::Semicolon:
    case EggTokenizerOperator::Less:
    case EggTokenizerOperator::ShiftLeft:
    case EggTokenizerOperator::LessEqual:
    case EggTokenizerOperator::EqualEqual:
    case EggTokenizerOperator::Greater:
    case EggTokenizerOperator::GreaterEqual:
    case EggTokenizerOperator::ShiftRight:
    case EggTokenizerOperator::ShiftRightUnsigned:
    case EggTokenizerOperator::Query:
    case EggTokenizerOperator::QueryQuery:
    case EggTokenizerOperator::BracketLeft:
    case EggTokenizerOperator::BracketRight:
    case EggTokenizerOperator::Caret:
    case EggTokenizerOperator::CurlyLeft:
    case EggTokenizerOperator::Bar:
    case EggTokenizerOperator::BarBar:
    case EggTokenizerOperator::CurlyRight:
    case EggTokenizerOperator::Tilde:
    default:
      throw exceptionFromToken(context, "Unknown assignment operator", *this);
  }
  return std::make_shared<EggParserNode_Set>(aop, this->child[0]->promote(context), this->child[1]->promote(context));
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Mutate::promote(egg::yolk::IEggParserContext& context) {
  EggParserMutate mop;
  if (this->op == EggTokenizerOperator::PlusPlus) {
    mop = EggParserMutate::Increment;
  } else if (this->op == EggTokenizerOperator::MinusMinus) {
    mop = EggParserMutate::Decrement;
  } else {
    throw exceptionFromToken(context, "Unknown increment/decrement operator", *this);
  }
  return std::make_shared<EggParserNode_Mutate>(mop, this->child->promote(context));
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Break::promote(egg::yolk::IEggParserContext& context) {
  if (!context.isAllowed(EggParserAllowed::Break)) {
    throw exceptionFromLocation(context, "The 'break' statement may only be used within loops or switch statements", *this);
  }
  return std::make_shared<EggParserNode_Break>();
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Case::promote(egg::yolk::IEggParserContext& context) {
  // The logic is handled by the 'switch' node, so just promote the value expression
  if (!context.isAllowed(EggParserAllowed::Case)) {
    throw exceptionFromLocation(context, "The 'case' statement may only be used within switch statements", *this);
  }
  return this->child->promote(context);
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Catch::promote(egg::yolk::IEggParserContext& context) {
  auto type = this->child[0]->promote(context);
  EggParserContextNested nested(context, EggParserAllowed::Rethrow|EggParserAllowed::Return|EggParserAllowed::Yield);
  auto block = this->child[1]->promote(nested);
  return std::make_shared<EggParserNode_Catch>(this->name, type, block);
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Continue::promote(egg::yolk::IEggParserContext& context) {
  if (!context.isAllowed(EggParserAllowed::Continue)) {
    throw exceptionFromLocation(context, "The 'continue' statement may only be used within loops or switch statements", *this);
  }
  return std::make_shared<EggParserNode_Continue>();
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Default::promote(egg::yolk::IEggParserContext& context) {
  // The logic is handled by the 'switch' node, so just assume it's a misplaced 'default'
  throw exceptionFromLocation(context, "The 'default' statement may only be used within switch statements", *this);
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Do::promote(egg::yolk::IEggParserContext& context) {
  auto condition = this->child[0]->promote(context);
  EggParserContextNested nested(context, EggParserAllowed::Break|EggParserAllowed::Continue, EggParserAllowed::Rethrow|EggParserAllowed::Return|EggParserAllowed::Yield);
  auto block = this->child[1]->promote(nested);
  return std::make_shared<EggParserNode_Do>(condition, block);
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_If::promote(egg::yolk::IEggParserContext& context) {
  assert((this->child.size() == 2) || (this->child.size() == 3));
  auto condition = this->child[0]->promote(context);
  auto trueBlock = this->child[1]->promote(context);
  std::shared_ptr<egg::yolk::IEggParserNode> falseBlock = nullptr;
  if (this->child.size() == 3) {
    falseBlock = this->child[2]->promote(context);
  }
  return std::make_shared<EggParserNode_If>(condition, trueBlock, falseBlock);
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Finally::promote(egg::yolk::IEggParserContext& context) {
  // The logic is handled by the 'try' node, so just assume it's a misplaced 'finally'
  throw exceptionFromLocation(context, "The 'finally' statement may only be used as part of a 'try' statement", *this);
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_For::promote(egg::yolk::IEggParserContext& context) {
  // We allow empty statements but not flow control in the three 'for' clauses
  EggParserContextNested nested1(context, EggParserAllowed::Empty);
  auto pre = this->child[0]->promote(nested1);
  auto cond = this->child[1]->promote(nested1);
  auto post = this->child[2]->promote(nested1);
  EggParserContextNested nested2(context, EggParserAllowed::Break|EggParserAllowed::Continue, EggParserAllowed::Rethrow|EggParserAllowed::Return|EggParserAllowed::Yield);
  auto block = this->child[3]->promote(nested2);
  return std::make_shared<EggParserNode_For>(pre, cond, post, block);
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Foreach::promote(egg::yolk::IEggParserContext& context) {
  auto target = this->child[0]->promote(context);
  auto expr = this->child[1]->promote(context);
  EggParserContextNested nested(context, EggParserAllowed::Break|EggParserAllowed::Continue, EggParserAllowed::Rethrow|EggParserAllowed::Return|EggParserAllowed::Yield);
  auto block = this->child[2]->promote(nested);
  return std::make_shared<EggParserNode_Foreach>(target, expr, block);
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Return::promote(egg::yolk::IEggParserContext& context) {
  auto result = std::make_shared<EggParserNode_Return>();
  for (auto& i : this->child) {
    result->addChild(i->promote(context));
  }
  return result;
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Switch::promote(egg::yolk::IEggParserContext&) {
  EGG_THROW(__FUNCTION__ " TODO"); // TODO
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Throw::promote(egg::yolk::IEggParserContext&) {
  EGG_THROW(__FUNCTION__ " TODO"); // TODO
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Try::promote(egg::yolk::IEggParserContext&) {
  EGG_THROW(__FUNCTION__ " TODO"); // TODO
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Using::promote(egg::yolk::IEggParserContext&) {
  EGG_THROW(__FUNCTION__ " TODO"); // TODO
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_While::promote(egg::yolk::IEggParserContext&) {
  EGG_THROW(__FUNCTION__ " TODO"); // TODO
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Yield::promote(egg::yolk::IEggParserContext&) {
  EGG_THROW(__FUNCTION__ " TODO"); // TODO
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_UnaryOperator::promote(egg::yolk::IEggParserContext& context) {
  if (this->op == EggTokenizerOperator::Bang) {
    return this->promoteUnary<EggParserNode_UnaryLogicalNot>(context);
  }
  if (this->op == EggTokenizerOperator::Ampersand) {
    return this->promoteUnary<EggParserNode_UnaryRef>(context);
  }
  if (this->op == EggTokenizerOperator::Star) {
    return this->promoteUnary<EggParserNode_UnaryDeref>(context);
  }
  if (this->op == EggTokenizerOperator::Minus) {
    return this->promoteUnary<EggParserNode_UnaryNegate>(context);
  }
  if (this->op == EggTokenizerOperator::Ellipsis) {
    return this->promoteUnary<EggParserNode_UnaryEllipsis>(context);
  }
  if (this->op == EggTokenizerOperator::Tilde) {
    return this->promoteUnary<EggParserNode_UnaryBitwiseNot>(context);
  }
  throw exceptionFromToken(context, "Unknown unary operator", *this);
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_BinaryOperator::promote(egg::yolk::IEggParserContext& context) {
  switch (this->op) {
  case EggTokenizerOperator::Percent:
    return this->promoteBinary<EggParserNode_BinaryRemainder>(context);
  case EggTokenizerOperator::Ampersand:
    return this->promoteBinary<EggParserNode_BinaryBitwiseAnd>(context);
  case EggTokenizerOperator::AmpersandAmpersand:
    return this->promoteBinary<EggParserNode_BinaryLogicalAnd>(context);
  case EggTokenizerOperator::Star:
    return this->promoteBinary<EggParserNode_BinaryMultiply>(context);
  case EggTokenizerOperator::Plus:
    return this->promoteBinary<EggParserNode_BinaryPlus>(context);
  case EggTokenizerOperator::Minus:
    return this->promoteBinary<EggParserNode_BinaryMinus>(context);
  case EggTokenizerOperator::Lambda:
    return this->promoteBinary<EggParserNode_BinaryLambda>(context);
  case EggTokenizerOperator::Dot:
    return this->promoteBinary<EggParserNode_BinaryDot>(context);
  case EggTokenizerOperator::Slash:
    return this->promoteBinary<EggParserNode_BinaryDivide>(context);
  case EggTokenizerOperator::Less:
    return this->promoteBinary<EggParserNode_BinaryLess>(context);
  case EggTokenizerOperator::ShiftLeft:
    return this->promoteBinary<EggParserNode_BinaryShiftLeft>(context);
  case EggTokenizerOperator::LessEqual:
    return this->promoteBinary<EggParserNode_BinaryLessEqual>(context);
  case EggTokenizerOperator::EqualEqual:
    return this->promoteBinary<EggParserNode_BinaryEqual>(context);
  case EggTokenizerOperator::Greater:
    return this->promoteBinary<EggParserNode_BinaryGreater>(context);
  case EggTokenizerOperator::GreaterEqual:
    return this->promoteBinary<EggParserNode_BinaryGreaterEqual>(context);
  case EggTokenizerOperator::ShiftRight:
    return this->promoteBinary<EggParserNode_BinaryShiftRight>(context);
  case EggTokenizerOperator::ShiftRightUnsigned:
    return this->promoteBinary<EggParserNode_BinaryShiftRightUnsigned>(context);
  case EggTokenizerOperator::QueryQuery:
    return this->promoteBinary<EggParserNode_BinaryNullCoalescing>(context);
  case EggTokenizerOperator::BracketLeft:
    return this->promoteBinary<EggParserNode_BinaryBrackets>(context);
  case EggTokenizerOperator::Caret:
    return this->promoteBinary<EggParserNode_BinaryBitwiseXor>(context);
  case EggTokenizerOperator::Bar:
    return this->promoteBinary<EggParserNode_BinaryBitwiseOr>(context);
  case EggTokenizerOperator::BarBar:
    return this->promoteBinary<EggParserNode_BinaryLogicalOr>(context);
  case EggTokenizerOperator::Bang:
  case EggTokenizerOperator::BangEqual:
  case EggTokenizerOperator::PercentEqual:
  case EggTokenizerOperator::AmpersandEqual:
  case EggTokenizerOperator::ParenthesisLeft:
  case EggTokenizerOperator::ParenthesisRight:
  case EggTokenizerOperator::StarEqual:
  case EggTokenizerOperator::PlusPlus:
  case EggTokenizerOperator::PlusEqual:
  case EggTokenizerOperator::Comma:
  case EggTokenizerOperator::MinusMinus:
  case EggTokenizerOperator::MinusEqual:
  case EggTokenizerOperator::SlashEqual:
  case EggTokenizerOperator::Colon:
  case EggTokenizerOperator::Semicolon:
  case EggTokenizerOperator::Ellipsis:
  case EggTokenizerOperator::ShiftLeftEqual:
  case EggTokenizerOperator::Equal:
  case EggTokenizerOperator::ShiftRightEqual:
  case EggTokenizerOperator::ShiftRightUnsignedEqual:
  case EggTokenizerOperator::Query:
  case EggTokenizerOperator::BracketRight:
  case EggTokenizerOperator::CaretEqual:
  case EggTokenizerOperator::CurlyLeft:
  case EggTokenizerOperator::BarEqual:
  case EggTokenizerOperator::CurlyRight:
  case EggTokenizerOperator::Tilde:
  default:
    throw exceptionFromToken(context, "Unknown binary operator", *this);
  }
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_TernaryOperator::promote(egg::yolk::IEggParserContext&) {
  EGG_THROW(__FUNCTION__ " TODO"); // TODO
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Call::promote(egg::yolk::IEggParserContext&) {
  EGG_THROW(__FUNCTION__ " TODO"); // TODO
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Named::promote(egg::yolk::IEggParserContext&) {
  EGG_THROW(__FUNCTION__ " TODO"); // TODO
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Identifier::promote(egg::yolk::IEggParserContext&) {
  return std::make_shared<EggParserNode_Identifier>(this->name);
}

std::shared_ptr<egg::yolk::IEggParserNode> egg::yolk::EggSyntaxNode_Literal::promote(egg::yolk::IEggParserContext& context) {
  if (this->kind == EggTokenizerKind::Integer) {
    return std::make_shared<EggParserNode_LiteralInteger>(this->value.i);
  }
  if (this->kind == EggTokenizerKind::Float) {
    return std::make_shared<EggParserNode_LiteralFloat>(this->value.f);
  }
  if (this->kind == EggTokenizerKind::String) {
    return std::make_shared<EggParserNode_LiteralString>(this->value.s);
  }
  throw exceptionFromToken(context, "Unknown literal value type", *this);
}

std::shared_ptr<egg::yolk::IEggParser> egg::yolk::EggParserFactory::createModuleParser() {
  return std::make_shared<EggParserModule>();
}
