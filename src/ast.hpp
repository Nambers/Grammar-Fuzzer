#ifndef AST_HPP
#define AST_HPP

#include <array>
#include <random>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace FuzzingAST {

using NodeID = int;
using ScopeID = int;
using TypeID = int;

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
    Call,         // f(x)
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

class ASTData; // forward declaration

class BuiltinContext {
  public:
    std::unordered_map<std::string, FunctionSignature> builtinsFuncs = {};
    size_t builtinsFuncsCnt = 0;
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
    void update(ASTData &ast);

    // Pick a random type available in given scope (fallback to 0)
    TypeID pickRandomType(ScopeID scopeID);

    // Pick a random variable name by type, with fallback to object type (0)
    std::string pickRandomVar(ScopeID scopeID, TypeID type);

    // Pick a random variable of any type in given scope (including inherited
    // and object)
    std::string pickRandomVar(ScopeID scopeID);
    std::string pickRandomVar(ScopeID scopeID,
                              const std::vector<TypeID> &types);

    const std::pair<const std::string, FunctionSignature> &
    pickRandomFunc(const ASTData &ast, ScopeID scopeID);

  private:
    std::vector<std::unordered_map<TypeID, std::vector<std::string>>>
        index_; // one map per scope,
                // includes inherited
                // variables

    std::vector<std::vector<TypeID>> typeList_;
    std::vector<std::uniform_int_distribution<size_t>> typeDist_;

    std::vector<
        std::unordered_map<TypeID, std::uniform_int_distribution<size_t>>>
        varDist_;

    std::vector<std::uniform_int_distribution<size_t>> funcDist_;
    std::vector<size_t> funcCnts_;
};

class ASTNodeValue {
  public:
    std::variant<std::string, int64_t, bool, double> val;
};

class ASTNode {
  public:
    ASTNodeKind kind;
    // only it's declareVar, the type is the type of the variable, lvar
    TypeID type = -1;
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
    std::vector<NodeID> declarations = {};
    std::vector<NodeID> expressions = {};
    std::vector<std::string> types = {};
    std::vector<TypeID> inheritedTypes = {};
    std::vector<int> variables = {};
    std::unordered_map<std::string, FunctionSignature> funcSignatures = {};
};

class AST {
  public:
    std::string nameCnt = "a";
    std::vector<ASTScope> scopes = {};
    std::vector<ASTNode> declarations = {};
    std::vector<ASTNode> expressions = {};

    // generate main block
    AST() : scopes({ASTScope()}) {}
};

class ASTData {
  public:
    AST ast;
};

const std::string &getTypeName(TypeID tid, const AST &scope,
                               const BuiltinContext &ctx);
void initPrimitiveTypes(BuiltinContext &ctx);
}; // namespace FuzzingAST

#endif // AST_HPP
