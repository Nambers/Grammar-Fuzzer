#ifndef AST_HPP
#define AST_HPP

#include <array>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace FuzzingAST {

using NodeID = int;
using ScopeID = int;
using TypeID = int;
using VarID = int;

constexpr size_t SCOPE_MAX_TYPE = 200;
constexpr std::array BINARY_OPS{"+",  "-",  "*",  "/", "%",  "**",
                                "//", "==", "!=", "<", ">",  "<=",
                                ">=", "&",  "|",  "^", "<<", ">>"};
constexpr std::array UNARY_OPS{"-", "not", "~"};

enum class ASTNodeKind {
    Function = 0, // def f(x):
    Class,        // class A:
    DeclareVar,   // var a = ...
    Import,       // import x
                  // ---
    Assign,       // x = y
    GetProp,      // z = x.y
    SetProp,      // x.y = z
    Call,         // z = y(x)
    Return,       // return x
    BinaryOp,     // x + y
    UnaryOp,      // -x
    // ---
    GlobalRef,
};

constexpr ASTNodeKind DECL_NODE_END = ASTNodeKind::Import;
constexpr ASTNodeKind EXEC_NODE_START = ASTNodeKind::Assign;
constexpr ASTNodeKind EXEC_NODE_END = ASTNodeKind::UnaryOp;

class FunctionSignature {
  public:
    std::vector<TypeID> paramTypes;
    TypeID selfType = -1; // for methods, the type of the class
    TypeID returnType = -1;
};

class PropInfo {
  public:
    TypeID type = -1;
    ScopeID scope = -1;
    std::string name;
    bool isConst = false;
    bool isCallable = false;
    FunctionSignature funcSig = {};
};

class ASTData; // forward declaration
class AST;

class BuiltinContext {
  public:
    std::unordered_map<TypeID, std::vector<PropInfo>> builtinsProps = {};
    std::vector<std::string> types = {};
    size_t builtinTypesCnt = 0;
    std::vector<std::vector<std::vector<TypeID>>> ops = {};
    std::vector<std::vector<TypeID>> unaryOps = {};
    TypeID strID = -1;
    TypeID intID = -1;
    TypeID floatID = -1;
    TypeID boolID = -1;
    // --- variable provider ---
  public:
    // Build index for all scopes, merging parent scope and initializing
    // distributions
    void updateVars(const ASTData &ast);
    void updateFuncs(const ASTData &ast);
    inline void update(const ASTData &ast) {
        updateVars(ast);
        updateFuncs(ast);
    }

    // Pick a random type available in given scope (fallback to 0)
    TypeID pickRandomType(ScopeID scopeID);

    // Pick a random variable name by type, with fallback to object type (0)
    std::string pickRandomVar(ScopeID scopeID, TypeID type);

    // Pick a random variable of any type in given scope (including inherited
    // and object)
    std::string pickRandomVar(ScopeID scopeID);
    std::string pickRandomVar(ScopeID scopeID,
                              const std::vector<TypeID> &types);

    std::optional<PropInfo> pickRandomFunc(const AST &ast, ScopeID scopeID);
    std::optional<PropInfo> pickRandomMethod(const AST &ast, TypeID tid);

  private:
    // avoid store ptr
    class PropKey {
      public:
        bool isBuiltin; // true -> builtinsProps
        size_t idx;
        TypeID type; // builtinProps[type]
    };

    std::vector<std::unordered_map<TypeID, std::vector<std::string>>> index_;

    std::vector<std::vector<TypeID>> typeList_;
    std::vector<std::uniform_int_distribution<size_t>> typeDist_;
    std::vector<
        std::unordered_map<TypeID, std::uniform_int_distribution<size_t>>>
        varDist_;

    std::vector<std::vector<PropKey>> funcList_;
    std::vector<std::uniform_int_distribution<size_t>> funcDist_;
    std::vector<size_t> funcCnts_;
    std::unordered_map<TypeID, std::uniform_int_distribution<size_t>>
        methodDist_;
};

class ASTNodeValue {
  public:
    std::variant<std::string, int64_t, bool, double> val;
};

class ASTNode {
  public:
    ASTNodeKind kind;
    /*
    assign: [0] = [1]
    unaryOp: [0] = [1] [2]
    binaryOp: [0] = [1] [2] [3]
    class: class [0]([1],... till sentinel=-1):
                [3] ... as member functions
    function:
     */
    std::vector<ASTNodeValue> fields = {};
    ScopeID scope = -1;
};

class ASTScope {
  public:
    ScopeID parent = -1;
    TypeID retType = -1;
    int paramCnt = 0;
    std::vector<NodeID> declarations = {};
    std::vector<NodeID> expressions = {};
    std::vector<std::string> types = {};
    std::vector<TypeID> inheritedTypes = {};
    std::vector<VarID> variables = {};
};

class AST {
  public:
    std::string nameCnt = "a";
    std::vector<ASTScope> scopes = {};
    std::vector<ASTNode> declarations = {};
    std::vector<ASTNode> expressions = {};
    std::vector<PropInfo> variables = {};
    // we don't do normal function in fuzzing,
    // bc it is very unlikely to trigger bugs
    // std::vector<PropInfo> functions;
    std::unordered_map<TypeID, std::vector<PropInfo>> classProps;

    // generate main block
    AST() : scopes({ASTScope()}) {}
};

class ASTData {
  public:
    AST ast;
};

class ExecutionContext {
  public:
    virtual ~ExecutionContext() = default;
    virtual void *getContext() = 0;
    virtual void releasePtr() = 0;
};

const std::string &getTypeName(TypeID tid, const AST &scope,
                               const BuiltinContext &ctx);
TypeID resolveType(const std::string &fullname, const BuiltinContext &ctx,
                   const AST &ast, ScopeID sid);
std::optional<PropInfo> getPropByName(const std::string &name,
                                      const std::vector<PropInfo> &slice,
                                      bool isCallable, ScopeID sid);
void initPrimitiveTypes(BuiltinContext &ctx);
}; // namespace FuzzingAST

#endif // AST_HPP
