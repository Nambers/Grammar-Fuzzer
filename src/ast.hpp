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

constexpr ScopeID EMPTY_SCOPE = -1;

// not delcare under a class scope
constexpr TypeID NOT_UNDER_CLASS = -1;
constexpr TypeID NO_RETURN = -1;
constexpr TypeID OBJECT_TYPE = 0;

constexpr size_t SCOPE_MAX_TYPE = 200;
constexpr size_t MAX_SCOPE_CNT = 20;
constexpr size_t MAX_GEN_HISTORY = 100;
constexpr size_t REROLL_ATTEMPTS = 100;

constexpr uint32_t RUNLINES_TIMEOUT_MS = 1000;
constexpr uint32_t RUNLINE_TIMEOUT_MS = 500;

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
    SetItem,      // x[y] = z
    GetItem,      // z = x[y]
    // ---
    GlobalRef,
};

constexpr ASTNodeKind DECL_NODE_END = ASTNodeKind::Import;
constexpr ASTNodeKind EXEC_NODE_START = ASTNodeKind::GetProp;
constexpr ASTNodeKind EXEC_NODE_END = ASTNodeKind::GetItem;

class FunctionSignature {
  public:
    std::vector<TypeID> paramTypes;
    TypeID selfType = -1; // for methods, the type of the class
    TypeID returnType = -1;

  public:
    bool operator==(const FunctionSignature &other) const = default;
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
    bool operator==(const PropInfo &other) const = default;
    struct Hash {
        template <typename T>
        static inline void hash_combine(size_t &seed, T value) {
            seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        std::size_t operator()(const FuzzingAST::PropInfo &key) const {
            size_t seed = 0;

            hash_combine(seed, std::hash<TypeID>{}(key.type));
            hash_combine(seed, std::hash<ScopeID>{}(key.scope));
            hash_combine(seed, std::hash<std::string>{}(key.name));
            hash_combine(seed, std::hash<bool>{}(key.isConst));
            hash_combine(seed, std::hash<bool>{}(key.isCallable));
            hash_combine(seed, std::hash<bool>{}(key.isArg));
            // Optionally, hash funcSig if needed for uniqueness
            return seed;
        }
    };
};

class ASTData; // forward declaration
class AST;
class ASTScope;

// avoid store ptr
class PropKey {
  public:
    ModuleID moduleID = NO_MODULE; // moduleID = -1 => general, moduleID = 0 =>
                                   // builtins, moduleID > 0
                                   // => modulesProps[moduleID - 1]
    size_t idx = SIZE_MAX;
    TypeID parentType = -1; // builtinProps[type]

    inline bool empty() const {
        return idx == SIZE_MAX && parentType == NOT_UNDER_CLASS;
    }
    inline bool operator==(const PropKey &other) const = default;
    // empty static instance
    inline static const PropKey &emptyKey() {
        static const PropKey emptyKeyInstance{false, SIZE_MAX, NOT_UNDER_CLASS};
        return emptyKeyInstance;
    }
};

enum class ObjectKind {
    mutable_var = 0,
    constant_var = 1,
    function = 2,
};

class ScopeProvider {
  public:
    // TypeID is dense enough (<=75), use vector indexed by TypeID directly
    // index: [typeID] -> vector of PropKey
    std::unordered_map<TypeID, std::vector<PropKey>> constIndex;   // [TypeID]
    std::unordered_map<TypeID, std::vector<PropKey>> mutableIndex; // [TypeID]
    std::unordered_map<TypeID, std::vector<PropKey>> funcIndex;    // [TypeID]

    // dist per TypeID, only valid when corresponding index is non-empty
    std::unordered_map<TypeID, std::uniform_int_distribution<size_t>> constDist;
    std::unordered_map<TypeID, std::uniform_int_distribution<size_t>>
        mutableDist;
    std::unordered_map<TypeID, std::uniform_int_distribution<size_t>> funcDist;

    std::array<std::bernoulli_distribution, 3> useParent;

  public:
    ScopeProvider() = default;

