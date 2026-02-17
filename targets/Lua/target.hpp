#ifndef LUA_TARGET_HPP
#define LUA_TARGET_HPP

#include "ast.hpp"
#include <lua.hpp>
#include <memory>

namespace FuzzingAST {

struct LuaStateDeleter {
    void operator()(lua_State *L) const {
        if (L)
            lua_close(L);
    }
};
using LuaStatePtr = std::unique_ptr<lua_State, LuaStateDeleter>;

class LuaExecutionContext : public ExecutionContext {
  public:
    explicit LuaExecutionContext(LuaStatePtr state)
        : state_(std::move(state)) {}

    void *getContext() override { return state_.get(); }
    void releasePtr() override { (void)state_.release(); }

  private:
    LuaStatePtr state_;
};

} // namespace FuzzingAST

#endif // LUA_TARGET_HPP
