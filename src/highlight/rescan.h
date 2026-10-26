#ifndef ENCRYPTED_HIGHLIGHT_RESCAN_H
#define ENCRYPTED_HIGHLIGHT_RESCAN_H

// Incremental line scanner for multi-line syntax state.
//
// A full scan records a SynState checkpoint every kCheckpointLines lines. After
// an edit the scanner restarts at the nearest checkpoint before the change and
// stops as soon as the recomputed state matches the next recorded checkpoint,
// which keeps typing O(edited region) instead of O(document).
//
// UI-free: the caller supplies the document text and a byte-per-byte style
// vector. Each entry is a SynStyle cast to unsigned char; the GUI maps that to
// its own style characters.

#include <cstddef>
#include <string_view>
#include <vector>

#include "highlight/syn_parse.h"

namespace highlight {

class Rescanner {
public:
    static constexpr std::size_t kCheckpointLines = 64;

    struct Checkpoint {
        std::size_t offset = 0;  // byte offset of a line start
        SynState    state;       // state at the start of that line
    };

    struct DirtyRange {
        std::size_t start = 0;
        std::size_t end   = 0;
    };

    void reset();

    void set_language(SynLang lang);
    SynLang language() const { return lang_; }

    // Full scan. `styles` is resized to text.size() and fully rewritten.
    void rescan_all(std::string_view text, std::vector<unsigned char> &styles);

    // Rescan after an edit that replaced [pos, pos + removed) with `inserted`
    // bytes. `styles` must already be resized to text.size(); only the range
    // returned in DirtyRange is rewritten.
    DirtyRange rescan_after_edit(std::string_view text,
                                 std::size_t pos,
                                 std::size_t removed,
                                 std::size_t inserted,
                                 std::vector<unsigned char> &styles);

    const std::vector<Checkpoint> &checkpoints() const { return checkpoints_; }

private:
    // Writes style bytes for one line and advances `state` past it.
    void scan_line(std::string_view line, SynState &state,
                   unsigned char *out);

    SynLang                  lang_ = SynLang::None;
    std::vector<Checkpoint>  checkpoints_;
    std::vector<SynRun>      runs_;
};

}  // namespace highlight

#endif  // ENCRYPTED_HIGHLIGHT_RESCAN_H
