#ifndef QUICKJS_TARGET_HPP
#define QUICKJS_TARGET_HPP

#include "ast.hpp"
#include <memory>

extern "C" {
#include <quickjs.h>
}

namespace FuzzingAST {

struct QuickJSHandle {
    JSRuntime *rt = nullptr;
    JSContext *ctx = nullptr;
};

struct QuickJSHandleDeleter {
    void operator()(QuickJSHandle *h) const {
        if (!h)
            return;
        if (h->ctx)
            JS_FreeContext(h->ctx);
        if (h->rt)
            JS_FreeRuntime(h->rt);
        delete h;
    }
};

using QuickJSHandlePtr = std::unique_ptr<QuickJSHandle, QuickJSHandleDeleter>;

class QuickJSExecutionContext : public ExecutionContext {
  public:
    explicit QuickJSExecutionContext(QuickJSHandlePtr handle)
        : handle_(std::move(handle)) {}

    void *getContext() override { return handle_.get(); }
    void releasePtr() override { (void)handle_.release(); }

  private:
    QuickJSHandlePtr handle_;
};

} // namespace FuzzingAST

#endif // QUICKJS_TARGET_HPP
