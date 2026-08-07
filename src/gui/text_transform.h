#ifndef GUI_TEXT_TRANSFORM_H
#define GUI_TEXT_TRANSFORM_H

#include <cstddef>
#include <string>

/* Scan forward from `start` to find the end of the current line.
   On return, `content_end` points one past the last non-newline byte and
   `newline_len` is the byte length of the line terminator (0, 1, or 2). */
void tt_find_line_break(const std::string &text,
                        std::size_t start,
                        std::size_t &content_end,
                        std::size_t &newline_len);

/* Expand tab characters to spaces, honouring Unicode display widths.
   `initial_column` is the display column at which `text` starts (default 0). */
std::string tt_tabs_to_spaces(const std::string &text, int tab_width,
                               int initial_column = 0);

/* Convert runs of leading spaces/tabs to tabs (inverse of tt_tabs_to_spaces). */
std::string tt_spaces_to_tabs(const std::string &text, int tab_width);

/* Remove trailing whitespace (spaces and tabs) from every line. */
std::string tt_trim_trailing(const std::string &text);

/* Remove blank lines (lines that contain only whitespace). */
std::string tt_remove_blank_lines(const std::string &text);

/* Wrap long lines at word boundaries so no line exceeds `width` columns.
   Injected newlines match the dominant style already present in `text`. */
std::string tt_wrap_columns(const std::string &text, int width);

/* Ensure the text ends with a newline, using the text's own line-ending style. */
std::string tt_ensure_trailing_newline(const std::string &text);

#endif /* GUI_TEXT_TRANSFORM_H */
