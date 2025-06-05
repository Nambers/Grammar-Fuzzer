#ifndef TimeOutRunner_HPP
#define TimeOutRunner_HPP

#include <Python.h>

void installSignalHandler();
int evalWithTimeOut(PyObject *codeObj, PyObject *globals, int timeout_ms);

#endif // TimeOutRunner_HPP
