#ifndef AST_HPP
#define AST_HPP

#include <array>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace FuzzingAST {

using NodeID = int;
using ScopeID = int;
using TypeID = int;
using ModuleID = int;
using VarID = int;
constexpr ModuleID NO_MODULE = -1;
constexpr ModuleID BUILTIN_MODULE_ID = 0;

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
    GetProp,      // z = x.y
    SetProp,      // x.y = z
    Call,         // z = y(x)
    Return,       // return x
    BinaryOp,     // x + y
    UnaryOp,      // -x
    NewInstance,  // x = Class()
    // ---
    GlobalRef,
};

constexpr ASTNodeKind DECL_NODE_END = ASTNodeKind::Import;
constexpr ASTNodeKind EXEC_NODE_START = ASTNodeKind::GetProp;
constexpr ASTNodeKind EXEC_NODE_END = ASTNodeKind::NewInstance;

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
    bool isArg = false;
    FunctionSignature funcSig = {};
    bool operator==(const PropInfo &other) const {
        return type == other.type && scope == other.scope &&
               name == other.name && isConst == other.isConst &&
               isCallable == other.isCallable && isArg == other.isArg &&
               funcSig.paramTypes == other.funcSig.paramTypes &&
               funcSig.selfType == other.funcSig.selfType &&
               funcSig.returnType == other.funcSig.returnType;
    }
    struct Hash {
        std::size_t operator()(const FuzzingAST::PropInfo &key) const {
            std::hash<std::string> stringHasher;
            std::hash<TypeID> typeHasher;
            std::hash<ScopeID> scopeHasher;

            size_t seed = 0;
            seed ^=
                typeHasher(key.type) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^=
                scopeHasher(key.scope) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^=
                stringHasher(key.name) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= std::hash<bool>()(key.isConst) + 0x9e3779b9 + (seed << 6) +
                    (seed >> 2);
            seed ^= std::hash<bool>()(key.isCallable) + 0x9e3779b9 +
                    (seed << 6) + (seed >> 2);
            return seed;
        }
    };
};

class ASTData; // forward declaration
class AST;

// avoid store ptr
class PropKey {
  public:
    ModuleID moduleID = NO_MODULE; // moduleID = -1 => general, moduleID = 0 =>
                                   // builtins, moduleID > 0
                                   // => modulesProps[moduleID - 1]
    size_t idx = SIZE_MAX;
    TypeID parentType = -1; // builtinProps[type]

    inline bool empty() const { return idx == SIZE_MAX && parentType == -1; }
    inline bool operator==(const PropKey &other) const {
        return moduleID == other.moduleID && idx == other.idx &&
               parentType == other.parentType;
    }
    // empty static instance
    inline static const PropKey &emptyKey() {
        static const PropKey emptyKeyInstance{false, SIZE_MAX, -1};
        return emptyKeyInstance;
    }
};

class BuiltinContext {
  public:
    std::unordered_map<TypeID, std::vector<PropInfo>> builtinsProps = {};
    std::unordered_map<ModuleID,
                       std::unordered_map<TypeID, std::vector<PropInfo>>>
        modulesProps = {};
    std::vector<std::string> types = {};
    size_t builtinTypesCnt = 0;
    std::vector<std::vector<std::vector<TypeID>>> ops = {};
    std::vector<std::vector<TypeID>> unaryOps = {};
    TypeID strID = -1;
    TypeID intID = -1;
    TypeID floatID = -1;
    TypeID boolID = -1;
    std::discrete_distribution<bool> pickConstDist{
        9, 1}; // 9:1 non-const and const
               // --- variable provider ---
  public:
    // Build index for all scopes, merging parent scope and initializing
    // distributions
    void updateVars(const AST &ast);
    void updateFuncs(const AST &ast);
    inline void update(const AST &ast) {
        updateVars(ast);
        updateFuncs(ast);
    }

    // Pick a random type available in given scope (fallback to 0)
    TypeID pickRandomType(ScopeID scopeID);

    bool pickConst();

    // Pick a random variable name by type, with fallback to object type (0)
    PropKey pickRandomVar(ScopeID scopeID, TypeID type, bool isConst);

    // Pick a random variable of any type in given scope (including inherited
    // and object)
    PropKey pickRandomVar(ScopeID scopeID, bool isConst);
    PropKey pickRandomVar(ScopeID scopeID, const std::vector<TypeID> &types,
                          bool isConst);

    PropKey pickRandomFunc(ScopeID scopeID);
    PropKey pickRandomMethod(TypeID tid);

  private:
    std::vector<std::unordered_map<TypeID, std::vector<PropKey>>> constIndex_;
    std::vector<std::unordered_map<TypeID, std::vector<PropKey>>> mutableIndex_;

    std::vector<std::vector<TypeID>> typeList_;
    std::vector<std::uniform_int_distribution<size_t>> typeDist_;
    std::vector<std::unordered_map<
        TypeID, std::array<std::uniform_int_distribution<size_t>, 2>>>
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
    NodeID retNodeID = -1;
    NodeID globalRefID = -1;
    std::vector<NodeID> declarations = {};
    std::vector<NodeID> expressions = {};
    std::vector<std::string> types = {};
    std::vector<TypeID> inheritedTypes = {};
    std::vector<VarID> variables = {};
};

class AST {
  public:
    std::string nameCnt = "aaa"; // try to avoid keyword, like `as`
    std::vector<ASTScope> scopes = {};
    std::vector<ASTNode> declarations = {};
    std::vector<ASTNode> expressions = {};
    // variables treated as parentType = -1(no parent), ModuleID = -1(no module)
    std::vector<PropKey> variables = {};
    std::unordered_set<ModuleID> importedModules = {};
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

const std::string &getTypeName(TypeID tid, const AST &ast,
                               const BuiltinContext &ctx);
TypeID resolveType(const std::string &fullname, const BuiltinContext &ctx,
                   const AST &ast, ScopeID sid);
std::optional<PropInfo> getPropByName(const std::string &name,
                                      const std::vector<PropInfo> &slice,
                                      bool isCallable, ScopeID sid);
void initPrimitiveTypes(BuiltinContext &ctx);
inline const PropInfo &unfoldKey(const PropKey &key, const AST &ast,
                                 const BuiltinContext &ctx) {
    if (key.moduleID == BUILTIN_MODULE_ID)
        return ctx.builtinsProps.at(key.parentType).at(key.idx);
    if (key.moduleID > 0)
        return ctx.modulesProps.at(key.moduleID).at(key.parentType).at(key.idx);
    return ast.classProps.at(key.parentType).at(key.idx);
}

inline PropInfo &unfoldKey(const PropKey &key, AST &ast, BuiltinContext &ctx) {
    if (key.moduleID == BUILTIN_MODULE_ID)
        return ctx.builtinsProps.at(key.parentType).at(key.idx);
    if (key.moduleID > 0)
        return ctx.modulesProps.at(key.moduleID).at(key.parentType).at(key.idx);
    return ast.classProps.at(key.parentType).at(key.idx);
}

inline void insertGlobalVar(const std::string &varName, bool isConst,
                            bool isArg,
                            std::unordered_set<std::string> &globalVars) {
    if (!isConst && !isArg)
        globalVars.insert(varName);
}

inline void insertGlobalVar(const PropInfo &varProp,
                            std::unordered_set<std::string> &globalVars) {
    if (!varProp.isConst && !varProp.isArg)
        globalVars.insert(varProp.name);
}
}; // namespace FuzzingAST

#endif // AST_HPP
