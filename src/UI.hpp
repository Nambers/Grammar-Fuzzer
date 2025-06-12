#ifndef UI_HPP
#define UI_HPP

#include <atomic>
#include <iostream>
#include <sstream>
#include <string>

namespace FuzzingAST {
class FuzzSchedulerState;

namespace TUI {
void update(const FuzzSchedulerState &state, size_t currentASTSize);
void initTUI();
void finalizeTUI();
void writeTUI(const FuzzSchedulerState &state, size_t currentASTSize);
} // namespace TUI
} // namespace FuzzingAST
#endif // UI_HPP