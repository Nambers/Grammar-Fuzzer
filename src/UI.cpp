#include "UI.hpp"
#include "FuzzSchedulerState.hpp"
#include "ast.hpp"
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <signal.h>

using namespace ftxui;
using namespace FuzzingAST::TUI;

const static std::string stage_names[] = {
    "Execution Generation", "Declaration Mutation", "Fallback Old Corpus"};

extern uint32_t newEdgeCnt;
extern uint32_t errCnt;
extern uint32_t corpusSize;

class RingBuffer {
  public:
    explicit RingBuffer(size_t capacity)
        : buffer_(capacity), capacity_(capacity), head_(0), count_(0) {}

    void push_line(const std::string &line) {
        if (capacity_ == 0)
            return;
        size_t idx = (head_ + count_) % capacity_;
        buffer_[idx] = line;
        if (count_ < capacity_) {
            ++count_;
        } else {
            head_ = (head_ + 1) % capacity_;
        }
    }

    std::string str() const {
        std::ostringstream oss;
        for (size_t i = 0; i < count_; ++i) {
            size_t idx = (head_ + i) % capacity_;
            oss << buffer_[idx] << '\n';
        }
        return oss.str();
    }

    std::string last_n_lines(size_t n) const {
        if (n >= count_) {
            return str();
        }
        std::ostringstream oss;
        size_t start = (head_ + (count_ - n)) % capacity_;
        for (size_t i = 0; i < n; ++i) {
            size_t idx = (start + i) % capacity_;
            oss << buffer_[idx] << '\n';
        }
        return oss.str();
    }

  private:
    std::vector<std::string> buffer_;
    size_t capacity_;
    size_t head_;
    size_t count_;
};

class LineBuf : public std::streambuf {
  public:
    LineBuf(RingBuffer &ring) : ring_(ring) {}

  protected:
    int overflow(int ch) override {
        if (ch == EOF)
            return !EOF;
        char c = static_cast<char>(ch);

        if (c == '\r') {
            return ch;
        }

        line_buffer_.push_back(c);
        if (c == '\n') {
            ring_.push_line(line_buffer_);
            line_buffer_.clear();
        }
        return ch;
    }

    std::streamsize xsputn(const char *s, std::streamsize n) override {
        std::streamsize total = 0;
        for (std::streamsize i = 0; i < n; ++i) {
            char c = s[i];
            if (c == '\r')
                continue;
            line_buffer_.push_back(c);
            if (c == '\n') {
                ring_.push_line(line_buffer_);
                line_buffer_.clear();
            }
            total++;
        }
        return total;
    }

    int sync() override {
        if (!line_buffer_.empty()) {
            ring_.push_line(line_buffer_);
            line_buffer_.clear();
        }
        return 0;
    }

  private:
    RingBuffer &ring_;
    std::string line_buffer_;
};

constexpr size_t LOG_LINES = 50;
static RingBuffer stdout_buffer(LOG_LINES);
static RingBuffer stderr_buffer(LOG_LINES);
static LineBuf stdout_linebuf(stdout_buffer);
static LineBuf stderr_linebuf(stderr_buffer);

static std::streambuf *old_cout = nullptr;
static std::streambuf *old_cerr = nullptr;
static std::chrono::steady_clock::time_point start_time;

void FuzzingAST::TUI::initTUI() {
    old_cout = std::cout.rdbuf();
    old_cerr = std::cerr.rdbuf();
    std::cout.rdbuf(&stdout_linebuf);
    std::cerr.rdbuf(&stderr_linebuf);
    // TODO clear screen
    // std::cout << "\x1b[3J\x1b[H\x1b[2J" << std::flush;
    // Screen::Create(Dimension::Full(), Dimension::Full()).Clear();
    start_time = std::chrono::steady_clock::now();
}

void FuzzingAST::TUI::finalizeTUI() {
    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);
}

void FuzzingAST::TUI::writeTUI(const FuzzingAST::FuzzSchedulerState &state,
                               size_t currentASTSize) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - start_time;
    auto secs_total =
        std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    int hours = static_cast<int>(secs_total / 3600);
    int minutes = static_cast<int>((secs_total % 3600) / 60);
    int seconds = static_cast<int>(secs_total % 60);

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ":" << std::setw(2)
        << minutes << ":" << std::setw(2) << seconds;
    std::string uptime_str = oss.str(); // "hh:mm:ss"
    Element time_box = vbox({
                           filler(),
                           text("[Uptime]"),
                           text(uptime_str),
                           filler(),
                       }) |
                       size(WIDTH, EQUAL, 12) | flex | border;

    Element stats =
        vbox({
            filler(),
            hbox({text("Phase: ") | dim, text(stage_names[int(state.phase)]),
                  separator(), text("NewEdges: ") | dim,
                  text(std::to_string(newEdgeCnt)), separator(),
                  text("NoEdge: ") | dim,
                  text(std::to_string(state.noEdgeCount)), separator(),
                  text("ErrCnt: ") | dim, text(std::to_string(errCnt))}),
            hbox({text("ExecStalls: ") | dim,
                  text(std::to_string(state.execStallCount)), separator(),
                  text("ASTSize: ") | dim, text(std::to_string(currentASTSize)),
                  separator(), text("ExecThresh: ") | dim,
                  text(std::to_string(state.execFailureThreshold())),
                  separator(), text("CorpusSize: ") | dim,
                  text(std::to_string(corpusSize))}),
            filler(),
        }) |
        flex;

    Element out_box = vbox({text("[C++ stdout]") | bold,
                            paragraph(stdout_buffer.str()) | flex |
                                size(HEIGHT, EQUAL, LOG_LINES)}) |
                      border;

    Element err_box = vbox({text("[C++ stderr]") | bold,
                            paragraph(stderr_buffer.str()) | flex |
                                size(HEIGHT, EQUAL, LOG_LINES)}) |
                      border;

    Element ui =
        vbox({hbox({stats | flex, time_box | align_right}), separator(),
              hbox({
                  out_box | flex,
                  err_box | flex,
              }) | flex});

    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);
    std::cout << "\x1b[1;1H" << std::flush;

    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(ui));

    Render(screen, ui);
    screen.Print();

    std::cout << std::flush;

    std::cout.rdbuf(&stdout_linebuf);
    std::cerr.rdbuf(&stderr_linebuf);
}