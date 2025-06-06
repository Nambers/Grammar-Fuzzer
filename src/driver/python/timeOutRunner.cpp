#include "timeOutRunner.hpp"
#include "log.hpp"
#include <csetjmp>
#include <csignal>
#include <sys/time.h>

static sigjmp_buf timeoutJmp;

static void alarmHandler(int signum) {
    if (signum == SIGALRM) {
        siglongjmp(timeoutJmp, 1);
    }
}

static void set_timeout_ms(int timeout_ms) {
    struct itimerval timer{};
    timer.it_value.tv_sec = timeout_ms / 1000;
    timer.it_value.tv_usec = (timeout_ms % 1000) * 1000;

    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0; // one-shot
    setitimer(ITIMER_REAL, &timer, nullptr);
}

static void clear_timeout() {
    struct itimerval zero = {};
    setitimer(ITIMER_REAL, &zero, nullptr);
}

void installSignalHandler() {
    struct sigaction sa{};
    sa.sa_handler = alarmHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGALRM, &sa, nullptr) == -1) {
        perror("sigaction");
        std::abort();
    }
}

int evalWithTimeOut(PyObject *codeObj, PyObject *globals, int timeout_ms) {
    if (sigsetjmp(timeoutJmp, 1) == 0) {
        set_timeout_ms(timeout_ms);

        PyObject *result = PyEval_EvalCode(codeObj, globals, globals);
        clear_timeout(); // cancel timeout

        if (!result) {
            if (PyErr_Occurred()) {
#ifndef DISABLE_DEBUG_OUTPUT
                PyErr_Print();
#endif
                PyErr_Clear();
                return -1;
            }
        }

        Py_DECREF(result);
        return 0;
    } else {
        clear_timeout(); // just in case
        PyErr_SetString(PyExc_TimeoutError, "Execution timed out. (longjmp)");
        PyErr_Clear();
        ERROR("Timeout during PyEval_EvalCode.");
        return -2;
    }
}
