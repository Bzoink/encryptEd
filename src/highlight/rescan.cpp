#include "highlight/rescan.h"

#include <algorithm>
#include <cstring>

namespace highlight {

namespace {

constexpr unsigned char kNormal = static_cast<unsigned char>(SynStyle::Normal);

// Offset one past the last byte of the line starting at `offset` (the newline
// itself is not part of the line).
std::size_t line_end(std::string_view text, std::size_t offset) {
    const std::size_t nl = text.find('\n', offset);
    return nl == std::string_view::npos ? text.size() : nl;
}

}  // namespace

void Rescanner::reset() {
    checkpoints_.clear();
    runs_.clear();
}

void Rescanner::set_language(SynLang lang) {
    if (lang != lang_) {
        lang_ = lang;
        reset();
    }
}

void Rescanner::scan_line(std::string_view line, SynState &state,
                          unsigned char *out) {
    std::memset(out, kNormal, line.size());
    runs_.clear();
    syn_parse_line(line, state, lang_, runs_);
    for (const SynRun &run : runs_) {
        if (run.start >= line.size()) continue;
        const std::size_t n = std::min(run.len, line.size() - run.start);
        std::memset(out + run.start, static_cast<unsigned char>(run.style), n);
    }
}

void Rescanner::rescan_all(std::string_view text,
                           std::vector<unsigned char> &styles) {
    checkpoints_.clear();
    styles.assign(text.size(), kNormal);

    SynState state{};
    std::size_t offset = 0;
    std::size_t line   = 0;

    for (;;) {
        if (line % kCheckpointLines == 0)
            checkpoints_.push_back({offset, state});
        if (offset >= text.size()) break;

        const std::size_t end = line_end(text, offset);
        if (lang_ != SynLang::None)
            scan_line(text.substr(offset, end - offset), state,
                      styles.data() + offset);

        if (end >= text.size()) break;
        offset = end + 1;
        ++line;
    }
}

Rescanner::DirtyRange Rescanner::rescan_after_edit(
    std::string_view text, std::size_t pos, std::size_t removed,
    std::size_t inserted, std::vector<unsigned char> &styles) {
    if (lang_ == SynLang::None || checkpoints_.empty() ||
        styles.size() != text.size()) {
        rescan_all(text, styles);
        return {0, text.size()};
    }

    const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(inserted) -
                                 static_cast<std::ptrdiff_t>(removed);

    // Checkpoint at or before the edit: its state is still valid.
    std::size_t ci = 0;
    while (ci + 1 < checkpoints_.size() && checkpoints_[ci + 1].offset <= pos)
        ++ci;

    SynState state = checkpoints_[ci].state;
    const std::size_t start_offset = checkpoints_[ci].offset;

    // Old checkpoints after the edit, shifted. Drop those that the edit
    // invalidated or that no longer sit on a line start.
    std::vector<Checkpoint> tail;
    tail.reserve(checkpoints_.size() - ci);
    for (std::size_t i = ci + 1; i < checkpoints_.size(); ++i) {
        const Checkpoint &cp = checkpoints_[i];
        if (cp.offset <= pos + removed) continue;
        const std::ptrdiff_t shifted =
            static_cast<std::ptrdiff_t>(cp.offset) + delta;
        if (shifted < 0) continue;
        const std::size_t off = static_cast<std::size_t>(shifted);
        if (off > text.size()) continue;
        if (off != 0 && text[off - 1] != '\n') continue;
        tail.push_back({off, cp.state});
    }

    std::vector<Checkpoint> fresh;
    fresh.push_back({start_offset, state});

    std::size_t offset      = start_offset;
    std::size_t lines_since = 0;
    std::size_t ti          = 0;
    bool scanned_any        = false;

    for (;;) {
        // Rejoin the untouched tail once the state has settled. At least one
        // line must be rescanned so a same-length edit is never skipped.
        if (scanned_any && ti < tail.size() && tail[ti].offset == offset &&
            tail[ti].state == state) {
            break;
        }
        if (offset >= text.size()) break;

        if (lines_since >= kCheckpointLines) {
            fresh.push_back({offset, state});
            lines_since = 0;
        }

        const std::size_t end = line_end(text, offset);
        scan_line(text.substr(offset, end - offset), state,
                  styles.data() + offset);
        scanned_any = true;

        offset = (end < text.size()) ? end + 1 : text.size();
        ++lines_since;

        while (ti < tail.size() && tail[ti].offset < offset) ++ti;
    }

    const bool rejoined = ti < tail.size() && tail[ti].offset == offset &&
                          tail[ti].state == state && scanned_any;

    checkpoints_.resize(ci);
    checkpoints_.insert(checkpoints_.end(), fresh.begin(), fresh.end());
    if (rejoined)
        checkpoints_.insert(checkpoints_.end(), tail.begin() + ti, tail.end());

    return {start_offset, offset};
}

}  // namespace highlight
