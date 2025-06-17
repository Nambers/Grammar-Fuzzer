#ifndef TARGET_HPP
#define TARGET_HPP

#include "ast.hpp"
#include <Python.h>
#include <memory>

namespace FuzzingAST {
struct PyObjectDeleter {
    void operator()(PyObject *obj) const { Py_XDECREF(obj); }
};
using PyObjectPtr = std::unique_ptr<PyObject, PyObjectDeleter>;
class PythonExecutionContext : public ExecutionContext {
  public:
    explicit PythonExecutionContext(PyObjectPtr dict)
        : dict_(std::move(dict)) {}

    void *getContext() override { return dict_.get(); }
    void releasePtr() override { dict_.release(); }

  private:
    PyObjectPtr dict_;
};
} // namespace FuzzingAST

#endif // TARGET_HPP