#include "tui/md_parse.h"

#include <cctype>

namespace {
std::size_t leading_spaces(const std::string &s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return i;
}

// Scan inline spans (`code`, **strong**, *emph*) starting at byte `from`,
// appending runs to `out`. Content outside spans is left as Normal (no run).
void scan_inline(const std::string &line, std::size_t from, std::vector<MdRun> &out) {
    std::size_t i = from;
    while (i < line.size()) {
        char c = line[i];
        if (c == '`') {
            std::size_t end = line.find('`', i + 1);
            if (end != std::string::npos) {
                out.push_back({i, end - i + 1, MdStyle::InlineCode});
                i = end + 1;
                continue;
            }
        } else if (c == '*' && i + 1 < line.size() && line[i + 1] == '*') {
            std::size_t end = line.find("**", i + 2);
            if (end != std::string::npos) {
                out.push_back({i, end - i + 2, MdStyle::Strong});
                i = end + 2;
                continue;
            }
        } else if (c == '*') {
            std::size_t end = line.find('*', i + 1);
            if (end != std::string::npos) {
                out.push_back({i, end - i + 1, MdStyle::Emphasis});
                i = end + 1;
                continue;
            }
        } else if (c == '[') {
            std::size_t close = line.find(']', i + 1);
            if (close != std::string::npos && close + 1 < line.size() && line[close + 1] == '(') {
                std::size_t paren = line.find(')', close + 1);
                if (paren != std::string::npos) {
                    out.push_back({i, paren - i + 1, MdStyle::Link});
                    i = paren + 1;
                    continue;
                }
            }
        }
        ++i;
    }
}
}  // namespace

bool md_is_fence(const std::string &line) {
    std::size_t i = leading_spaces(line);
    if (i + 2 >= line.size() + 1 && i + 3 > line.size()) return false;
    if (i + 3 > line.size()) return false;
    char c = line[i];
    if (c != '`' && c != '~') return false;
    return line[i + 1] == c && line[i + 2] == c;
}

std::vector<MdRun> md_parse_line(const std::string &line, bool in_code) {
    std::vector<MdRun> runs;
    if (in_code) {
        if (!line.empty()) runs.push_back({0, line.size(), MdStyle::CodeBlock});
        return runs;
    }

    std::size_t indent = leading_spaces(line);
    std::string trimmed = line.substr(indent);

    // Horizontal rule: ---, ***, ___ (3+).
    if (trimmed.size() >= 3) {
        char r = trimmed[0];
        if (r == '-' || r == '*' || r == '_') {
            bool all = true;
            for (char ch : trimmed) {
                if (ch != r && ch != ' ') { all = false; break; }
            }
            if (all) {
                runs.push_back({0, line.size(), MdStyle::Rule});
                return runs;
            }
        }
    }

    // ATX heading: leading # ... .
    if (!trimmed.empty() && trimmed[0] == '#') {
        std::size_t h = 0;
        while (h < trimmed.size() && trimmed[h] == '#') ++h;
        if (h <= 6 && h < trimmed.size() && trimmed[h] == ' ') {
            runs.push_back({0, line.size(), MdStyle::Heading});
            return runs;
        }
    }

    // Blockquote.
    if (!trimmed.empty() && trimmed[0] == '>') {
        runs.push_back({0, line.size(), MdStyle::Quote});
        return runs;
    }

    // List marker: -, *, + or "N." then a space.
    std::size_t marker_len = 0;
    if (trimmed.size() >= 2 &&
        (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') && trimmed[1] == ' ') {
        marker_len = 2;
    } else {
        std::size_t d = 0;
        while (d < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[d]))) ++d;
        if (d > 0 && d + 1 < trimmed.size() && trimmed[d] == '.' && trimmed[d + 1] == ' ') {
            marker_len = d + 2;
        }
    }
    if (marker_len) {
        runs.push_back({indent, marker_len, MdStyle::ListMarker});
        scan_inline(line, indent + marker_len, runs);
        return runs;
    }

    scan_inline(line, 0, runs);
    return runs;
}
