#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

extern std::mt19937 rng;

static void havoc_ascii(std::string &s, std::size_t max_bytes,
                        std::size_t rounds = 16) {
    if (max_bytes == 0)
        return;

    if (s.empty()) {
        std::uniform_int_distribution<int> seed_len(1, 6);
        std::uniform_int_distribution<int> ascii('!', '~');
        int n = std::min<std::size_t>(seed_len(rng), max_bytes);
        for (int i = 0; i < n; ++i)
            s.push_back(static_cast<char>(ascii(rng)));
    }

    enum Op { REPLACE, INSERT, DELETE, DUP };
    std::uniform_int_distribution<int> pick_op(0, 3);
    std::uniform_int_distribution<int> ascii('!', '~');
    std::uniform_int_distribution<int> dup_len(1, 6);

    for (std::size_t i = 0; i < rounds && !s.empty(); ++i) {
        std::size_t pos = rng() % s.size();
        switch (static_cast<Op>(pick_op(rng))) {
        case REPLACE:
            s[pos] = static_cast<char>(ascii(rng));
            break;
        case INSERT:
            if (s.size() < max_bytes)
                s.insert(s.begin() + pos, static_cast<char>(ascii(rng)));
            break;
        case DELETE:
            if (s.size() > 1)
                s.erase(s.begin() + pos);
            break;
        case DUP: {
            std::size_t len =
                std::min<std::size_t>(dup_len(rng), s.size() - pos);
            if (s.size() + len <= max_bytes)
                s.insert(pos, s.substr(pos, len));
            break;
        }
        }
    }
}

void havoc(std::string &quoted, std::size_t max_bytes,
                  size_t max_havoc_rounds = 16) {
    if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"')
        return;

    std::string inner{quoted.begin() + 1, quoted.end() - 1};

    havoc_ascii(inner, max_bytes, max_havoc_rounds);

    std::string escaped;
    escaped.reserve(inner.size() * 2);
    for (char c : inner) {
        if (c == '"' || c == '\\')
            escaped.push_back('\\');
        escaped.push_back(c);
    }
    quoted.assign("\"").append(escaped).push_back('"');
}