    inline std::unordered_map<TypeID, std::vector<PropKey>> &
    selectIndex(const PropInfo &pi) {
        return pi.isCallable ? funcIndex
                             : (pi.isConst ? constIndex : mutableIndex);
    }

    inline const std::unordered_map<TypeID, std::vector<PropKey>> &
    selectIndex(ObjectKind kind) const {
        switch (kind) {
        case ObjectKind::constant_var:
            return constIndex;
        case ObjectKind::mutable_var:
            return mutableIndex;
        case ObjectKind::function:
            return funcIndex;
        }
    }

    inline std::unordered_map<FuzzingAST::TypeID,
                              std::uniform_int_distribution<size_t>> &
    selectDist(ObjectKind kind) {
        switch (kind) {
        case ObjectKind::constant_var:
            return constDist;
        case ObjectKind::mutable_var:
            return mutableDist;
        case ObjectKind::function:
            return funcDist;
        }
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
    TypeID listID = -1;
    TypeID bytearrayID = -1;
    TypeID dictID = -1;
    std::discrete_distribution<int> pickValueKindDist{
        9, 1, 6}; // 9:1:6 non-const, const and function result
                  // --- variable provider ---
  public:
    void initFromBuiltins();
    // Build index for all scopes, merging parent scope and initializing
    // distributions
    void updateVars(const AST &ast);

    // Pick a random type available in given scope (fallback to 0)
    TypeID pickRandomType(ScopeID scopeID);

    ObjectKind pickValueKind();
    bool respectType();

    // Pick a random variable name by type, with fallback to object type (0)
    PropKey pickRandomVar(ScopeID scopeID, TypeID type, ObjectKind valueKind,
                          const std::vector<ASTScope> &scopes);

    // Pick a random variable of any type in given scope (including inherited
    // and object)
    PropKey pickRandomVar(ScopeID scopeID, ObjectKind valueKind,
                          const std::vector<ASTScope> &scopes);
    PropKey pickRandomVar(ScopeID scopeID, const std::vector<TypeID> &types,
                          ObjectKind valueKind,
                          const std::vector<ASTScope> &scopes);

    PropKey pickRandomMethod(TypeID tid);

  private:
    std::vector<std::vector<TypeID>> typeList_;
    std::vector<std::uniform_int_distribution<size_t>> typeDist_;

    // [scopeID]
    std::vector<ScopeProvider> scopeProviders;
    // Cached builtins index injected into global scope each update.
    ScopeProvider builtinsScopeCache;

    std::bernoulli_distribution respectType_dist{
        0.8}; // 80% respect type, 20% not respect type

    std::unordered_map<TypeID, std::uniform_int_distribution<size_t>>
        methodDist_;
    std::unordered_map<TypeID, std::vector<PropKey>> methodIndex_;
};

class ASTNodeValue {
  public:
    std::variant<std::string, int64_t, bool, double> val;

  public:
    bool operator==(const ASTNodeValue &other) const = default;
};

inline const ASTNodeValue SENTINEL_NODE{-1};

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
    // if it's function, it should have scope linked to it
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
    std::unordered_set<ModuleID> importedModules = {};
};

class AST {
  public:
    std::string nameCnt = "aaa"; // try to avoid keyword, like `as`
    std::vector<ASTScope> scopes = {};
    std::vector<ASTNode> declarations = {};
    std::vector<ASTNode> expressions = {};
    // variables treated as parentType = -1(no parent), ModuleID = -1(no module)
    std::vector<PropKey> variables = {};
    // we don't do normal function in fuzzing,
    // bc it is very unlikely to trigger bugs
    // std::vector<PropInfo> functions;
    // TypeID -> -1 means not under class
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

inline void insertGlobalVar(const PropInfo &varProp,
                            std::unordered_set<std::string> &globalVars) {
    if (!varProp.isCallable && !varProp.isConst && !varProp.isArg)
        globalVars.insert(varProp.name);
}
}; // namespace FuzzingAST

#endif // AST_HPP
