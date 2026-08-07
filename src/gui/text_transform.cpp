#include "gui/text_transform.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" {
#include "core/utf8.h"
}

void tt_find_line_break(const std::string &text,
                        std::size_t start,
                        std::size_t &content_end,
                        std::size_t &newline_len)
{
    content_end = start;
    while (content_end < text.size() &&
           text[content_end] != '\n' && text[content_end] != '\r') {
        ++content_end;
    }
    newline_len = 0;
    if (content_end < text.size()) {
        newline_len = 1;
        if (text[content_end] == '\r' &&
            content_end + 1 < text.size() &&
            text[content_end + 1] == '\n') {
            newline_len = 2;
        }
    }
}

std::string tt_tabs_to_spaces(const std::string &text, int tab_width,
                               int initial_column)
{
    std::string out;
    out.reserve(text.size());

    const char *ptr = text.data();
    const char *end = ptr + text.size();
    int column = initial_column;

    while (ptr < end) {
        if (*ptr == '\t') {
            int spaces = tab_width - (column % tab_width);
            if (spaces <= 0) spaces = tab_width;
            out.append(static_cast<std::size_t>(spaces), ' ');
            column += spaces;
            ++ptr;
            continue;
        }
        if (*ptr == '\n' || *ptr == '\r') {
            out.push_back(*ptr++);
            column = 0;
            continue;
        }
        int len = 0;
        uint32_t ucs = utf8_decode_next(ptr, end, &len);
        if (len <= 0) len = 1;
        out.append(ptr, static_cast<std::size_t>(len));
        int width = utf8_codepoint_width(ucs);
        if (width > 0) column += width;
        ptr += len;
    }

    return out;
}

std::string tt_spaces_to_tabs(const std::string &text, int tab_width)
{
    std::string out;
    out.reserve(text.size());

    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t line_end = start;
        std::size_t newline_len = 0;
        tt_find_line_break(text, start, line_end, newline_len);

        std::size_t pos = start;
        int indent_columns = 0;
        while (pos < line_end) {
            if (text[pos] == ' ') {
                ++indent_columns;
                ++pos;
            } else if (text[pos] == '\t') {
                int spaces = tab_width - (indent_columns % tab_width);
                if (spaces <= 0) spaces = tab_width;
                indent_columns += spaces;
                ++pos;
            } else {
                break;
            }
        }

        out.append(static_cast<std::size_t>(indent_columns / tab_width), '\t');
        out.append(static_cast<std::size_t>(indent_columns % tab_width), ' ');
        out.append(text, pos, line_end - pos);
        if (newline_len > 0) out.append(text, line_end, newline_len);
        start = line_end + newline_len;
    }

    return out;
}

std::string tt_trim_trailing(const std::string &text)
{
    std::string out;
    out.reserve(text.size());

    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t line_end = start;
        std::size_t newline_len = 0;
        tt_find_line_break(text, start, line_end, newline_len);

        std::size_t trim_end = line_end;
        while (trim_end > start &&
               (text[trim_end - 1] == ' ' || text[trim_end - 1] == '\t')) {
            --trim_end;
        }

        out.append(text, start, trim_end - start);
        if (newline_len > 0) out.append(text, line_end, newline_len);
        start = line_end + newline_len;
    }

    return out;
}

std::string tt_remove_blank_lines(const std::string &text)
{
    std::string out;
    out.reserve(text.size());

    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t line_end = start;
        std::size_t newline_len = 0;
        tt_find_line_break(text, start, line_end, newline_len);

        bool blank = true;
        for (std::size_t i = start; i < line_end; ++i) {
            if (text[i] != ' ' && text[i] != '\t') {
                blank = false;
                break;
            }
        }

        if (!blank) {
            out.append(text, start, line_end - start);
            if (newline_len > 0) out.append(text, line_end, newline_len);
        }

        start = line_end + newline_len;
    }

    return out;
}

std::string tt_wrap_columns(const std::string &text, int width)
{
    if (width <= 0) return text;

    std::string nl = "\n";
    if (text.find("\r\n") != std::string::npos)    nl = "\r\n";
    else if (text.find('\r') != std::string::npos) nl = "\r";

    std::string out;
    out.reserve(text.size() + text.size() / static_cast<std::size_t>(width) + 4);

    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t line_end    = pos;
        std::size_t newline_len = 0;
        tt_find_line_break(text, pos, line_end, newline_len);

        std::size_t line_pos = pos;
        while (line_pos < line_end) {
            std::size_t scan           = line_pos;
            std::size_t last_space_pos = std::string::npos;
            int         col            = 0;

            while (scan < line_end) {
                if (text[scan] == ' ' || text[scan] == '\t')
                    last_space_pos = scan;

                int clen = 0;
                utf8_decode_next(text.data() + scan,
                                 text.data() + line_end, &clen);
                if (clen <= 0) clen = 1;
                ++col;
                scan += static_cast<std::size_t>(clen);

                if (col >= width) break;
            }

            if (scan >= line_end) {
                out.append(text, line_pos, line_end - line_pos);
                line_pos = line_end;
            } else if (last_space_pos != std::string::npos) {
                out.append(text, line_pos, last_space_pos - line_pos);
                out.append(nl);
                line_pos = last_space_pos + 1;
            } else {
                std::size_t break_at = scan;
                while (break_at < line_end &&
                       text[break_at] != ' ' && text[break_at] != '\t')
                    ++break_at;
                if (break_at < line_end) {
                    out.append(text, line_pos, break_at - line_pos);
                    out.append(nl);
                    line_pos = break_at + 1;
                } else {
                    out.append(text, line_pos, line_end - line_pos);
                    line_pos = line_end;
                }
            }
        }

        if (newline_len > 0)
            out.append(text, line_end, newline_len);

        pos = line_end + newline_len;
    }

    return out;
}

std::string tt_ensure_trailing_newline(const std::string &text)
{
    if (text.empty()) return text;

    const char last = text.back();
    if (last == '\n' || last == '\r') return text;

    if (text.find("\r\n") != std::string::npos) return text + "\r\n";
    if (text.find('\r')   != std::string::npos) return text + "\r";
    return text + "\n";
}
