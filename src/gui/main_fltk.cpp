// EncryptEd — FLTK 1.5 editor
// C++17; all text is UTF-8; files saved as .txx (XChaCha20-Poly1305 + Argon2id)

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Spinner.H>
#include <FL/Fl_Preferences.H>
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>
#include <FL/fl_ask.H>
#include <FL/filename.H>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <deque>
#include <map>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  include <shellapi.h>
#  include <FL/platform.H>   // fl_xid()
#else
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <sys/resource.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <limits.h>
#  include <stdlib.h>   // realpath
#endif

extern "C" {
#include "core/txx_codec.h"
#include "core/txx_format.h"
#include "core/txx_status.h"
#include "core/utf8.h"
#include "platform/file_io.h"
#include "platform/random.h"
#include "platform/secure_mem.h"
}

#include "app/about.h"
#include "app/file_format.h"
#include "app/size_format.h"
#include "gui/text_transform.h"
#include "gui/highlight.h"
#include "gui/plugin_buffer.h"
#include "gui/plugin_dialog.h"

#ifndef ENCRYPTED_APP_AUTHOR
#define ENCRYPTED_APP_AUTHOR "Unknown"
#endif

#ifndef ENCRYPTED_APP_VERSION
#define ENCRYPTED_APP_VERSION "0"
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void wipe_string(std::string &s)
{
    if (!s.empty()) {
        txx_secure_zero(&s[0], s.size());
    }
    s.clear();
}

// RAII helper: wipes a document-text string when its scope ends, including
// early returns.
struct StringWiper {
    std::string &value;
    ~StringWiper() { wipe_string(value); }
};

static void secure_free_text(char *text)
{
    if (text == nullptr) {
        return;
    }
    txx_secure_zero(text, std::strlen(text));
    free(text);
}

// Only launch URLs with an explicitly understood scheme. Anything else
// (file://, custom schemes, embedded paths) must not reach the OS handler,
// since the target text is untrusted document content.
static bool is_safe_external_uri(const std::string &url)
{
    auto scheme_ok = [](const std::string &scheme) {
        for (const std::string &known : {"http", "https", "mailto"}) {
            size_t n = known.size();
            if (scheme.size() == n) {
                bool match = true;
                for (size_t i = 0; i < n; ++i) {
                    if (std::tolower(static_cast<unsigned char>(scheme[i])) != known[i]) {
                        match = false;
                        break;
                    }
                }
                if (match) return true;
            }
        }
        return false;
    };
    size_t colon = url.find(':');
    if (colon == std::string::npos || colon == 0 || colon > 32) return false;
    std::string scheme = url.substr(0, colon);
    for (char ch : scheme) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && ch != '+' && ch != '-' && ch != '.') return false;
    }
    if (url.compare(colon, 3, "://") == 0) {
        return scheme_ok(scheme);
    }
    // mailto: and similar non-hierarchical schemes pass through unchanged.
    return scheme_ok(scheme) && colon + 1u < url.size();
}

static void blend_icon_pixel(unsigned char *pixels,
                             int size,
                             int x,
                             int y,
                             unsigned char red,
                             unsigned char green,
                             unsigned char blue,
                             unsigned char alpha)
{
    if (pixels == nullptr || alpha == 0 || x < 0 || y < 0 || x >= size || y >= size) {
        return;
    }

    unsigned char *pixel = pixels + ((y * size + x) * 4);
    unsigned int inv = 255U - alpha;
    pixel[0] = static_cast<unsigned char>((red * alpha + pixel[0] * inv) / 255U);
    pixel[1] = static_cast<unsigned char>((green * alpha + pixel[1] * inv) / 255U);
    pixel[2] = static_cast<unsigned char>((blue * alpha + pixel[2] * inv) / 255U);
    pixel[3] = static_cast<unsigned char>(alpha + ((pixel[3] * inv) / 255U));
}

static void fill_icon_round_rect(unsigned char *pixels,
                                 int size,
                                 double left,
                                 double top,
                                 double width,
                                 double height,
                                 double radius,
                                 unsigned char red,
                                 unsigned char green,
                                 unsigned char blue,
                                 unsigned char alpha)
{
    const double right = left + width;
    const double bottom = top + height;
    const double inner_left = left + radius;
    const double inner_right = right - radius;
    const double inner_top = top + radius;
    const double inner_bottom = bottom - radius;

    for (int y = static_cast<int>(top); y < static_cast<int>(bottom + 1.0); ++y) {
        for (int x = static_cast<int>(left); x < static_cast<int>(right + 1.0); ++x) {
            const double px = static_cast<double>(x) + 0.5;
            const double py = static_cast<double>(y) + 0.5;
            const double nearest_x = std::max(inner_left, std::min(px, inner_right));
            const double nearest_y = std::max(inner_top, std::min(py, inner_bottom));
            const double dx = px - nearest_x;
            const double dy = py - nearest_y;

            if (radius <= 0.0 || dx * dx + dy * dy <= radius * radius) {
                blend_icon_pixel(pixels, size, x, y, red, green, blue, alpha);
            }
        }
    }
}

static double triangle_edge(double ax, double ay, double bx, double by, double px, double py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void fill_icon_triangle(unsigned char *pixels,
                               int size,
                               double ax,
                               double ay,
                               double bx,
                               double by,
                               double cx,
                               double cy,
                               unsigned char red,
                               unsigned char green,
                               unsigned char blue,
                               unsigned char alpha)
{
    const int min_x = static_cast<int>(std::min(ax, std::min(bx, cx)));
    const int max_x = static_cast<int>(std::max(ax, std::max(bx, cx)) + 1.0);
    const int min_y = static_cast<int>(std::min(ay, std::min(by, cy)));
    const int max_y = static_cast<int>(std::max(ay, std::max(by, cy)) + 1.0);

    for (int y = min_y; y < max_y; ++y) {
        for (int x = min_x; x < max_x; ++x) {
            const double px = static_cast<double>(x) + 0.5;
            const double py = static_cast<double>(y) + 0.5;
            const double e1 = triangle_edge(ax, ay, bx, by, px, py);
            const double e2 = triangle_edge(bx, by, cx, cy, px, py);
            const double e3 = triangle_edge(cx, cy, ax, ay, px, py);
            const bool has_neg = e1 < 0.0 || e2 < 0.0 || e3 < 0.0;
            const bool has_pos = e1 > 0.0 || e2 > 0.0 || e3 > 0.0;

            if (!(has_neg && has_pos)) {
                blend_icon_pixel(pixels, size, x, y, red, green, blue, alpha);
            }
        }
    }
}

static void fill_icon_circle(unsigned char *pixels,
                             int size,
                             double center_x,
                             double center_y,
                             double radius,
                             unsigned char red,
                             unsigned char green,
                             unsigned char blue,
                             unsigned char alpha)
{
    const int min_x = static_cast<int>(center_x - radius);
    const int max_x = static_cast<int>(center_x + radius + 1.0);
    const int min_y = static_cast<int>(center_y - radius);
    const int max_y = static_cast<int>(center_y + radius + 1.0);

    for (int y = min_y; y < max_y; ++y) {
        for (int x = min_x; x < max_x; ++x) {
            const double dx = (static_cast<double>(x) + 0.5) - center_x;
            const double dy = (static_cast<double>(y) + 0.5) - center_y;
            if (dx * dx + dy * dy <= radius * radius) {
                blend_icon_pixel(pixels, size, x, y, red, green, blue, alpha);
            }
        }
    }
}

static void fill_icon_segment(unsigned char *pixels,
                              int size,
                              double ax,
                              double ay,
                              double bx,
                              double by,
                              double radius,
                              unsigned char red,
                              unsigned char green,
                              unsigned char blue,
                              unsigned char alpha)
{
    const double vx = bx - ax;
    const double vy = by - ay;
    const double vv = vx * vx + vy * vy;
    const int min_x = static_cast<int>(std::min(ax, bx) - radius - 1.0);
    const int max_x = static_cast<int>(std::max(ax, bx) + radius + 1.0);
    const int min_y = static_cast<int>(std::min(ay, by) - radius - 1.0);
    const int max_y = static_cast<int>(std::max(ay, by) + radius + 1.0);

    for (int y = min_y; y < max_y; ++y) {
        for (int x = min_x; x < max_x; ++x) {
            const double px = static_cast<double>(x) + 0.5;
            const double py = static_cast<double>(y) + 0.5;
            double t = ((px - ax) * vx + (py - ay) * vy) / vv;
            t = std::max(0.0, std::min(1.0, t));
            const double dx = px - (ax + t * vx);
            const double dy = py - (ay + t * vy);
            if (dx * dx + dy * dy <= radius * radius) {
                blend_icon_pixel(pixels, size, x, y, red, green, blue, alpha);
            }
        }
    }
}

static void fill_icon_top_ring(unsigned char *pixels,
                               int size,
                               double center_x,
                               double center_y,
                               double outer_radius,
                               double inner_radius,
                               unsigned char red,
                               unsigned char green,
                               unsigned char blue,
                               unsigned char alpha)
{
    const int min_x = static_cast<int>(center_x - outer_radius);
    const int max_x = static_cast<int>(center_x + outer_radius + 1.0);
    const int min_y = static_cast<int>(center_y - outer_radius);
    const int max_y = static_cast<int>(center_y + 1.0);

    for (int y = min_y; y < max_y; ++y) {
        for (int x = min_x; x < max_x; ++x) {
            const double dx = (static_cast<double>(x) + 0.5) - center_x;
            const double dy = (static_cast<double>(y) + 0.5) - center_y;
            const double d2 = dx * dx + dy * dy;
            if (d2 <= outer_radius * outer_radius && d2 >= inner_radius * inner_radius && dy <= 0.0) {
                blend_icon_pixel(pixels, size, x, y, red, green, blue, alpha);
            }
        }
    }
}

static Fl_RGB_Image *create_app_icon_image(int size)
{
    const double unit = static_cast<double>(size) / 64.0;
    unsigned char *pixels = new unsigned char[size * size * 4]();

    fill_icon_round_rect(pixels, size, 5.0 * unit, 5.0 * unit, 54.0 * unit, 54.0 * unit, 14.0 * unit,
                         13, 31, 48, 255);

    fill_icon_round_rect(pixels, size, 14.0 * unit, 10.0 * unit, 26.0 * unit, 35.0 * unit, 4.0 * unit,
                         244, 246, 248, 255);
    fill_icon_triangle(pixels, size,
                       31.0 * unit, 10.0 * unit,
                       40.0 * unit, 10.0 * unit,
                       40.0 * unit, 19.0 * unit,
                       217, 227, 236, 255);
    fill_icon_triangle(pixels, size,
                       31.0 * unit, 10.0 * unit,
                       40.0 * unit, 19.0 * unit,
                       31.0 * unit, 19.0 * unit,
                       229, 236, 242, 255);
    fill_icon_round_rect(pixels, size, 18.0 * unit, 18.0 * unit, 15.0 * unit, 2.5 * unit, 1.2 * unit,
                         143, 167, 184, 255);
    fill_icon_round_rect(pixels, size, 18.0 * unit, 24.0 * unit, 11.0 * unit, 2.5 * unit, 1.2 * unit,
                         143, 167, 184, 255);
    fill_icon_round_rect(pixels, size, 18.0 * unit, 30.0 * unit, 13.0 * unit, 2.5 * unit, 1.2 * unit,
                         143, 167, 184, 255);

    fill_icon_segment(pixels, size,
                      17.0 * unit, 46.0 * unit,
                      35.0 * unit, 28.0 * unit,
                      4.6 * unit,
                      240, 169, 62, 255);
    fill_icon_segment(pixels, size,
                      20.0 * unit, 43.0 * unit,
                      38.0 * unit, 25.0 * unit,
                      1.2 * unit,
                      250, 212, 127, 220);
    fill_icon_triangle(pixels, size,
                       34.5 * unit, 22.5 * unit,
                       41.0 * unit, 29.0 * unit,
                       38.0 * unit, 18.5 * unit,
                       122, 91, 63, 255);
    fill_icon_triangle(pixels, size,
                       37.1 * unit, 20.0 * unit,
                       39.3 * unit, 22.2 * unit,
                       38.4 * unit, 18.9 * unit,
                       30, 34, 39, 255);

    fill_icon_top_ring(pixels, size, 43.5 * unit, 38.0 * unit, 7.0 * unit, 4.6 * unit,
                       211, 222, 232, 255);
    fill_icon_round_rect(pixels, size, 36.0 * unit, 38.0 * unit, 15.0 * unit, 14.0 * unit, 3.5 * unit,
                         36, 190, 146, 255);
    fill_icon_circle(pixels, size, 43.5 * unit, 45.0 * unit, 2.1 * unit,
                     11, 72, 57, 255);
    fill_icon_round_rect(pixels, size, 42.6 * unit, 45.0 * unit, 1.8 * unit, 4.2 * unit, 0.8 * unit,
                         11, 72, 57, 255);

    Fl_RGB_Image *image = new Fl_RGB_Image(pixels, size, size, 4);
    image->alloc_array = 1;
    return image;
}

static Fl_RGB_Image *app_icon_image(int size)
{
    static Fl_RGB_Image *icon16 = create_app_icon_image(16);
    static Fl_RGB_Image *icon32 = create_app_icon_image(32);
    static Fl_RGB_Image *icon64 = create_app_icon_image(64);

    switch (size) {
    case 16:
        return icon16;
    case 32:
        return icon32;
    default:
        return icon64;
    }
}

static void apply_app_icon(Fl_Window &window)
{
    const Fl_RGB_Image *icons[3] = { app_icon_image(16), app_icon_image(32), app_icon_image(64) };

    window.icons(icons, 3);
}

// Decode a percent-encoded URI path component.
static std::string url_decode(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = { s[i + 1], s[i + 2], '\0' };
            char *end   = nullptr;
            long  v     = strtol(hex, &end, 16);
            if (end == hex + 2) { out += static_cast<char>(v); i += 2; continue; }
        }
        out += s[i];
    }
    return out;
}

// Blocking modal password dialog.
// Returns empty string on Cancel.
static std::string ask_password(const char *title, const char *prompt)
{
    Fl_Window *dlg = new Fl_Window(360, 115, title);
    dlg->set_modal();
    Fl_Box *lbl = new Fl_Box(10, 10, 340, 25, prompt);
    lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    Fl_Input *inp = new Fl_Input(10, 42, 340, 28);
    inp->type(FL_SECRET_INPUT);
    Fl_Return_Button *ok  = new Fl_Return_Button(192, 78, 80, 26, "OK");
    Fl_Button        *can = new Fl_Button(282, 78, 68, 26, "Cancel");
    dlg->end();

    bool accepted = false;
    ok->callback([](Fl_Widget *w, void *v) {
        *static_cast<bool *>(v) = true;
        w->window()->hide();
    }, &accepted);
    can->callback([](Fl_Widget *w, void *) { w->window()->hide(); });

    dlg->show();
    while (dlg->shown()) Fl::wait();

    std::string result;
    if (accepted) result = inp->value();
    // Best-effort wipe of the Fl_Input internal buffer.  FLTK does not
    // guarantee zeroing on destruction (and may have already re-allocated
    // its buffer growing into a larger pool), but overwriting in place with
    // random bytes and then zeros at the current length covers the common
    // case where the storage hasn't moved.
    if (inp->size() > 0) {
        const std::size_t n = static_cast<std::size_t>(inp->size());
        std::vector<uint8_t> noise(n);
        if (txx_random_bytes(noise.data(), n) == TXX_OK) {
            // Restrict to printable ASCII so Fl_Input doesn't reject control
            // bytes; the goal is to overwrite memory, not produce valid UTF-8.
            for (std::size_t i = 0; i < n; ++i) noise[i] = 0x20 + (noise[i] % 0x5F);
            inp->replace(0, inp->size(),
                         reinterpret_cast<const char *>(noise.data()),
                         static_cast<int>(n));
            txx_secure_zero(noise.data(), n);
        }
        std::string zeros(n, '\0');
        inp->replace(0, inp->size(), zeros.c_str(), static_cast<int>(n));
        wipe_string(zeros);
    }
    inp->value("");
    delete dlg;
    return result;
}

// Prompts twice for a new password; up to 3 attempts on mismatch.
// Returns false on cancel.
static bool ask_password_new(const char *title, std::string &out)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::string p1 = ask_password(title, "New password:");
        if (p1.empty()) return false;
        std::string p2 = ask_password(title, "Confirm password:");
        if (p1 == p2) {
            out = p1;
            wipe_string(p1);
            wipe_string(p2);
            return true;
        }
        wipe_string(p1);
        wipe_string(p2);
        fl_alert("Passwords do not match. Please try again.");
    }
    return false;
}

static void show_about_dialog()
{
    Fl_Window *dlg = new Fl_Window(560, 450, "About EncryptEd");
    dlg->set_modal();
    apply_app_icon(*dlg);

    Fl_Box *icon = new Fl_Box(24, 24, 72, 72);
    icon->box(FL_NO_BOX);
    icon->image(app_icon_image(64));

    Fl_Box *title = new Fl_Box(112, 24, 286, 30, "EncryptEd");
    title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    title->labelfont(FL_HELVETICA_BOLD);
    title->labelsize(24);

    Fl_Box *subtitle = new Fl_Box(112, 56, 286, 24, "Encrypted UTF-8 text editor");
    subtitle->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    subtitle->labelsize(13);

    std::string fltk_version = std::to_string(FL_MAJOR_VERSION) + "." +
        std::to_string(FL_MINOR_VERSION) + "." + std::to_string(FL_PATCH_VERSION);
    std::string body_text = about_text("EncryptEd - encrypted UTF-8 text editor",
        "FLTK " + fltk_version + " - LGPL with exceptions (GUI)");

    Fl_Box *body = new Fl_Box(24, 108, 512, 292, nullptr);
    body->copy_label(body_text.c_str());
    body->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    body->labelsize(12);

    Fl_Return_Button *ok = new Fl_Return_Button(456, 410, 80, 28, "OK");
    dlg->end();

    ok->callback([](Fl_Widget *w, void *) { w->window()->hide(); });

    dlg->show();
    while (dlg->shown()) Fl::wait();
    delete dlg;
}

// Blocking modal column-width dialog.
// Returns the requested width (>= 1) or -1 on cancel / invalid input.
static int ask_column_width()
{
    Fl_Window *dlg = new Fl_Window(280, 115, "Column Wrap");
    dlg->set_modal();
    Fl_Box *lbl = new Fl_Box(10, 10, 260, 25, "Wrap width (characters):");
    lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    Fl_Input *inp = new Fl_Input(10, 42, 260, 28);
    inp->value("72");
    Fl_Return_Button *ok  = new Fl_Return_Button(112, 78, 80, 26, "OK");
    Fl_Button        *can = new Fl_Button(202, 78, 68, 26, "Cancel");
    dlg->end();

    bool accepted = false;
    ok->callback([](Fl_Widget *w, void *v) {
        *static_cast<bool *>(v) = true;
        w->window()->hide();
    }, &accepted);
    can->callback([](Fl_Widget *w, void *) { w->window()->hide(); });

    dlg->show();
    while (dlg->shown()) Fl::wait();

    int result = -1;
    if (accepted) {
        const char *val = inp->value();
        char *end_ptr   = nullptr;
        long parsed     = val ? strtol(val, &end_ptr, 10) : 0;
        if (val && end_ptr != val && *end_ptr == '\0' && parsed > 0 && parsed <= 9999)
            result = static_cast<int>(parsed);
        else
            fl_alert("Please enter a positive number.");
    }
    delete dlg;
    return result;
}

// ---------------------------------------------------------------------------
// Application settings
// ---------------------------------------------------------------------------

struct AppSettings {
    enum IndentMode {
        INDENT_TABS = 0,
        INDENT_SPACES = 1,
    };

    enum StatusCount {
        STATUS_COUNT_CHARS = 0,
        STATUS_COUNT_SIZE = 1, // Preserve the existing File size preference.
        STATUS_COUNT_BYTES = 2,
    };

    Fl_Font     font               = FL_COURIER;
    Fl_Fontsize size               = 14;
    bool        dark_mode          = false;
    int         highlight_mode     = 1;   // 0 off, 1 auto, 2 markdown, 3 language
    SynLang     highlight_lang     = SynLang::None;
    bool        vertical_scrollbar = true;
    IndentMode  indent_mode        = INDENT_TABS;
    int         indent_width       = 4;
    StatusCount status_count       = STATUS_COUNT_CHARS;
};

static constexpr int kIndentWidthMin = 2;
static constexpr int kIndentWidthMax = 8;

// Above this size, per-keystroke code highlighting costs more than it is
// worth; the preference stays unchanged and the status bar explains why.
static constexpr int kMaxHighlightBytes = 2 * 1024 * 1024;

// Languages offered by the highlighting preference. Markdown has its own
// entry in the choice, so it is omitted here.
struct HighlightLangEntry { const char *label; SynLang lang; };
static const HighlightLangEntry kHighlightLangs[] = {
    {"C", SynLang::C},
    {"C++", SynLang::Cpp},
    {"Python", SynLang::Python},
    {"Shell", SynLang::Shell},
    {"CMake", SynLang::CMake},
    {"Rust", SynLang::Rust},
    {"JavaScript", SynLang::JavaScript},
    {"HTML", SynLang::HTML},
    {"CSS", SynLang::CSS},
    {"PowerShell", SynLang::PowerShell},
    {"Go", SynLang::Go},
    {"PHP", SynLang::PHP},
    {"JSON", SynLang::JSON},
    {"SQL", SynLang::SQL},
    {"Dockerfile", SynLang::Dockerfile},
    {"YAML", SynLang::YAML},
    {"XML", SynLang::XML},
    {"Java", SynLang::Java},
    {"C#", SynLang::CSharp},
    {"Makefile", SynLang::Makefile},
    {"Batch", SynLang::Batch},
    {"Ruby", SynLang::Ruby},
    {"Perl", SynLang::Perl},
    {"TOML", SynLang::TOML},
    {"INI", SynLang::INI},
    {"R", SynLang::R},
    {"Swift", SynLang::Swift},
    {"Kotlin", SynLang::Kotlin},
    {"Lua", SynLang::Lua},
    {"Assembly", SynLang::Asm},
};
static constexpr int kHighlightLangCount =
    static_cast<int>(sizeof(kHighlightLangs) / sizeof(kHighlightLangs[0]));

struct FontEntry { std::string label; Fl_Font font; };
static std::vector<FontEntry> g_mono_fonts;

// Scan system fonts for monospace faces.  Must be called after a window
// has been shown so that fl_width() has an active graphics context.
static void init_font_list()
{
    if (!g_mono_fonts.empty()) return;

    // FLTK built-in Courier is always available and always monospace.
    g_mono_fonts.push_back({"Courier", FL_COURIER});

    // Canonical system name of FL_COURIER (e.g. "Courier New" on Windows).
    // We use it to skip the system copy so Courier never becomes a submenu.
    const char *courier_sys = Fl::get_font_name(FL_COURIER);

    // Load all system fonts (assigns IDs starting at 16).
    int n = Fl::set_fonts("*");
    for (int i = 16; i < n; ++i) {
        int attr = 0;
        const char *raw = Fl::get_font_name(i, &attr);
        if (!raw) continue;
        if (attr & (FL_BOLD | FL_ITALIC)) continue;   // regular faces only

        // Trim leading/trailing ASCII whitespace (Windows sometimes returns
        // names like " Gothic" or "Lucida Console ").
        const char *start = raw;
        while (*start == ' ' || *start == '\t') ++start;
        size_t len = strlen(start);
        while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) --len;
        if (len == 0) continue;
        std::string name(start, len);

        // Skip names with non-ASCII bytes (render as squares in the menu).
        bool ascii = true;
        for (char c : name)
            if ((unsigned char)c > 126) { ascii = false; break; }
        if (!ascii) continue;

        // Skip names that don't start with an ASCII letter — filters out old
        // bitmap/OEM fonts ("8514oem"), vertical CJK variants ("@MS Gothic"), etc.
        char fc = name[0];
        if (!((fc >= 'A' && fc <= 'Z') || (fc >= 'a' && fc <= 'z'))) continue;

        // Skip the system copy of FL_COURIER (e.g. "Courier New") and any exact
        // "Courier" match — prevents a spurious "Courier →" submenu.
        if (name == "Courier") continue;
        if (courier_sys && name == courier_sys) continue;

        fl_font(i, 14);
        double wi = fl_width("i", 1);
        double wW = fl_width("W", 1);
        if (wi < 0.5 || std::fabs(wi - wW) >= 0.5) continue;  // not monospace

        g_mono_fonts.push_back({name, static_cast<Fl_Font>(i)});
    }

    // Sort alphabetically, keeping Courier pinned at the top.
    std::sort(g_mono_fonts.begin() + 1, g_mono_fonts.end(),
              [](const FontEntry &a, const FontEntry &b){ return a.label < b.label; });
}

static Fl_Font font_by_name(const char *name)
{
    if (name && *name)
        for (const auto &fe : g_mono_fonts)
            if (fe.label == name) return fe.font;
    return FL_COURIER;
}

static int clamp_indent_width(int width)
{
    if (width < kIndentWidthMin) return kIndentWidthMin;
    if (width > kIndentWidthMax) return kIndentWidthMax;
    return width;
}

#if defined(_WIN32)
static constexpr txx_newline_style kDefaultPlatformNewlineStyle = TXX_NEWLINE_CRLF;
#else
static constexpr txx_newline_style kDefaultPlatformNewlineStyle = TXX_NEWLINE_LF;
#endif

struct DocumentTextState {
    txx_text_encoding source_encoding = TXX_TEXT_ENCODING_UTF8;
    txx_newline_style newline_style   = kDefaultPlatformNewlineStyle;
    bool              mixed_newlines  = false;
    bool              transcoded      = false;
    bool              had_bom         = false;
};

// ---------------------------------------------------------------------------
// Find / Replace window (non-modal, stays on screen)
// ---------------------------------------------------------------------------

class FindReplaceWindow : public Fl_Window {
    Fl_Input        *find_inp_;
    Fl_Input        *repl_inp_;
    Fl_Check_Button *case_btn_;
    Fl_Text_Buffer  *buf_    = nullptr;
    Fl_Text_Editor  *editor_ = nullptr;

    static void find_cb(Fl_Widget *, void *v)     { static_cast<FindReplaceWindow *>(v)->do_find(); }
    static void repl_cb(Fl_Widget *, void *v)     { static_cast<FindReplaceWindow *>(v)->do_replace(); }
    static void repl_all_cb(Fl_Widget *, void *v) { static_cast<FindReplaceWindow *>(v)->do_replace_all(); }
    static void close_cb(Fl_Widget *, void *v)    { static_cast<Fl_Window *>(v)->hide(); }

    void do_find(bool previous = false)
    {
        if (!buf_ || find_inp_->size() == 0) return;
        int start = editor_ ? editor_->insert_position() : 0;
        if (buf_->selected()) {
            int first, last;
            buf_->selection_position(&first, &last);
            start = previous ? first : last;
        }
        int found = 0;
        bool matched;
        if (previous) {
            matched = start > 0 && buf_->search_backward(start - 1, find_inp_->value(), &found, case_btn_->value());
            if (!matched) matched = buf_->search_backward(buf_->length(), find_inp_->value(), &found, case_btn_->value());
        } else {
            matched = buf_->search_forward(start, find_inp_->value(), &found, case_btn_->value());
            if (!matched) matched = buf_->search_forward(0, find_inp_->value(), &found, case_btn_->value());
        }
        if (matched) {
            buf_->select(found, found + static_cast<int>(strlen(find_inp_->value())));
            if (editor_) {
                editor_->insert_position(found);
                editor_->show_insert_position();
            }
        } else fl_beep();
    }

    void do_replace()
    {
        if (!buf_ || find_inp_->size() == 0) return;
        int first = 0, last = 0, found = 0;
        if (!buf_->selected() || !buf_->selection_position(&first, &last) ||
            last - first != find_inp_->size() ||
            !buf_->search_forward(first, find_inp_->value(), &found, case_btn_->value()) || found != first) {
            do_find(); return;
        }
        buf_->replace_selection(repl_inp_->value());
        do_find();
    }

    void do_replace_all()
    {
        if (!buf_ || find_inp_->size() == 0) return;
        int pos      = 0;
        int count    = 0;
        int found    = 0;
        int find_len = static_cast<int>(strlen(find_inp_->value()));
        int repl_len = static_cast<int>(strlen(repl_inp_->value()));
        while (buf_->search_forward(pos, find_inp_->value(), &found, case_btn_->value())) {
            buf_->replace(found, found + find_len, repl_inp_->value());
            pos = found + repl_len;
            ++count;
            if (pos > buf_->length()) break;
        }
        if (count == 0) fl_beep();
    }

public:
    FindReplaceWindow()
        : Fl_Window(400, 155, "Find / Replace")
    {
        Fl_Box *fl = new Fl_Box(10, 10, 65, 24, "Find:");
        fl->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        find_inp_ = new Fl_Input(80, 10, 310, 24);

        Fl_Box *rl = new Fl_Box(10, 42, 65, 24, "Replace:");
        rl->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        repl_inp_ = new Fl_Input(80, 42, 310, 24);

        case_btn_ = new Fl_Check_Button(10, 74, 130, 24, "Match case");

        Fl_Return_Button *fn = new Fl_Return_Button(10,  118,  80, 26, "Find");
        Fl_Button        *rp = new Fl_Button(100,  118,  80, 26, "Replace");
        Fl_Button        *ra = new Fl_Button(190,  118, 110, 26, "Replace All");
        Fl_Button        *cl = new Fl_Button(330,  118,  60, 26, "Close");

        fn->callback(find_cb,     this);
        rp->callback(repl_cb,     this);
        ra->callback(repl_all_cb, this);
        cl->callback(close_cb,    this);

        end();
        set_non_modal();
    }

    void attach(Fl_Text_Editor *ed, Fl_Text_Buffer *buf)
    {
        editor_ = ed;
        buf_    = buf;
    }

    void show_find()
    {
        show();
        find_inp_->take_focus();
    }

    void find_again(bool previous) {
        if (find_inp_->size() == 0) { show_find(); return; }
        do_find(previous);
    }

    int handle(int event) override {
        if ((event == FL_KEYDOWN || event == FL_SHORTCUT) &&
            (Fl::event_key() == FL_F + 2 || Fl::event_key() == FL_F + 3)) {
            find_again(Fl::event_key() == FL_F + 2);
            return 1;
        }
        return Fl_Window::handle(event);
    }
};

// ---------------------------------------------------------------------------
// Preferences dialog
// ---------------------------------------------------------------------------

class PrefsDialog : public Fl_Window {
    Fl_Choice       *font_choice_;
    Fl_Spinner      *size_spin_;
    Fl_Choice       *indent_choice_;
    Fl_Box          *indent_width_label_;
    Fl_Spinner      *indent_width_spin_;
    Fl_Check_Button *dark_btn_;
    Fl_Choice       *hl_choice_;
    Fl_Check_Button *scrollbar_btn_;
    Fl_Choice       *status_count_choice_;
    AppSettings      result_;
    bool             accepted_ = false;

    void update_indent_width_state()
    {
        const bool spaces_mode = indent_choice_->value() == 1;
        if (spaces_mode) {
            indent_width_label_->activate();
            indent_width_spin_->activate();
        } else {
            indent_width_label_->deactivate();
            indent_width_spin_->deactivate();
        }
    }

    static void ok_cb(Fl_Widget *w, void *v)
    {
        auto *d = static_cast<PrefsDialog *>(v);
        const Fl_Menu_Item *item = d->font_choice_->mvalue();
        d->result_.font = item ? static_cast<Fl_Font>(reinterpret_cast<intptr_t>(item->user_data()))
                               : FL_COURIER;
        d->result_.size         = static_cast<Fl_Fontsize>(d->size_spin_->value());
        d->result_.indent_mode  = d->indent_choice_->value() == 1
                                ? AppSettings::INDENT_SPACES
                                : AppSettings::INDENT_TABS;
        d->result_.indent_width = clamp_indent_width(static_cast<int>(d->indent_width_spin_->value()));
        d->result_.dark_mode          = d->dark_btn_->value() != 0;
        const int hi = d->hl_choice_->value();
        if (hi <= 2) {
            d->result_.highlight_mode = hi;
            d->result_.highlight_lang = SynLang::None;
        } else {
            const Fl_Menu_Item *it = d->hl_choice_->mvalue();
            d->result_.highlight_mode = 3;
            d->result_.highlight_lang = it
                ? static_cast<SynLang>(reinterpret_cast<intptr_t>(it->user_data()))
                : SynLang::None;
        }
        d->result_.vertical_scrollbar = d->scrollbar_btn_->value() != 0;
        d->result_.status_count = d->status_count_choice_->value() == 1
                                ? AppSettings::STATUS_COUNT_BYTES
                                : d->status_count_choice_->value() == 2
                                    ? AppSettings::STATUS_COUNT_SIZE
                                    : AppSettings::STATUS_COUNT_CHARS;
        d->accepted_            = true;
        w->window()->hide();
    }
    static void indent_mode_cb(Fl_Widget *, void *v)
    {
        static_cast<PrefsDialog *>(v)->update_indent_width_state();
    }
    static void cancel_cb(Fl_Widget *w, void *) { w->window()->hide(); }

public:
    explicit PrefsDialog(const AppSettings &current)
        : Fl_Window(310, 367, "Preferences"), result_(current)
    {
        set_modal();

        // Font family
        Fl_Box *fl = new Fl_Box(10, 14, 90, 24, "Font:");
        fl->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        font_choice_ = new Fl_Choice(108, 14, 190, 24);
        // Group fonts that share the same first word into submenus (e.g. all "Noto …"
        // fonts appear under a "Noto" submenu).  Fonts whose first word is unique
        // are listed at the top level.
        for (const auto &fe : g_mono_fonts) {
            auto sep = fe.label.find(' ');
            std::string first = fe.label.substr(0, sep);   // whole name if no space
            int siblings = 0;
            for (const auto &fe2 : g_mono_fonts) {
                if (fe2.label.size() >= first.size() &&
                    fe2.label.compare(0, first.size(), first) == 0 &&
                    (fe2.label.size() == first.size() || fe2.label[first.size()] == ' '))
                    ++siblings;
            }
            std::string mlabel = (siblings >= 2) ? first + "/" + fe.label : fe.label;
            font_choice_->add(mlabel.c_str(), 0, nullptr,
                              reinterpret_cast<void*>(static_cast<intptr_t>(fe.font)),
                              FL_MENU_RADIO);
        }
        // Restore the current selection by matching the stored font ID in user_data.
        {
            void *target = reinterpret_cast<void*>(static_cast<intptr_t>(current.font));
            const Fl_Menu_Item *items = font_choice_->menu();
            for (int i = 0, n = font_choice_->size(); i < n; ++i) {
                if (!(items[i].flags & FL_SUBMENU) && items[i].user_data() == target) {
                    font_choice_->value(i);
                    // Mark this radio item as set so the popup shows a check.
                    font_choice_->setonly(const_cast<Fl_Menu_Item *>(&items[i]));
                    break;
                }
            }
        }

        // Font size
        Fl_Box *sl = new Fl_Box(10, 50, 90, 24, "Size:");
        sl->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        size_spin_ = new Fl_Spinner(108, 50, 80, 24);
        size_spin_->minimum(8);
        size_spin_->maximum(36);
        size_spin_->step(1);
        size_spin_->value(current.size);

        // Indentation mode
        Fl_Box *il = new Fl_Box(10, 86, 90, 24, "Tab key:");
        il->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        indent_choice_ = new Fl_Choice(108, 86, 190, 24);
        indent_choice_->add("Tab character");
        indent_choice_->add("Spaces");
        indent_choice_->value(current.indent_mode == AppSettings::INDENT_SPACES ? 1 : 0);
        indent_choice_->callback(indent_mode_cb, this);

        // Indentation width
        indent_width_label_ = new Fl_Box(10, 122, 90, 24, "Indent width:");
        indent_width_label_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        indent_width_spin_ = new Fl_Spinner(108, 122, 80, 24);
        indent_width_spin_->minimum(kIndentWidthMin);
        indent_width_spin_->maximum(kIndentWidthMax);
        indent_width_spin_->step(1);
        indent_width_spin_->value(current.indent_width);

        // Dark mode
        dark_btn_ = new Fl_Check_Button(108, 158, 190, 24, "Dark mode");
        dark_btn_->value(current.dark_mode ? 1 : 0);

        // Syntax highlighting mode
        Fl_Box *hl = new Fl_Box(10, 194, 90, 24, "Highlighting:");
        hl->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        hl_choice_ = new Fl_Choice(108, 194, 190, 24);
        hl_choice_->add("Off");
        hl_choice_->add("Auto (file type)");
        hl_choice_->add("Markdown");
        for (int i = 0; i < kHighlightLangCount; ++i) {
            hl_choice_->add(kHighlightLangs[i].label, 0, nullptr,
                            reinterpret_cast<void *>(
                                static_cast<intptr_t>(kHighlightLangs[i].lang)),
                            i == 0 ? FL_MENU_DIVIDER : 0);
        }
        {
            int sel = 1;
            if (current.highlight_mode == 0) sel = 0;
            else if (current.highlight_mode == 2) sel = 2;
            else if (current.highlight_mode == 3) {
                sel = 1;
                for (int i = 0; i < kHighlightLangCount; ++i) {
                    if (kHighlightLangs[i].lang == current.highlight_lang) {
                        sel = 3 + i;
                        break;
                    }
                }
            }
            hl_choice_->value(sel);
        }

        // Status bar counter mode
        Fl_Box *scl = new Fl_Box(10, 230, 90, 24, "Status bar:");
        scl->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        status_count_choice_ = new Fl_Choice(108, 230, 190, 24);
        status_count_choice_->add("Character count");
        status_count_choice_->add("Byte count");
        status_count_choice_->add("Readable size");
        status_count_choice_->value(current.status_count == AppSettings::STATUS_COUNT_BYTES ? 1
                                    : current.status_count == AppSettings::STATUS_COUNT_SIZE ? 2 : 0);

        scrollbar_btn_ = new Fl_Check_Button(108, 266, 190, 24, "Vertical scrollbar");
        scrollbar_btn_->value(current.vertical_scrollbar ? 1 : 0);

        // Buttons
        Fl_Return_Button *ok  = new Fl_Return_Button(118, 327, 80, 28, "OK");
        Fl_Button        *can = new Fl_Button(208, 327, 80, 28, "Cancel");
        ok->callback(ok_cb,     this);
        can->callback(cancel_cb, this);

        update_indent_width_state();

        end();
    }

    // Returns true and fills `out` if the user clicked OK.
    bool run(AppSettings &out)
    {
        show();
        while (shown()) Fl::wait();
        if (accepted_) { out = result_; return true; }
        return false;
    }
};

// ---------------------------------------------------------------------------
// Text editor widget with drag-and-drop file support
// ---------------------------------------------------------------------------

class DndTextEditor : public Fl_Text_Editor {
    std::function<void(const std::string &)> on_drop_;
    bool dnd_active_ = false;
    bool text_drag_pending_ = false;
    bool is_text_drag_ = false;
    int text_drag_sel_start_ = 0;
    int text_drag_sel_end_ = 0;
    int text_drag_drop_pos_ = 0;
    AppSettings::IndentMode indent_mode_ = AppSettings::INDENT_TABS;
    int indent_width_ = 4;
    std::function<void()> on_cursor_change_;
    std::function<void()> on_insert_mode_change_;
    std::function<bool(int)> on_ctrl_click_;
    std::function<bool(int)> on_link_at_;
    bool link_cursor_active_ = false;
    int last_cursor_pos_ = 0;

    std::string indent_text() const
    {
        if (indent_mode_ == AppSettings::INDENT_SPACES) {
            return std::string(indent_width_, ' ');
        }
        return "\t";
    }

    void notify_cursor_change()
    {
        if (!on_cursor_change_) return;
        const int pos = insert_position();
        if (pos != last_cursor_pos_) {
            last_cursor_pos_ = pos;
            on_cursor_change_();
        }
    }

    static bool multiline_selection(Fl_Text_Buffer *buf, int &start, int &end)
    {
        start = 0;
        end = 0;
        if (buf == nullptr || !buf->selected()) return false;
        buf->selection_position(&start, &end);
        return start != end && buf->count_lines(start, end) > 0;
    }

    static int outdent_columns(Fl_Text_Buffer *buf, int line_start, int indent_width)
    {
        if (buf == nullptr || line_start < 0 || line_start >= buf->length()) return 0;
        if (buf->byte_at(line_start) == '\t') return 1;

        int count = 0;
        while (count < indent_width) {
            int pos = line_start + count;
            if (pos >= buf->length() || buf->byte_at(pos) != ' ') break;
            ++count;
        }
        return count;
    }

    void indent_selected_lines(int sel_start, int sel_end)
    {
        Fl_Text_Buffer *buf = buffer();
        if (buf == nullptr) return;

        const std::string indent = indent_text();
        const int indent_len = static_cast<int>(indent.size());
        if (indent_len == 0) return;

        const int first_line = buf->line_start(sel_start);
        int last_pos = sel_end;
        if (sel_end > sel_start && sel_end == buf->line_start(sel_end)) {
            last_pos = sel_end - 1;
        }
        if (last_pos < first_line) last_pos = first_line;

        int line = buf->line_start(last_pos);
        while (true) {
            buf->insert(line, indent.c_str(), indent_len);
            if (line == first_line) break;
            int prev = buf->rewind_lines(line, 1);
            if (prev >= line) break;
            line = prev;
        }
    }

    void outdent_selected_lines(int sel_start, int sel_end)
    {
        Fl_Text_Buffer *buf = buffer();
        if (buf == nullptr) return;

        const int first_line = buf->line_start(sel_start);
        int last_pos = sel_end;
        if (sel_end > sel_start && sel_end == buf->line_start(sel_end)) {
            last_pos = sel_end - 1;
        }
        if (last_pos < first_line) last_pos = first_line;

        int line = buf->line_start(last_pos);
        while (true) {
            int remove_len = outdent_columns(buf, line, indent_width_);
            if (remove_len > 0) {
                buf->remove(line, line + remove_len);
            }
            if (line == first_line) break;
            int prev = buf->rewind_lines(line, 1);
            if (prev >= line) break;
            line = prev;
        }
    }

    int handle_indent_key()
    {
        Fl_Text_Buffer *buf = buffer();
        if (buf == nullptr) return 1;

        int sel_start = 0;
        int sel_end = 0;
        if (multiline_selection(buf, sel_start, sel_end)) {
            indent_selected_lines(sel_start, sel_end);
        } else if (buf->selected() && buf->selection_position(&sel_start, &sel_end)) {
            const std::string indent = indent_text();
            buf->replace(sel_start, sel_end, indent.c_str(), static_cast<int>(indent.size()));
            buf->unselect();
            insert_position(sel_start + static_cast<int>(indent.size()));
        } else {
            const std::string indent = indent_text();
            const int cursor = insert_position();
            buf->insert(cursor, indent.c_str(), static_cast<int>(indent.size()));
            insert_position(cursor + static_cast<int>(indent.size()));
        }

        show_insert_position();
        return 1;
    }

    int handle_outdent_key()
    {
        Fl_Text_Buffer *buf = buffer();
        if (buf == nullptr) return 1;

        int sel_start = 0;
        int sel_end = 0;
        if (multiline_selection(buf, sel_start, sel_end)) {
            outdent_selected_lines(sel_start, sel_end);
            show_insert_position();
            return 1;
        }

        int cursor = insert_position();
        if (buf->selected() && buf->selection_position(&sel_start, &sel_end)) {
            cursor = sel_start;
            buf->unselect();
        }

        const int line_start = buf->line_start(cursor);
        const int remove_len = outdent_columns(buf, line_start, indent_width_);
        if (remove_len > 0) {
            buf->remove(line_start, line_start + remove_len);
            if (cursor > line_start) {
                insert_position(std::max(line_start, cursor - remove_len));
            }
        }

        show_insert_position();
        return 1;
    }
public:
    DndTextEditor(int x, int y, int w, int h,
                  std::function<void(const std::string &)> on_drop)
        : Fl_Text_Editor(x, y, w, h), on_drop_(std::move(on_drop))
    {
        tab_nav(0);
        // Route every default undo shortcut through the transaction boundaries.
        for (auto *binding = key_bindings; binding; binding = binding->next)
            if (binding->function == Fl_Text_Editor::kf_undo) binding->function = EditorTextBuffer::undo_key;
        add_key_binding('z', FL_CTRL | FL_SHIFT, EditorTextBuffer::redo_key);
        add_key_binding('y', FL_CTRL, EditorTextBuffer::redo_key);
#ifdef __APPLE__
        add_key_binding('z', FL_META | FL_SHIFT, EditorTextBuffer::redo_key);
#endif
    }

    void resize(int x, int y, int w, int h) override
    {
        if (scrollbar_align() & (FL_ALIGN_LEFT | FL_ALIGN_RIGHT)) {
            Fl_Text_Editor::resize(x, y, w, h);
            return;
        }
        // FLTK's wrap-at-bounds optimization can force the vertical bar on
        // despite scrollbar_align(). Lay out this widget without a gutter,
        // then restore the global size before any other widget is laid out.
        const int global_size = Fl::scrollbar_size();
        Fl::scrollbar_size(0);
        Fl_Text_Editor::resize(x, y, w, h);
        mVScrollBar->hide();
        Fl::scrollbar_size(global_size);
    }

    void set_indent_behavior(AppSettings::IndentMode indent_mode, int indent_width)
    {
        indent_mode_ = indent_mode;
        indent_width_ = clamp_indent_width(indent_width);
        if (buffer() != nullptr) {
            buffer()->tab_distance(indent_width_);
        }
    }

    void set_cursor_change_callback(std::function<void()> cb)
    {
        on_cursor_change_ = std::move(cb);
    }

    void set_insert_mode_change_callback(std::function<void()> cb)
    {
        on_insert_mode_change_ = std::move(cb);
    }

    void set_ctrl_click_callback(std::function<bool(int)> cb)
    {
        on_ctrl_click_ = std::move(cb);
    }

    void set_link_at_callback(std::function<bool(int)> cb)
    {
        on_link_at_ = std::move(cb);
    }

    int handle(int event) override
    {
        if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE &&
            Fl::event_ctrl() && on_ctrl_click_)
        {
            const int pos = xy_to_position(Fl::event_x(), Fl::event_y(),
                                           Fl_Text_Display::CHARACTER_POS);
            if (on_ctrl_click_(pos)) return 1;
        }
        // Track whether a push inside a selection might become a text-drag
        if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE && !Fl::event_ctrl()) {
            Fl_Text_Buffer *buf = buffer();
            text_drag_pending_ = false;
            if (buf && buf->selected()) {
                int sel_start = 0, sel_end = 0;
                buf->selection_position(&sel_start, &sel_end);
                const int click_pos = xy_to_position(Fl::event_x(), Fl::event_y(),
                                                     Fl_Text_Display::CHARACTER_POS);
                if (click_pos >= sel_start && click_pos < sel_end) {
                    text_drag_pending_ = true;
                    text_drag_sel_start_ = sel_start;
                    text_drag_sel_end_   = sel_end;
                }
            }
        }
        switch (event) {
            case FL_KEYDOWN:
                if (Fl::event_key() == FL_Insert) {
                    int r = Fl_Text_Editor::handle(event);
                    if (on_insert_mode_change_) on_insert_mode_change_();
                    notify_cursor_change();
                    return r;
                }
                if (Fl::event_key() == FL_Tab) {
                    int r = Fl::event_state(FL_SHIFT) ? handle_outdent_key()
                                                       : handle_indent_key();
                    notify_cursor_change();
                    return r;
                }
                break;
            case FL_DND_ENTER:
                dnd_active_ = true;
                if (text_drag_pending_) {
                    is_text_drag_ = true;
                    text_drag_pending_ = false;
                    text_drag_drop_pos_ = xy_to_position(Fl::event_x(), Fl::event_y(),
                                                         Fl_Text_Display::CHARACTER_POS);
                    if (window()) window()->cursor(FL_CURSOR_MOVE);
                }
                return 1;
            case FL_DND_DRAG:
                dnd_active_ = true;
                if (text_drag_pending_) {
                    is_text_drag_ = true;
                    text_drag_pending_ = false;
                }
                if (is_text_drag_) {
                    text_drag_drop_pos_ = xy_to_position(Fl::event_x(), Fl::event_y(),
                                                         Fl_Text_Display::CHARACTER_POS);
                    if (window()) window()->cursor(FL_CURSOR_MOVE);
                }
                return 1;
            case FL_DND_LEAVE:
                dnd_active_ = false;
                if (is_text_drag_) {
                    is_text_drag_ = false;
                    text_drag_pending_ = false;
                    if (window()) window()->cursor(FL_CURSOR_INSERT);
                }
                return 1;
            case FL_DND_RELEASE:
                if (is_text_drag_) {
                    text_drag_drop_pos_ = xy_to_position(Fl::event_x(), Fl::event_y(),
                                                         Fl_Text_Display::CHARACTER_POS);
                }
                return 1;
            case FL_PASTE:
                if (dnd_active_) {
                    dnd_active_ = false;
                    if (is_text_drag_) {
                        is_text_drag_ = false;
                        Fl_Text_Buffer *buf = buffer();
                        if (buf) {
                            const std::string moved = Fl::event_text() ? Fl::event_text() : "";
                            if (!moved.empty()) {
                                int drop_pos = text_drag_drop_pos_;
                                const int sel_start = text_drag_sel_start_;
                                const int sel_end   = text_drag_sel_end_;
                                // Verify original selection is still intact before removing
                                char *cur_sel = buf->text_range(sel_start, sel_end);
                                const bool sel_intact = cur_sel && moved == cur_sel;
                                free(cur_sel);
                                if (sel_intact) {
                                    buf->remove(sel_start, sel_end);
                                    if (drop_pos >= sel_end)
                                        drop_pos -= (sel_end - sel_start);
                                    else if (drop_pos > sel_start)
                                        drop_pos = sel_start;
                                }
                                buf->insert(drop_pos, moved.c_str());
                                buf->select(drop_pos, drop_pos + static_cast<int>(moved.size()));
                                insert_position(drop_pos + static_cast<int>(moved.size()));
                            }
                        }
                        if (window()) window()->cursor(FL_CURSOR_INSERT);
                        return 1;
                    }
                    std::string raw = Fl::event_text();
                    // Take first URI only
                    std::size_t nl = raw.find('\n');
                    if (nl != std::string::npos) raw = raw.substr(0, nl);
                    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
                    if (raw.rfind("file://", 0) == 0) raw = raw.substr(7);
                    raw = url_decode(raw);
#ifdef _WIN32
                    // Strip leading slash before Windows drive letter: /C:/... → C:/...
                    if (raw.size() >= 3 && raw[0] == '/' &&
                        std::isalpha(static_cast<unsigned char>(raw[1])) && raw[2] == ':') {
                        raw = raw.substr(1);
                    }
#endif
                    if (!raw.empty() && on_drop_) on_drop_(raw);
                    return 1;
                }
                break;
        }
        int r = Fl_Text_Editor::handle(event);
        // Apply hand cursor AFTER base class so we override Fl_Text_Display's
        // FL_CURSOR_INSERT (set on FL_MOVE) and FL_CURSOR_NONE (set on key press).
        // Only act when the pointer is inside the text area (not over the
        // scrollbar) so the scrollbar shows its default arrow cursor.
        if (on_link_at_ && window()) {
            const bool in_text_area = Fl::event_inside(text_area.x, text_area.y,
                                                       text_area.w, text_area.h);
            const bool is_ctrl_key = (event == FL_KEYDOWN || event == FL_KEYUP) &&
                (Fl::event_key() == FL_Control_L || Fl::event_key() == FL_Control_R);
            switch (event) {
                case FL_MOVE:
                case FL_ENTER:
                    if (in_text_area) {
                        const bool want_hand = Fl::event_ctrl() &&
                            on_link_at_(xy_to_position(Fl::event_x(), Fl::event_y(),
                                                       Fl_Text_Display::CHARACTER_POS));
                        link_cursor_active_ = want_hand;
                        window()->cursor(want_hand ? FL_CURSOR_HAND : FL_CURSOR_INSERT);
                    } else if (link_cursor_active_) {
                        link_cursor_active_ = false;
                        window()->cursor(FL_CURSOR_DEFAULT);
                    }
                    break;
                case FL_KEYDOWN:
                    if (is_ctrl_key && in_text_area) {
                        const bool want_hand =
                            on_link_at_(xy_to_position(Fl::event_x(), Fl::event_y(),
                                                       Fl_Text_Display::CHARACTER_POS));
                        link_cursor_active_ = want_hand;
                        window()->cursor(want_hand ? FL_CURSOR_HAND : FL_CURSOR_INSERT);
                    }
                    break;
                case FL_KEYUP:
                    if (is_ctrl_key && link_cursor_active_) {
                        link_cursor_active_ = false;
                        window()->cursor(in_text_area ? FL_CURSOR_INSERT
                                                       : FL_CURSOR_DEFAULT);
                    }
                    break;
                case FL_LEAVE:
                    link_cursor_active_ = false;
                    break;
                default:
                    break;
            }
        }
        notify_cursor_change();
        return r;
    }
};

// ---------------------------------------------------------------------------
// In-memory password storage with XOR-pad obfuscation.
//
// Stores (random_pad XOR plaintext) so the password does not appear verbatim
// in a casual process memory dump or a `strings` scan.
//
// THREAT MODEL LIMITS (defense in depth, not a security boundary):
//   * Both `data_` and `pad_` live in the same process address space, so any
//     attacker with arbitrary read (Spectre-class side channels, /proc/<pid>/
//     mem access from the same UID, kernel memory disclosure, or core dumps
//     containing both vectors) can XOR them back together trivially.
//   * Anyone who can ptrace or debug the process can read the password while
//     it is being entered, while the GUI dialog holds it in an Fl_Input, or
//     while it is unwrapped via `get()` and passed to the codec.
//   * The pad is freshly generated per `set()` from the platform CSPRNG.
// The obfuscation defeats only opportunistic inspection of the same-process
// heap (e.g. someone running `strings` on a core file).
// ---------------------------------------------------------------------------
struct SecureString {
    SecureString() = default;
    SecureString(const SecureString &) = delete;
    SecureString &operator=(const SecureString &) = delete;
    ~SecureString() { clear(); }

    bool empty() const { return data_.empty(); }

    void set(const std::string &s) {
        clear();
        if (s.empty()) return;
        const std::size_t n = s.size();
        pad_.resize(n);
        if (txx_random_bytes(pad_.data(), n) != TXX_OK) {
            clear();
            return;
        }
        data_.resize(n);
        for (std::size_t i = 0; i < n; ++i)
            data_[i] = static_cast<uint8_t>(s[i]) ^ pad_[i];
    }

    std::string get() const {
        std::string s(data_.size(), '\0');
        for (std::size_t i = 0; i < data_.size(); ++i)
            s[i] = static_cast<char>(data_[i] ^ pad_[i]);
        return s;
    }

    void clear() {
        if (!data_.empty()) { txx_secure_zero(data_.data(), data_.size()); data_.clear(); }
        if (!pad_.empty())  { txx_secure_zero(pad_.data(),  pad_.size());  pad_.clear();  }
    }

private:
    std::vector<uint8_t> data_;
    std::vector<uint8_t> pad_;
};

// ---------------------------------------------------------------------------
// Menu bar that refreshes enable/disable state of its items on demand.
// ---------------------------------------------------------------------------

class EditorMenuBar : public Fl_Menu_Bar {
public:
    std::function<void()> on_refresh;

    EditorMenuBar(int X, int Y, int W, int H, const char *L = nullptr)
        : Fl_Menu_Bar(X, Y, W, H, L) {}

    int handle(int e) override {
        if ((e == FL_PUSH || e == FL_SHORTCUT) && on_refresh) {
            on_refresh();
        }
        return Fl_Menu_Bar::handle(e);
    }
};

// ---------------------------------------------------------------------------
// Main editor window
// ---------------------------------------------------------------------------

class EncryptEdWindow : public Fl_Double_Window {
    EditorMenuBar    *menu_;
    DndTextEditor    *editor_;
    EditorTextBuffer *buf_;
    Fl_Box           *status_;
    Fl_Box           *insert_mode_box_;
    Fl_Box           *cursor_pos_;
    FindReplaceWindow *fr_win_;  // allocated after end(); deleted in dtor

    std::string filepath_;
    SecureString password_;
    bool dirty_  = false;
    bool is_txx_ = false;
    DocumentTextState text_state_;

    AppSettings      settings_;
    std::string      pending_font_name_{"Courier"};
    Fl_Preferences   prefs_{ Fl_Preferences::USER_L, "encryptEd", "encryptEd" };
    Highlighter     *hl_ = nullptr;
    bool             highlight_off_for_size_ = false;

    static constexpr int MENU_H    = 25;
    static constexpr int STAT_H    = 20;
    static constexpr int CURSOR_W  = 160;
    static constexpr int INS_W     = 36;

    bool plugin_running_ = false;
    plugins::Catalog plugin_catalog_;
    std::filesystem::path plugin_root_ = plugins::user_directory();
    struct PluginBinding { EncryptEdWindow *window; std::size_t index; };
    std::vector<PluginBinding> plugin_bindings_;
    std::deque<std::string> plugin_labels_;
    std::vector<Fl_Menu_Item> plugin_menu_;

    void append_plugin_item(const std::string &label, Fl_Callback *callback,
                            void *data, int flags = 0)
    {
        // Build raw menu items to preserve slashes and other menu-path characters.
        std::string literal;
        for (char ch : label) { literal += ch; if (ch == '&') literal += '&'; }
        plugin_labels_.push_back(std::move(literal));
        Fl_Menu_Item item{};
        item.text = plugin_labels_.back().c_str();
        item.callback_ = callback;
        item.user_data_ = data;
        item.flags = flags;
        item.labelsize_ = FL_NORMAL_SIZE;
        plugin_menu_.push_back(item);
    }

    void reload_plugins(bool report_errors)
    {
        if (plugin_running_) return;
        menu_->value(-1);
        plugin_catalog_ = plugins::discover(plugin_root_);
        plugin_menu_.clear(); plugin_labels_.clear(); plugin_bindings_.clear();
        plugin_bindings_.reserve(plugin_catalog_.commands.size());
        append_plugin_item("Install Default Plugins", [](Fl_Widget *, void *v) {
            static_cast<EncryptEdWindow *>(v)->install_default_plugins();
        }, this);
        append_plugin_item("Reload Plugins", [](Fl_Widget *, void *v) {
            static_cast<EncryptEdWindow *>(v)->reload_plugins(true);
        }, this);
        append_plugin_item("Open Plugins Folder", [](Fl_Widget *, void *v) {
            static_cast<EncryptEdWindow *>(v)->open_plugins_folder();
        }, this, FL_MENU_DIVIDER);
        struct Node {
            std::map<std::string, Node> folders;
            std::vector<std::size_t> commands;
        } root;
        for (std::size_t i = 0; i < plugin_catalog_.commands.size(); ++i) {
            Node *node = &root;
            for (const auto &folder : plugin_catalog_.commands[i].folders) node = &node->folders[folder];
            node->commands.push_back(i);
            plugin_bindings_.push_back({this, i});
        }
        std::function<void(const Node &)> append = [&](const Node &node) {
            for (const auto &folder : node.folders) {
                append_plugin_item(folder.first, nullptr, nullptr, FL_SUBMENU);
                append(folder.second);
                plugin_menu_.push_back({});
            }
            for (auto i : node.commands) {
                append_plugin_item(plugin_catalog_.commands[i].label, [](Fl_Widget *, void *v) {
                    auto &binding = *static_cast<PluginBinding *>(v);
                    binding.window->run_plugin(binding.index);
                }, &plugin_bindings_[i]);
            }
        };
        append(root);
        if (plugin_catalog_.commands.empty()) append_plugin_item("No plugins installed", nullptr, nullptr, FL_MENU_INACTIVE);
        if (!plugin_catalog_.errors.empty()) append_plugin_item("Show discovery errors...", [](Fl_Widget *, void *v) {
            static_cast<EncryptEdWindow *>(v)->show_plugin_errors();
        }, this);
        plugin_menu_.push_back({});
        auto *item = const_cast<Fl_Menu_Item *>(menu_->find_item("Plugins"));
        if (item) item->user_data(plugin_menu_.data());
        else menu_->add("Plugins", 0, nullptr, plugin_menu_.data(), FL_SUBMENU_POINTER);
        menu_->redraw();
        if (report_errors) show_plugin_errors();
    }

    void show_plugin_errors()
    {
        if (plugin_catalog_.errors.empty()) return;
        std::string errors;
        for (const auto &error : plugin_catalog_.errors) {
            if (errors.size() > 3000) { errors += "\nAdditional errors omitted."; break; }
            if (!errors.empty()) errors += '\n';
            errors += error;
        }
        fl_alert("%s", errors.c_str());
    }

    void install_default_plugins()
    {
        if (plugin_running_) return;
        auto source = plugins::default_directory(plugins::executable_path());
        auto result = plugins::install_defaults(source, plugin_root_);
        reload_plugins(false);
        std::string summary = "Installed " + std::to_string(result.installed) +
            " plugins; kept " + std::to_string(result.skipped.size()) + " existing files.";
        for (const auto &path : result.skipped) summary += "\nKept: " + path;
        for (const auto &error : result.errors) summary += "\n" + error;
        fl_message("%s", summary.c_str());
    }

    void open_plugins_folder()
    {
        if (plugin_running_) return;
        if (plugin_root_.empty()) { fl_alert("Could not determine the plugin directory."); return; }
        std::error_code ec;
        std::filesystem::create_directories(plugin_root_, ec);
        if (ec) { fl_alert("Could not create plugin directory: %s", ec.message().c_str()); return; }
        auto path = std::filesystem::absolute(plugin_root_, ec).generic_u8string();
        if (ec) { fl_alert("Could not resolve plugin directory."); return; }
        std::string uri = "file://";
#ifdef _WIN32
        uri += '/';
#endif
        const char *hex = "0123456789ABCDEF";
        for (unsigned char ch : path) {
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '/' || ch == ':' || ch == '-' || ch == '_' || ch == '.') uri += ch;
            else { uri += '%'; uri += hex[ch >> 4]; uri += hex[ch & 15]; }
        }
        char error[512]{};
        if (!fl_open_uri(uri.c_str(), error, sizeof(error))) fl_alert("Could not open plugin folder: %s", error);
    }

    void run_plugin(std::size_t index)
    {
        if (plugin_running_ || index >= plugin_catalog_.commands.size()) return;
        const auto &command = plugin_catalog_.commands[index];
        std::string source, error;
        if (!plugins::read_script(plugin_root_, command, source, error)) {
            fl_alert("Plugin %s: %s", command.label.c_str(), error.c_str()); return;
        }
        plugins::Document document;
        document.text = buffer_text();
        document.cursor = static_cast<std::size_t>(editor_->insert_position());
        int start = 0, end = 0;
        if (buf_->selection_position(&start, &end)) {
            document.selection_start = static_cast<std::size_t>(start);
            document.selection_end = static_cast<std::size_t>(end);
        }
        document.indent_width = settings_.indent_width;
        document.indent_spaces = settings_.indent_mode == AppSettings::INDENT_SPACES;
        PluginDialogs dialogs("Plugin: " + command.label);
        plugin_running_ = true;
        fr_win_->hide();
        deactivate();
        auto result = plugins::run(source, "@" + command.path.filename().u8string(), document, dialogs);
        activate();
        plugin_running_ = false;
        if (result.status == plugins::Status::Success) {
            apply_plugin_text(*buf_, result.document);
            editor_->insert_position(static_cast<int>(result.document.cursor));
            if (result.document.selection_start != result.document.selection_end)
                buf_->select(static_cast<int>(result.document.selection_start), static_cast<int>(result.document.selection_end));
            else buf_->unselect();
            editor_->show_insert_position();
            update_cursor_pos();
        } else if (result.status == plugins::Status::Error) {
            fl_alert("Plugin %s failed:\n%s", command.label.c_str(), result.error.c_str());
        }
        editor_->take_focus();
        refresh_menu_state();
    }

    // ---- settings ----

    void load_settings()
    {
        char font_name[128] = "Courier";
        prefs_.get("font_name", font_name, font_name, static_cast<int>(sizeof font_name));
        pending_font_name_ = font_name;
        settings_.font = font_by_name(font_name);  // FL_COURIER until init_fonts_and_apply() runs

        int sz = 14;
        prefs_.get("font_size", sz, sz);
        if (sz < 8 || sz > 36) sz = 14;
        settings_.size = static_cast<Fl_Fontsize>(sz);

        int dark = 0;
        prefs_.get("dark_mode", dark, dark);
        settings_.dark_mode = (dark != 0);

        int indent_mode = static_cast<int>(AppSettings::INDENT_TABS);
        prefs_.get("indent_mode", indent_mode, indent_mode);
        settings_.indent_mode = indent_mode == static_cast<int>(AppSettings::INDENT_SPACES)
                              ? AppSettings::INDENT_SPACES
                              : AppSettings::INDENT_TABS;

        int indent_width = 4;
        prefs_.get("indent_width", indent_width, indent_width);
        settings_.indent_width = clamp_indent_width(indent_width);

        int status_count = static_cast<int>(AppSettings::STATUS_COUNT_CHARS);
        prefs_.get("status_count", status_count, status_count);
        settings_.status_count = status_count >= 0 && status_count <= 2
                               ? static_cast<AppSettings::StatusCount>(status_count)
                               : AppSettings::STATUS_COUNT_CHARS;

        int hl_mode = -1;
        prefs_.get("highlight_mode", hl_mode, hl_mode);
        if (hl_mode < 0 || hl_mode > 3) {
            // Migrate the pre-1.3 boolean preference.
            int md_hl = 1;
            prefs_.get("markdown_highlight", md_hl, md_hl);
            hl_mode = md_hl ? 1 : 0;
        }
        settings_.highlight_mode = hl_mode;
        int hl_lang = static_cast<int>(SynLang::None);
        prefs_.get("highlight_lang", hl_lang, hl_lang);
        settings_.highlight_lang = static_cast<SynLang>(hl_lang);
        int scrollbar = 1;
        prefs_.get("vertical_scrollbar", scrollbar, 1);
        settings_.vertical_scrollbar = scrollbar != 0;
    }

    void save_settings()
    {
        const char *fn = Fl::get_font_name(settings_.font);
        prefs_.set("font_name", fn ? fn : "Courier");
        prefs_.set("font_size",  static_cast<int>(settings_.size));
        prefs_.set("dark_mode",  settings_.dark_mode ? 1 : 0);
        prefs_.set("indent_mode", static_cast<int>(settings_.indent_mode));
        prefs_.set("indent_width", settings_.indent_width);
        prefs_.set("status_count", static_cast<int>(settings_.status_count));
        prefs_.set("highlight_mode", settings_.highlight_mode);
        prefs_.set("highlight_lang", static_cast<int>(settings_.highlight_lang));
        // Keep the legacy key readable by older builds.
        prefs_.set("markdown_highlight", settings_.highlight_mode != 0 ? 1 : 0);
        prefs_.set("vertical_scrollbar", settings_.vertical_scrollbar ? 1 : 0);
    }

    void apply_settings()
    {
        editor_->scrollbar_align(settings_.vertical_scrollbar ? FL_ALIGN_BOTTOM_RIGHT : FL_ALIGN_BOTTOM);
        editor_->resize(editor_->x(), editor_->y(), editor_->w(), editor_->h());
        editor_->textfont(settings_.font);
        editor_->textsize(settings_.size);
        editor_->set_indent_behavior(settings_.indent_mode, settings_.indent_width);

        if (settings_.dark_mode) {
            Fl::background(45, 45, 45);
            Fl::background2(30, 30, 30);
            Fl::foreground(220, 220, 220);
            editor_->color(fl_rgb_color(30, 30, 30));
            editor_->textcolor(fl_rgb_color(220, 220, 220));
            editor_->selection_color(fl_rgb_color(70, 100, 140));
            editor_->cursor_color(fl_rgb_color(220, 220, 220));
        } else {
            Fl::background(240, 240, 240);
            Fl::background2(255, 255, 255);
            Fl::foreground(0, 0, 0);
            editor_->color(FL_WHITE);
            editor_->textcolor(FL_BLACK);
            editor_->selection_color(FL_SELECTION_COLOR);
            editor_->cursor_color(FL_BLACK);
        }

        Fl::reload_scheme();
        redraw();
        update_status();

        apply_highlight_settings();
    }

    // Resolve the highlighting preference against the current file name.
    // Auto detection is off for untitled buffers until the first save.
    void resolve_highlight_mode(HighlightMode &mode, SynLang &lang) const
    {
        lang = SynLang::None;
        switch (settings_.highlight_mode) {
        case 0:
            mode = HighlightMode::Off;
            return;
        case 2:
            mode = HighlightMode::Markdown;
            return;
        case 3:
            lang = settings_.highlight_lang;
            if (lang == SynLang::Markdown) mode = HighlightMode::Markdown;
            else if (lang == SynLang::None) mode = HighlightMode::Off;
            else mode = HighlightMode::Code;
            return;
        default:
            if (filepath_.empty()) {
                mode = HighlightMode::Off;
                return;
            }
            lang = syn_detect(filepath_);
            if (lang == SynLang::Markdown) mode = HighlightMode::Markdown;
            else if (lang == SynLang::None) mode = HighlightMode::Off;
            else mode = HighlightMode::Code;
            return;
        }
    }

    void apply_highlight_settings()
    {
        HighlightMode mode = HighlightMode::Off;
        SynLang lang = SynLang::None;
        resolve_highlight_mode(mode, lang);

        highlight_off_for_size_ =
            mode == HighlightMode::Code && buf_->length() > kMaxHighlightBytes;
        if (highlight_off_for_size_) mode = HighlightMode::Off;

        if (mode == HighlightMode::Off) {
            delete hl_;
            hl_ = nullptr;
            editor_->set_ctrl_click_callback(nullptr);
            editor_->set_link_at_callback(nullptr);
            return;
        }

        if (!hl_) {
            hl_ = new Highlighter();
            hl_->attach(editor_, buf_);
        }
        hl_->set_mode(mode, lang);
        hl_->rebuild_style_table(settings_.font, settings_.size,
                                 settings_.dark_mode);
        hl_->rehighlight_all();

        editor_->set_ctrl_click_callback([this](int pos) -> bool {
            std::string url = hl_ ? hl_->url_at(pos) : std::string();
            if (url.empty()) return false;
            if (!is_safe_external_uri(url)) {
                fl_beep();
                return false;
            }
            fl_open_uri(url.c_str());
            return true;
        });
        editor_->set_link_at_callback([this](int pos) -> bool {
            return hl_ && !hl_->url_at(pos).empty();
        });
    }

    void do_preferences()
    {
        PrefsDialog dlg(settings_);
        AppSettings next;
        if (dlg.run(next)) {
            settings_ = next;
            apply_settings();
            save_settings();
        }
    }

    // ---- title / status ----

    void set_dirty(bool d)
    {
        dirty_ = d;
        update_title();
        update_status();
    }

    void update_title()
    {
        std::string t;
        if (filepath_.empty()) {
            t = "Untitled";
        } else {
            std::size_t p = filepath_.find_last_of("/\\");
            t = (p == std::string::npos) ? filepath_ : filepath_.substr(p + 1);
        }
        if (dirty_) t += " \xe2\x80\xa2";      // UTF-8 middle dot
        t += " \xe2\x80\x94 EncryptEd";         // em-dash
        copy_label(t.c_str());
    }

    void update_status()
    {
        std::string s = format_status_counter();
        if (highlight_off_for_size_) s += "  [highlighting off: large file]";
        if (is_txx_) s += "  [encrypted]";
        if (dirty_)  s += "  [unsaved changes]";
        status_->copy_label(s.c_str());
        status_->redraw();
    }

    int count_buffer_codepoints() const
    {
        if (buf_ == nullptr) return 0;
        int len = buf_->length();
        int n = 0;
        for (int i = 0; i < len; ++i) {
            if ((buf_->byte_at(i) & 0xC0) != 0x80) ++n;
        }
        return n;
    }

    std::string format_status_counter() const
    {
        char out[64];
        if (settings_.status_count == AppSettings::STATUS_COUNT_SIZE) {
            return human_readable_size(buf_ ? static_cast<std::size_t>(buf_->length()) : 0);
        } else if (settings_.status_count == AppSettings::STATUS_COUNT_BYTES) {
            return std::to_string(buf_ ? buf_->length() : 0) + " bytes";
        } else {
            int chars = count_buffer_codepoints();
            if (chars >= 10000) {
                double v = static_cast<double>(chars) / 1000.0;
                std::snprintf(out, sizeof(out), "%.2f thousand characters", v);
            } else {
                std::snprintf(out, sizeof(out), "%d characters", chars);
            }
        }
        return std::string(out);
    }

    // ---- dynamic menu state ----

    static void set_item_enabled(const Fl_Menu_Item *item, bool enabled)
    {
        if (item == nullptr) return;
        Fl_Menu_Item *m = const_cast<Fl_Menu_Item *>(item);
        if (enabled) m->activate();
        else         m->deactivate();
    }

    void refresh_menu_state()
    {
        if (menu_ == nullptr || plugin_running_) return;

        const bool has_file = !filepath_.empty();
        set_item_enabled(menu_->find_item("File/Open Containing Folder"), has_file);
        set_item_enabled(menu_->find_item("File/Save"),                   dirty_ || filepath_.empty());

        const bool has_sel = buf_ != nullptr && buf_->selected();
        set_item_enabled(menu_->find_item("Edit/Cut"),  has_sel);
        set_item_enabled(menu_->find_item("Edit/Copy"), has_sel);

        const bool clip_has_text = Fl::clipboard_contains(Fl::clipboard_plain_text) != 0;
        set_item_enabled(menu_->find_item("Edit/Paste"), clip_has_text);

        const bool has_text = buf_ != nullptr && buf_->length() > 0;
        set_item_enabled(menu_->find_item("Edit/Select All"), has_text);
        set_item_enabled(menu_->find_item("Edit/Find"),        has_text);
        set_item_enabled(menu_->find_item("Edit/Go to Line..."), has_text);
    }

    void update_insert_mode()
    {
        // insert_mode() returns 1 for insert, 0 for overwrite.
        if (editor_->insert_mode()) {
            insert_mode_box_->label("INS");
        } else {
            insert_mode_box_->label("OVR");
        }
        insert_mode_box_->redraw();
    }

    void update_cursor_pos()
    {
        const int pos   = editor_->insert_position();
        const int row   = buf_->count_lines(0, pos) + 1;
        const int total = buf_->count_lines(0, buf_->length()) + 1;
        const int ls    = buf_->line_start(pos);

        int col = 1;
        if (pos > ls) {
            char *seg = buf_->text_range(ls, pos);
            if (seg) {
                const char *p   = seg;
                const char *end = seg + (pos - ls);
                while (p < end) {
                    int clen = 0;
                    fl_utf8decode(p, end, &clen);
                    if (clen <= 0) clen = 1;
                    p += clen;
                    ++col;
                }
                free(seg);
            }
        }

        char lbl[64];
        std::snprintf(lbl, sizeof(lbl), "  C:%d  R:%d/%d", col, row, total);
        cursor_pos_->copy_label(lbl);
        cursor_pos_->redraw();
    }

    // ---- unsaved-changes guard ----

    bool check_unsaved()
    {
        if (!dirty_) return true;
        return fl_choice("This document has unsaved changes.\nDiscard changes?",
                         "Cancel", "Discard", nullptr) == 1;
    }

    // ---- file I/O ----

    static txx_status read_bytes(const char *path, txx_buffer *buf)
    {
        return txx_read_file_limited(path, TXX_MAX_FILE_BYTES, buf);
    }

    static txx_status write_bytes(const char *path, const uint8_t *data, std::size_t len)
    {
        return txx_write_file_atomic(path, data, len);
    }

    static std::string display_name_for_path(const std::string &path)
    {
        std::size_t p = path.find_last_of("/\\");
        return (p == std::string::npos) ? path : path.substr(p + 1);
    }

    static const char *line_endings_menu_path(txx_newline_style style, bool mixed)
    {
        if (mixed || style == TXX_NEWLINE_MIXED) {
            return "File/Line Endings/Mixed (Preserve)";
        }

        switch (style) {
        case TXX_NEWLINE_CRLF:
            return "File/Line Endings/CRLF (Windows)";
        case TXX_NEWLINE_CR:
            return "File/Line Endings/CR (Classic Mac)";
        case TXX_NEWLINE_LF:
            return "File/Line Endings/LF (Unix, macOS)";
        default:
            return line_endings_menu_path(kDefaultPlatformNewlineStyle, false);
        }
    }

    void sync_line_endings_menu()
    {
        if (menu_ == nullptr) return;

        const Fl_Menu_Item *item = menu_->find_item(
            line_endings_menu_path(text_state_.newline_style, text_state_.mixed_newlines));
        if (item != nullptr) {
            menu_->setonly(const_cast<Fl_Menu_Item *>(item));
        }
    }

    void set_line_endings_choice(txx_newline_style style, bool mixed)
    {
        txx_newline_style current_style = text_state_.newline_style;
        txx_newline_style next_style = style;

        if (!text_state_.mixed_newlines && current_style == TXX_NEWLINE_UNKNOWN) {
            current_style = kDefaultPlatformNewlineStyle;
        }
        if (!mixed && next_style == TXX_NEWLINE_UNKNOWN) {
            next_style = kDefaultPlatformNewlineStyle;
        }

        if (text_state_.mixed_newlines == mixed && current_style == next_style) {
            sync_line_endings_menu();
            return;
        }

        text_state_.newline_style = mixed ? TXX_NEWLINE_MIXED : next_style;
        text_state_.mixed_newlines = mixed;
        sync_line_endings_menu();
        if (!dirty_) set_dirty(true);
    }

    void reset_text_state()
    {
        text_state_ = DocumentTextState{};
        sync_line_endings_menu();
    }

    void set_text_state(const txx_text_info &info)
    {
        text_state_.source_encoding = info.encoding == TXX_TEXT_ENCODING_UNKNOWN
                                    ? TXX_TEXT_ENCODING_UTF8
                                    : info.encoding;
        text_state_.newline_style = info.newline_style;
        text_state_.mixed_newlines = info.mixed_newlines != 0;
        text_state_.transcoded = info.was_transcoded != 0;
        text_state_.had_bom = info.has_bom != 0;
        sync_line_endings_menu();
    }

    void analyze_text_state_from_utf8(const uint8_t *data, std::size_t len)
    {
        int mixed = 0;
        text_state_.source_encoding = TXX_TEXT_ENCODING_UTF8;
        text_state_.newline_style = txx_text_detect_newlines(data, len, &mixed);
        text_state_.mixed_newlines = mixed != 0;
        text_state_.transcoded = false;
        text_state_.had_bom = false;
        sync_line_endings_menu();
    }

    void mark_plain_text_saved_as_utf8()
    {
        text_state_.source_encoding = TXX_TEXT_ENCODING_UTF8;
        text_state_.transcoded = false;
        text_state_.had_bom = false;
    }

    void maybe_warn_loaded_plain_text(const std::string &path, const txx_text_info &info)
    {
        if (!info.was_transcoded && !info.mixed_newlines && !info.has_bom) {
            return;
        }

        std::string message;
        const std::string name = display_name_for_path(path);

        if (info.was_transcoded) {
            message = "Opened \"" + name + "\" as ";
            message += txx_text_encoding_string(info.encoding);
            message += " and converted it to UTF-8.";
        } else if (info.has_bom) {
            message = "Opened \"" + name + "\" as ";
            message += txx_text_encoding_string(info.encoding);
            message += ".\nThe BOM was removed inside the editor.";
        }

        if (info.mixed_newlines) {
            message += "\nThe file uses mixed line endings. Use File/Line Endings to keep them or choose one style before saving.";
        } else if (info.newline_style != TXX_NEWLINE_UNKNOWN) {
            message += "\nPlain-text save will preserve ";
            message += txx_newline_style_string(info.newline_style);
            message += " line endings.";
        } else if (info.was_transcoded || info.has_bom) {
            message += "\nPlain-text save will use UTF-8 without a BOM.";
        }

        fl_message("%s", message.c_str());
    }

    bool load_plain_text_bytes(const uint8_t *data,
                               std::size_t len,
                               const std::string &path,
                               const char *action)
    {
        txx_buffer decoded;
        txx_text_info info;
        txx_status status;

        txx_buffer_init(&decoded);
        txx_text_info_init(&info);
        status = txx_text_decode_to_utf8(data, len, &decoded, &info);
        if (status != TXX_OK) {
            fl_alert("Could not %s the file as text.\nSupported encodings: UTF-8, UTF-16, Windows-1252, ISO-8859-1.", action);
            return false;
        }
        if (decoded.len > 0 && std::memchr(decoded.data, 0, decoded.len) != nullptr) {
            txx_buffer_free(&decoded);
            fl_alert("Could not %s the file because embedded NUL bytes are not supported in text documents.", action);
            return false;
        }

        std::string text(reinterpret_cast<const char *>(decoded.data), decoded.len);
        txx_buffer_free(&decoded);

        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());

        load_text(text.c_str());
        wipe_string(text);
        filepath_ = path;
        password_.clear();
        is_txx_ = false;
        set_text_state(info);
        set_dirty(false);
        apply_highlight_settings();
        maybe_warn_loaded_plain_text(path, info);
        return true;
    }

    txx_status current_text_for_save(txx_buffer *text)
    {
        char *raw = buf_->text();
        std::size_t len = std::strlen(raw);
        txx_newline_style style = text_state_.mixed_newlines
                                ? TXX_NEWLINE_MIXED
                                : text_state_.newline_style;
        if (style == TXX_NEWLINE_UNKNOWN) {
            style = kDefaultPlatformNewlineStyle;
        }
        txx_status status = txx_text_convert_newlines(
            reinterpret_cast<const uint8_t *>(raw), len, style, text);
        secure_free_text(raw);
        return status;
    }

    // Load text into the buffer without triggering the modify callback.
    void load_text(const char *text)
    {
        buf_->remove_modify_callback(modify_cb, this);
        buf_->text(text);
        buf_->add_modify_callback(modify_cb, this);
    }

    std::string buffer_text() const
    {
        char *raw = buf_->text();
        std::string text = raw ? raw : "";
        secure_free_text(raw);
        return text;
    }

    std::string buffer_range_text(int start, int end) const
    {
        char *raw = buf_->text_range(start, end);
        std::string text = raw ? raw : "";
        secure_free_text(raw);
        return text;
    }

    bool selected_text_range(int &start, int &end) const
    {
        start = 0;
        end = 0;
        if (!buf_->selected()) return false;
        if (!buf_->selection_position(&start, &end)) return false;
        return start < end;
    }

    bool selected_line_range(int &start, int &end) const
    {
        int sel_start = 0;
        int sel_end = 0;
        if (!selected_text_range(sel_start, sel_end)) return false;

        start = buf_->line_start(sel_start);
        int last_pos = sel_end;
        if (sel_end > sel_start && sel_end == buf_->line_start(sel_end)) {
            last_pos = sel_end - 1;
        }
        if (last_pos < start) last_pos = start;

        end = buf_->line_end(last_pos);
        if (end < buf_->length() && buf_->byte_at(end) == '\n') ++end;
        return true;
    }

    void apply_range_text(int start, int end, const std::string &text, bool select_result)
    {
        buf_->replace(start, end, text.c_str(), static_cast<int>(text.size()));

        const int new_end = start + static_cast<int>(text.size());
        if (select_result && new_end > start) {
            buf_->select(start, new_end);
        } else {
            buf_->unselect();
        }

        editor_->insert_position(new_end);
        editor_->show_insert_position();
        editor_->take_focus();
    }

    void apply_document_text(const std::string &text)
    {
        const int cursor = editor_->insert_position();
        const int line_index = buf_->count_lines(0, cursor);
        const int line_start = buf_->line_start(cursor);
        const int display_column = buf_->count_displayed_characters(line_start, cursor);

        buf_->replace(0, buf_->length(), text.c_str(), static_cast<int>(text.size()));
        buf_->unselect();

        int new_line_start = buf_->skip_lines(0, line_index);
        if (new_line_start > buf_->length()) new_line_start = buf_->length();
        int new_pos = buf_->skip_displayed_characters(new_line_start, display_column);
        int new_line_end = buf_->line_end(new_line_start);
        if (new_pos > new_line_end) new_pos = new_line_end;

        editor_->insert_position(new_pos);
        editor_->show_insert_position();
        editor_->take_focus();
    }

    // ---- actions ----

    void do_convert_tabs_to_spaces()
    {
        int start = 0;
        int end = 0;
        if (selected_text_range(start, end)) {
            std::string current = buffer_range_text(start, end);
            StringWiper current_wiper{current};
            const int column = buf_->count_displayed_characters(buf_->line_start(start), start);
            std::string updated = tt_tabs_to_spaces(current, settings_.indent_width, column);
            StringWiper updated_wiper{updated};
            if (updated != current) apply_range_text(start, end, updated, true);
            return;
        }

        std::string current = buffer_text();
        StringWiper current_wiper{current};
        std::string updated = tt_tabs_to_spaces(current, settings_.indent_width);
        StringWiper updated_wiper{updated};
        if (updated != current) apply_document_text(updated);
    }

    void do_convert_leading_spaces_to_tabs()
    {
        int start = 0;
        int end = 0;
        if (selected_line_range(start, end)) {
            std::string current = buffer_range_text(start, end);
            StringWiper current_wiper{current};
            std::string updated = tt_spaces_to_tabs(current, settings_.indent_width);
            StringWiper updated_wiper{updated};
            if (updated != current) apply_range_text(start, end, updated, true);
            return;
        }

        std::string current = buffer_text();
        StringWiper current_wiper{current};
        std::string updated = tt_spaces_to_tabs(current, settings_.indent_width);
        StringWiper updated_wiper{updated};
        if (updated != current) apply_document_text(updated);
    }

    void do_trim_trailing_spaces()
    {
        int start = 0;
        int end = 0;
        if (selected_line_range(start, end)) {
            std::string current = buffer_range_text(start, end);
            StringWiper current_wiper{current};
            std::string updated = tt_trim_trailing(current);
            StringWiper updated_wiper{updated};
            if (updated != current) apply_range_text(start, end, updated, true);
            return;
        }

        std::string current = buffer_text();
        StringWiper current_wiper{current};
        std::string updated = tt_trim_trailing(current);
        StringWiper updated_wiper{updated};
        if (updated != current) apply_document_text(updated);
    }

    void do_remove_blank_lines()
    {
        int start = 0;
        int end = 0;
        if (selected_line_range(start, end)) {
            std::string current = buffer_range_text(start, end);
            StringWiper current_wiper{current};
            std::string updated = tt_remove_blank_lines(current);
            StringWiper updated_wiper{updated};
            if (updated != current) apply_range_text(start, end, updated, true);
            return;
        }

        std::string current = buffer_text();
        StringWiper current_wiper{current};
        std::string updated = tt_remove_blank_lines(current);
        StringWiper updated_wiper{updated};
        if (updated != current) apply_document_text(updated);
    }

    void do_ensure_trailing_newline()
    {
        std::string current = buffer_text();
        StringWiper current_wiper{current};
        std::string updated = tt_ensure_trailing_newline(current);
        StringWiper updated_wiper{updated};
        if (updated != current) apply_document_text(updated);
    }

    void do_column_wrap()
    {
        int width = ask_column_width();
        if (width < 1) return;

        int start = 0;
        int end   = 0;
        if (selected_line_range(start, end)) {
            std::string current = buffer_range_text(start, end);
            StringWiper current_wiper{current};
            std::string updated = tt_wrap_columns(current, width);
            StringWiper updated_wiper{updated};
            if (updated != current) apply_range_text(start, end, updated, true);
            return;
        }

        std::string current = buffer_text();
        StringWiper current_wiper{current};
        std::string updated = tt_wrap_columns(current, width);
        StringWiper updated_wiper{updated};
        if (updated != current) apply_document_text(updated);
    }

    void do_duplicate_current_line()
    {
        const int cursor = editor_->insert_position();
        const int line_start = buf_->line_start(cursor);
        const int line_end = buf_->line_end(cursor);
        const bool has_newline = line_end < buf_->length() && buf_->byte_at(line_end) == '\n';

        char *raw = buf_->text_range(line_start, line_end);
        std::string line = raw ? raw : "";
        secure_free_text(raw);

        const int relative = std::min(std::max(cursor - line_start, 0), line_end - line_start);
        std::string insertion;
        int insert_at = line_end;
        int duplicate_start = line_end + 1;

        if (has_newline) {
            insertion = line;
            insertion.push_back('\n');
            insert_at = line_end + 1;
            duplicate_start = insert_at;
        } else {
            insertion.push_back('\n');
            insertion += line;
            duplicate_start = line_end + 1;
        }

        buf_->insert(insert_at, insertion.c_str(), static_cast<int>(insertion.size()));
        buf_->unselect();
        editor_->insert_position(duplicate_start + relative);
        editor_->show_insert_position();
        editor_->take_focus();
    }

    void do_remove_current_line()
    {
        if (buf_->length() == 0) return;

        const int cursor = editor_->insert_position();
        const int line_start = buf_->line_start(cursor);
        const int line_end = buf_->line_end(cursor);

        int remove_start = line_start;
        int remove_end = line_end;
        if (line_end < buf_->length() && buf_->byte_at(line_end) == '\n') {
            remove_end = line_end + 1;
        } else if (line_start > 0 && buf_->byte_at(line_start - 1) == '\n') {
            remove_start = line_start - 1;
        }

        buf_->remove(remove_start, remove_end);
        buf_->unselect();
        editor_->insert_position(std::min(remove_start, buf_->length()));
        editor_->show_insert_position();
        editor_->take_focus();
    }

    void do_new()
    {
        if (!check_unsaved()) return;
        load_text("");
        filepath_.clear();
        password_.clear();
        is_txx_ = false;
        reset_text_state();
        set_dirty(false);
        apply_highlight_settings();
    }

    void do_open()
    {
        if (!check_unsaved()) return;

        Fl_Native_File_Chooser fc;
        fc.title("Open File");
        fc.type(Fl_Native_File_Chooser::BROWSE_FILE);
        fc.filter("All Supported\t*.{txx,txt,md,ini,cfg,log,csv,yaml,yml,json,toml,xml}\nEncrypted TXX\t*.txx\nText Files\t*.{txt,md,ini,cfg,log,csv,yaml,yml,json,toml,xml}\nAll Files\t*");
        if (fc.show() != 0) return;

        std::string path = fc.filename();
        bool open_as_txx = has_txx_extension(path);

        if (open_as_txx) {
            std::string pw = ask_password("Open", "Password:");
            if (pw.empty()) return;

            txx_buffer raw;
            if (read_bytes(path.c_str(), &raw) != TXX_OK) {
                fl_alert("Could not read file.");
                wipe_string(pw);
                return;
            }

            txx_buffer decoded;
            txx_status s = txx_decode_text(raw.data, raw.len, pw.c_str(), &decoded);
            txx_buffer_secure_free(&raw);
            if (s != TXX_OK) {
                wipe_string(pw);
                fl_alert("Could not open: %s", txx_status_string(s));
                return;
            }

            std::string text(reinterpret_cast<char *>(decoded.data), decoded.len);
            analyze_text_state_from_utf8(decoded.data, decoded.len);
            txx_buffer_secure_free(&decoded);
            load_text(text.c_str());
            wipe_string(text);

            password_.set(pw);
            wipe_string(pw);
            filepath_ = path;
            is_txx_   = true;
            set_dirty(false);
        } else {
            txx_buffer raw;
            if (read_bytes(path.c_str(), &raw) != TXX_OK) {
                fl_alert("Could not read file.");
                return;
            }
            bool ok = load_plain_text_bytes(raw.data, raw.len, path, "open");
            txx_buffer_secure_free(&raw);
            if (!ok) return;
        }
    }

    bool do_save_txx_file(const char *path, const std::string &pw)
    {
        txx_buffer prepared;
        txx_buffer_init(&prepared);
        if (current_text_for_save(&prepared) != TXX_OK) {
            fl_alert("Could not prepare text for saving.");
            return false;
        }

        txx_codec_options opts;
        txx_codec_default_options(&opts);
        txx_buffer encoded;
        txx_status s = txx_encode_text(
            prepared.data, prepared.len,
            pw.c_str(), &opts, &encoded);
        txx_buffer_secure_free(&prepared);
        if (s != TXX_OK) {
            fl_alert("Encode failed: %s", txx_status_string(s));
            return false;
        }

        s = write_bytes(path, encoded.data, encoded.len);
        txx_buffer_secure_free(&encoded);
        if (s != TXX_OK) {
            fl_alert("Write failed.");
            return false;
        }
        return true;
    }

    bool do_save_txt_file(const char *path)
    {
        txx_buffer prepared;
        txx_buffer_init(&prepared);
        txx_status s = current_text_for_save(&prepared);
        if (s == TXX_OK) {
            s = write_bytes(path, prepared.data, prepared.len);
        }
        txx_buffer_secure_free(&prepared);
        if (s == TXX_OK) {
            mark_plain_text_saved_as_utf8();
        }
        if (s != TXX_OK) {
            fl_alert("Write failed.");
            return false;
        }
        return true;
    }

    bool do_save()
    {
        if (filepath_.empty()) return do_save_as();

        if (is_txx_) {
            std::string pw = password_.get();
            if (pw.empty()) {
                if (!ask_password_new("Save", pw)) return false;
            }
            bool ok = do_save_txx_file(filepath_.c_str(), pw);
            if (ok) {
                password_.set(pw);
                wipe_string(pw);
                set_dirty(false);
            } else {
                wipe_string(pw);
            }
            return ok;
        } else {
            bool ok = do_save_txt_file(filepath_.c_str());
            if (ok) set_dirty(false);
            return ok;
        }
    }

    bool do_save_as()
    {
        Fl_Native_File_Chooser fc;
        fc.title("Save As");
        fc.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
        fc.filter("Encrypted TXX\t*.txx\nPlain Text\t*.txt");
        fc.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
        fc.filter_value(is_txx_ ? 0 : 1);
        if (fc.show() != 0) return false;

        std::string path = fc.filename();

        // Determine format from the typed extension; filter_value() is
        // unreliable with the GTK file chooser on Linux.
        //
        // Rules:
        //  .txx extension  → save encrypted
        //  any other ext   → save as plain text (preserves .md, .ini, etc.)
        //  no extension    → append based on filter selection
        bool save_as_txx;
        {
            auto dot = path.rfind('.');
            bool has_ext = dot != std::string::npos
                           && path.find('/', dot) == std::string::npos
#ifdef _WIN32
                           && path.find('\\', dot) == std::string::npos
#endif
                           ;
            if (has_ext) {
                save_as_txx = has_txx_extension(path);
            } else {
                save_as_txx = (fc.filter_value() == 0);
                path += save_as_txx ? ".txx" : ".txt";
            }
        }

        // Button 1 is the default; fl_choice_n also distinguishes Escape/close
        // from button 0, so only an explicit export accepts this transition.
        if (!save_as_txx && is_txx_ &&
            fl_choice_n("Save an unencrypted copy of this document?",
                        "Save Plain Text", "Cancel", nullptr) != 0)
            return false;

        if (save_as_txx) {
            std::string pw;
            if (!ask_password_new("Save As", pw)) return false;
            bool ok = do_save_txx_file(path.c_str(), pw);
            if (ok) {
                filepath_ = path;
                is_txx_   = true;
                password_.set(pw);
                wipe_string(pw);
                set_dirty(false);
                apply_highlight_settings();
            } else {
                wipe_string(pw);
            }
            return ok;
        } else {
            bool ok = do_save_txt_file(path.c_str());
            if (ok) {
                filepath_ = path;
                is_txx_   = false;
                password_.clear();
                set_dirty(false);
                apply_highlight_settings();
            }
            return ok;
        }
    }

    // Open a file by path, detecting type via magic bytes (used by DnD).
    void do_open_path(const std::string &path)
    {
        if (!check_unsaved()) return;

        // Read the file once and branch on the magic bytes from the in-memory
        // buffer.  The previous version peeked at the magic via a separate
        // fopen() and then re-opened the file in read_bytes(), opening a
        // symlink-swap TOCTOU window on POSIX where an attacker could replace
        // the file between the two opens.
        txx_buffer raw;
        if (read_bytes(path.c_str(), &raw) != TXX_OK) {
            fl_alert("Cannot open file:\n%s", path.c_str());
            return;
        }

        const bool is_txx_file =
            raw.len >= TXX_MAGIC_SIZE &&
            memcmp(raw.data, TXX_MAGIC, TXX_MAGIC_SIZE) == 0;

        if (is_txx_file) {
            // Encrypted TXX — ask for password
            std::string pw = ask_password("Open", "Password:");
            if (pw.empty()) { txx_buffer_secure_free(&raw); return; }

            txx_buffer decoded;
            txx_status s = txx_decode_text(raw.data, raw.len, pw.c_str(), &decoded);
            txx_buffer_secure_free(&raw);
            if (s != TXX_OK) {
                wipe_string(pw);
                fl_alert("Could not open: %s", txx_status_string(s));
                return;
            }
            std::string text(reinterpret_cast<char *>(decoded.data), decoded.len);
            analyze_text_state_from_utf8(decoded.data, decoded.len);
            txx_buffer_secure_free(&decoded);
            load_text(text.c_str());
            wipe_string(text);
            password_.set(pw);
            wipe_string(pw);
            filepath_ = path;
            is_txx_   = true;
            set_dirty(false);
            apply_highlight_settings();
        } else {
            // Plain text with legacy encoding detection and UTF-8 conversion.
            bool ok = load_plain_text_bytes(raw.data, raw.len, path, "open");
            txx_buffer_secure_free(&raw);
            if (!ok) return;
        }
    }
    void do_open_containing_folder()
    {
        if (filepath_.empty()) return;

#ifdef _WIN32
        // Convert UTF-8 path to UTF-16 and ask Explorer to select the file.
        int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath_.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return;
        std::wstring wpath(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, filepath_.c_str(), -1, wpath.data(), wlen);
        if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();
        // Apply the standard Windows argument-quoting rules to the path so
        // quotes or trailing backslashes in it cannot alter how Explorer
        // parses the parameter string.
        std::wstring quoted = L"\"";
        size_t backslashes = 0;
        for (wchar_t c : wpath) {
            if (c == L'\\') {
                ++backslashes;
                quoted.push_back(c);
                continue;
            }
            if (c == L'"') {
                quoted.append(backslashes, L'\\');  // double the preceding run
                quoted += L"\\\"";                  // escaped quote
            } else {
                quoted.push_back(c);
            }
            backslashes = 0;
        }
        quoted.append(backslashes, L'\\');  // trailing run precedes the closing quote
        quoted += L"\"";
        std::wstring params = L"/select," + quoted;
        ShellExecuteW(nullptr, L"open", L"explorer.exe",
                      params.c_str(), nullptr, SW_SHOWNORMAL);
#else
#  ifdef __APPLE__
        const char *prog = "/usr/bin/open";
        const char *arg1 = "-R";
        const char *arg2 = filepath_.c_str();
        char *const argv[] = {
            const_cast<char *>(prog),
            const_cast<char *>(arg1),
            const_cast<char *>(arg2),
            nullptr
        };
#  else
        // Linux/BSD: open the containing directory via xdg-open.
        //
        // Canonicalise via realpath() so the path we hand to xdg-open is the
        // real directory and not a symlink an attacker could repoint between
        // editor startup and the user clicking the menu item.  We also lstat
        // the canonical result and require it to be an actual directory owned
        // by the current user (defense in depth on shared systems).
        std::size_t p = filepath_.find_last_of("/\\");
        std::string dir = (p == std::string::npos) ? std::string(".")
                                                   : filepath_.substr(0, p);
        if (dir.empty()) dir = "/";

        char resolved[PATH_MAX];
        if (realpath(dir.c_str(), resolved) == nullptr) return;

        struct stat sb;
        if (lstat(resolved, &sb) != 0) return;
        if (!S_ISDIR(sb.st_mode)) return;
        if (sb.st_uid != getuid()) return;

        // Resolve xdg-open from fixed system directories rather than $PATH:
        // a substituted helper would execute with the editor's user context.
        static const char *const kXdgDirs[] = {"/usr/bin", "/usr/local/bin", "/bin"};
        std::string prog_path;
        for (const char *xdg_dir : kXdgDirs) {
            std::string candidate = std::string(xdg_dir) + "/xdg-open";
            if (access(candidate.c_str(), X_OK) == 0) {
                prog_path = candidate;
                break;
            }
        }
        if (prog_path.empty()) return;
        const char *prog = prog_path.c_str();
        char *const argv[] = {
            const_cast<char *>(prog),
            resolved,
            nullptr
        };
#  endif
        pid_t pid = fork();
        if (pid == 0) {
            // Detach from the editor so the file manager isn't tied to it.
            setsid();
            execvp(prog, argv);
            _exit(127);
        } else if (pid > 0) {
            // Reap to avoid a zombie.
            int status = 0;
            (void)waitpid(pid, &status, WNOHANG);
        }
#endif
    }

    void do_show_find()
    {
        fr_win_->show_find();
    }

    void do_go_to_line()
    {
        if (buf_ == nullptr) return;

        const int total_lines = buf_->count_lines(0, buf_->length()) + 1;

        // Build a small modal dialog.
        Fl_Window *dlg = new Fl_Window(280, 90, "Go to Line");
        dlg->set_modal();

        char lbl_text[64];
        std::snprintf(lbl_text, sizeof(lbl_text), "Line (1\xe2\x80\x93%d):", total_lines);
        Fl_Box *lbl = new Fl_Box(10, 14, 100, 24, nullptr);
        lbl->copy_label(lbl_text);
        lbl->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);

        Fl_Input *inp = new Fl_Input(118, 14, 140, 24);
        inp->when(FL_WHEN_ENTER_KEY | FL_WHEN_NOT_CHANGED);

        // Pre-fill with the current line number.
        const int cur_line = buf_->count_lines(0, editor_->insert_position()) + 1;
        char pre[32];
        std::snprintf(pre, sizeof(pre), "%d", cur_line);
        inp->value(pre);

        bool accepted = false;

        struct Cbs {
            static void ok(Fl_Widget *w, void *v)
            { *static_cast<bool *>(v) = true; w->window()->hide(); }
            static void cancel(Fl_Widget *w, void *)
            { w->window()->hide(); }
            // Fl_Input fires this when Enter is pressed.
            static void inp_enter(Fl_Widget *w, void *v)
            { *static_cast<bool *>(v) = true; w->window()->hide(); }
        };

        Fl_Button *ok_btn  = new Fl_Button(90,  52, 80, 28, "OK");
        Fl_Button *can_btn = new Fl_Button(180, 52, 80, 28, "Cancel");
        ok_btn->callback(Cbs::ok,        &accepted);
        can_btn->callback(Cbs::cancel,   nullptr);
        inp->callback(Cbs::inp_enter,    &accepted);

        dlg->end();
        dlg->show();
        inp->take_focus();
        inp->insert_position(static_cast<int>(strlen(pre)), 0);  // select all
        while (dlg->shown()) Fl::wait();

        const char *txt = inp->value();
        char *end_ptr   = nullptr;
        long parsed     = txt ? std::strtol(txt, &end_ptr, 10) : 0;
        delete dlg;

        if (!accepted) return;

        // Clamp and jump.  Lines are 1-based; find the byte offset of line N.
        const int line = std::max(1, std::min(static_cast<int>(parsed), total_lines));
        const int pos  = buf_->skip_lines(0, line - 1);
        editor_->insert_position(pos);
        editor_->show_insert_position();
        editor_->take_focus();
        update_cursor_pos();
    }

    // ---- callbacks ----

    static void modify_cb(int pos, int nIns, int nDel, int, const char *, void *v)
    {
        auto *w = static_cast<EncryptEdWindow *>(v);
        if ((nIns > 0 || nDel > 0) && !w->dirty_) w->set_dirty(true);
        else if (nIns > 0 || nDel > 0) w->update_status();
        w->update_cursor_pos();
        if (nIns > 0 || nDel > 0) {
            if (w->highlight_off_for_size_) {
                // Re-enable once the document is small enough again.
                if (w->buf_->length() <= kMaxHighlightBytes) {
                    w->apply_highlight_settings();
                    w->update_status();
                }
            } else if (w->hl_) {
                if (w->hl_->mode() == HighlightMode::Code &&
                    w->buf_->length() > kMaxHighlightBytes) {
                    w->apply_highlight_settings();
                    w->update_status();
                } else {
                    w->hl_->on_modify(pos, nIns, nDel);
                }
            }
        }
    }

    static void menu_cb(Fl_Widget *, void *v)
    {
        auto             *win  = static_cast<EncryptEdWindow *>(v);
        if (win->plugin_running_) return;
        const Fl_Menu_Item *item = win->menu_->mvalue();
        if (!item || !item->label()) return;
        const char *lbl = item->label();

        // File
        if      (!strcmp(lbl, "New"))                        win->do_new();
        else if (!strcmp(lbl, "Open..."))                    win->do_open();
        else if (!strcmp(lbl, "Open Containing Folder"))     win->do_open_containing_folder();
        else if (!strcmp(lbl, "Save"))                       win->do_save();
        else if (!strcmp(lbl, "Save As..."))                 win->do_save_as();
        else if (!strcmp(lbl, "LF (Unix, macOS)"))           win->set_line_endings_choice(TXX_NEWLINE_LF, false);
        else if (!strcmp(lbl, "CRLF (Windows)"))             win->set_line_endings_choice(TXX_NEWLINE_CRLF, false);
        else if (!strcmp(lbl, "CR (Classic Mac)"))           win->set_line_endings_choice(TXX_NEWLINE_CR, false);
        else if (!strcmp(lbl, "Mixed (Preserve)"))           win->set_line_endings_choice(TXX_NEWLINE_MIXED, true);
        else if (!strcmp(lbl, "Quit"))                       { if (win->check_unsaved()) win->hide(); }
        // Edit
        else if (!strcmp(lbl, "Undo"))        EditorTextBuffer::undo_key(0, win->editor_);
        else if (!strcmp(lbl, "Redo"))        EditorTextBuffer::redo_key(0, win->editor_);
        else if (!strcmp(lbl, "Redo"))        Fl_Text_Editor::kf_redo(0,       win->editor_);
        else if (!strcmp(lbl, "Cut"))         Fl_Text_Editor::kf_cut(0,        win->editor_);
        else if (!strcmp(lbl, "Copy"))        Fl_Text_Editor::kf_copy(0,       win->editor_);
        else if (!strcmp(lbl, "Paste"))       Fl_Text_Editor::kf_paste(0,      win->editor_);
        else if (!strcmp(lbl, "Select All"))  Fl_Text_Editor::kf_select_all(0, win->editor_);
        else if (!strcmp(lbl, "Find"))        win->do_show_find();
        else if (!strcmp(lbl, "Find Next"))   win->fr_win_->find_again(false);
        else if (!strcmp(lbl, "Find Previous")) win->fr_win_->find_again(true);
        else if (!strcmp(lbl, "Find and Replace")) win->do_show_find();
        else if (!strcmp(lbl, "Go to Line...")) win->do_go_to_line();
        else if (!strcmp(lbl, "Convert Tabs to Spaces"))       win->do_convert_tabs_to_spaces();
        else if (!strcmp(lbl, "Convert Leading Spaces to Tabs")) win->do_convert_leading_spaces_to_tabs();
        else if (!strcmp(lbl, "Trim Trailing Spaces"))         win->do_trim_trailing_spaces();
        else if (!strcmp(lbl, "Remove Blank Lines"))           win->do_remove_blank_lines();
        else if (!strcmp(lbl, "Column Wrap..."))               win->do_column_wrap();
        else if (!strcmp(lbl, "Ensure Trailing Newline"))      win->do_ensure_trailing_newline();
        else if (!strcmp(lbl, "Duplicate Current Line"))       win->do_duplicate_current_line();
        else if (!strcmp(lbl, "Remove Current Line"))          win->do_remove_current_line();
        // Settings
        else if (!strcmp(lbl, "Preferences...")) win->do_preferences();
        // Help
        else if (!strcmp(lbl, "About"))
            show_about_dialog();
    }

    static void close_cb(Fl_Widget *, void *v)
    {
        auto *w = static_cast<EncryptEdWindow *>(v);
        if (!w->plugin_running_ && w->check_unsaved()) w->hide();
    }

public:
    // Must be called from main() after show() — fl_width() needs an active gc.
    void init_fonts_and_apply()
    {
        init_font_list();
        settings_.font = font_by_name(pending_font_name_.c_str());
        apply_settings();
    }

    void open_initial_path(const std::string &path) { do_open_path(path); }
    void open_dropped_file(const std::string &path)  { if (!plugin_running_) do_open_path(path); }

    void resize(int x, int y, int w, int h) override
    {
        Fl_Double_Window::resize(x, y, w, h);
        status_->resize(0, h - STAT_H, w - CURSOR_W - INS_W, STAT_H);
        cursor_pos_->resize(w - CURSOR_W - INS_W, h - STAT_H, CURSOR_W, STAT_H);
        insert_mode_box_->resize(w - INS_W, h - STAT_H, INS_W, STAT_H);
    }

    EncryptEdWindow(int W, int H)
        : Fl_Double_Window(W, H, "EncryptEd"),
          fr_win_(nullptr)
    {
        menu_ = new EditorMenuBar(0, 0, W, MENU_H);
        menu_->on_refresh = [this]{ refresh_menu_state(); };
        menu_->add("File/New",                  FL_CTRL + 'n', menu_cb, this);
        menu_->add("File/Open...",              FL_CTRL + 'o', menu_cb, this);
        menu_->add("File/Open Containing Folder", 0,           menu_cb, this, FL_MENU_DIVIDER);
        menu_->add("File/Save",                 FL_CTRL + 's', menu_cb, this);
        menu_->add("File/Save As...",           0,             menu_cb, this, FL_MENU_DIVIDER);
        menu_->add("File/Line Endings/LF (Unix, macOS)",  0, menu_cb, this, FL_MENU_RADIO);
        menu_->add("File/Line Endings/CRLF (Windows)",    0, menu_cb, this, FL_MENU_RADIO);
        menu_->add("File/Line Endings/CR (Classic Mac)",  0, menu_cb, this, FL_MENU_RADIO);
        menu_->add("File/Line Endings/Mixed (Preserve)",  0, menu_cb, this, FL_MENU_RADIO | FL_MENU_DIVIDER);
        menu_->add("File/Quit",                 FL_CTRL + 'q', menu_cb, this);

        sync_line_endings_menu();

        menu_->add("Edit/Undo",       FL_CTRL + 'z', menu_cb, this);
        menu_->add("Edit/Redo",       FL_CTRL + FL_SHIFT + 'z', menu_cb, this);
        menu_->add("Edit/Redo",       FL_CTRL + 'y', menu_cb, this, FL_MENU_DIVIDER);
        menu_->add("Edit/Cut",        FL_CTRL + 'x', menu_cb, this);
        menu_->add("Edit/Copy",       FL_CTRL + 'c', menu_cb, this);
        menu_->add("Edit/Paste",      FL_CTRL + 'v', menu_cb, this);
        menu_->add("Edit/Select All", FL_CTRL + 'a', menu_cb, this, FL_MENU_DIVIDER);
        menu_->add("Edit/Find",        FL_CTRL + 'f', menu_cb, this);
        menu_->add("Edit/Find Next", FL_F + 3, menu_cb, this);
        menu_->add("Edit/Find Previous", FL_F + 2, menu_cb, this);
        menu_->add("Edit/Find and Replace", FL_CTRL + 'r', menu_cb, this);
        menu_->add("Edit/Go to Line...", FL_CTRL + 'g', menu_cb, this);
        menu_->add("Edit/Text/Convert Tabs to Spaces",         0, menu_cb, this);
        menu_->add("Edit/Text/Convert Leading Spaces to Tabs", 0, menu_cb, this);
        menu_->add("Edit/Text/Trim Trailing Spaces",           0, menu_cb, this);
        menu_->add("Edit/Text/Remove Blank Lines",             0, menu_cb, this);
        menu_->add("Edit/Text/Column Wrap...",                 0, menu_cb, this);
        menu_->add("Edit/Text/Ensure Trailing Newline",        0, menu_cb, this, FL_MENU_DIVIDER);
        menu_->add("Edit/Text/Duplicate Current Line",         FL_CTRL + 'd',              menu_cb, this);
        menu_->add("Edit/Text/Remove Current Line",            FL_CTRL + FL_SHIFT + 'k',   menu_cb, this);

        menu_->add("Plugins", 0, nullptr, nullptr, FL_SUBMENU_POINTER);

        menu_->add("Settings/Preferences...", 0, menu_cb, this);

        menu_->add("Help/About", 0, menu_cb, this);

        buf_    = new EditorTextBuffer();
        editor_ = new DndTextEditor(0, MENU_H, W, H - MENU_H - STAT_H,
                      [this](const std::string &p){ if (!plugin_running_) do_open_path(p); });
        editor_->buffer(buf_);
        editor_->wrap_mode(Fl_Text_Editor::WRAP_AT_BOUNDS, 0);
        editor_->textfont(FL_COURIER);
        editor_->textsize(14);
        buf_->add_modify_callback(modify_cb, this);

        status_ = new Fl_Box(0, H - STAT_H, W - CURSOR_W - INS_W, STAT_H, "New document");
        status_->box(FL_FLAT_BOX);
        status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        status_->labelsize(11);

        cursor_pos_ = new Fl_Box(W - CURSOR_W - INS_W, H - STAT_H, CURSOR_W, STAT_H, "");
        cursor_pos_->box(FL_FLAT_BOX);
        cursor_pos_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        cursor_pos_->labelsize(11);

        insert_mode_box_ = new Fl_Box(W - INS_W, H - STAT_H, INS_W, STAT_H, "INS");
        insert_mode_box_->box(FL_FLAT_BOX);
        insert_mode_box_->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
        insert_mode_box_->labelsize(11);

        editor_->set_cursor_change_callback([this]{ update_cursor_pos(); });
        editor_->set_insert_mode_change_callback([this]{ update_insert_mode(); });

        resizable(editor_);
        callback(close_cb, this);
        end();
        apply_app_icon(*this);

        // Created after end() so FLTK doesn't accidentally add these
        // widgets to the EncryptEdWindow's group.
        fr_win_ = new FindReplaceWindow();
        fr_win_->attach(editor_, buf_);

        load_settings();
        apply_settings();

        reload_plugins(false);
        update_title();
        update_status();
        update_insert_mode();
        update_cursor_pos();
        refresh_menu_state();
    }

    ~EncryptEdWindow()
    {
        buf_->remove_modify_callback(modify_cb, this);
        delete hl_;
        password_.clear();
        delete fr_win_;
        editor_->buffer(nullptr);
        delete buf_;
    }
};

// ---------------------------------------------------------------------------
// Windows: accept file drops via WM_DROPFILES.
// FLTK's OLE drop target only advertises text formats; Explorer drags files
// as CF_HDROP which FLTK rejects, showing a "not-allowed" cursor.  We subclass
// the HWND message procedure to catch WM_DROPFILES ourselves.
// ---------------------------------------------------------------------------
#ifdef _WIN32
static WNDPROC          g_orig_wndproc   = nullptr;
static EncryptEdWindow *g_drop_win       = nullptr;

static LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DROPFILES) {
        HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        UINT  count = DragQueryFileW(hDrop, 0xFFFFFFFFu, nullptr, 0);
        if (count > 0 && g_drop_win) {
            WCHAR wpath[32768];
            if (DragQueryFileW(hDrop, 0, wpath, static_cast<UINT>(std::size(wpath)))) {
                // Convert to UTF-8 for FLTK
                int need = WideCharToMultiByte(
                    CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                if (need > 1) {
                    std::string path(static_cast<std::size_t>(need - 1), '\0');
                    WideCharToMultiByte(
                        CP_UTF8, 0, wpath, -1, &path[0], need, nullptr, nullptr);
                    g_drop_win->open_dropped_file(path);
                }
            }
        }
        DragFinish(hDrop);
        return 0;
    }
    return CallWindowProcW(g_orig_wndproc, hwnd, msg, wParam, lParam);
}

static void enable_file_drop(HWND hwnd, EncryptEdWindow *win)
{
    g_drop_win     = win;
    DragAcceptFiles(hwnd, TRUE);
    g_orig_wndproc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(EditorWndProc)));
}
#endif

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

#ifdef _WIN32
static std::string initial_path_from_cmdline()
{
    int wargc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv) return {};
    std::string result;
    if (wargc >= 2) {
        unsigned wlen   = static_cast<unsigned>(wcslen(wargv[1]));
        unsigned needed = fl_utf8fromwc(nullptr, 0, wargv[1], wlen);
        result.resize(needed + 1, '\0');
        fl_utf8fromwc(&result[0], needed + 1, wargv[1], wlen);
        result.resize(needed);
    }
    LocalFree(wargv);
    return result;
}
#endif

int main(int argc, char **argv)
{
#ifndef _WIN32
    // Sensitive documents and the in-memory password must not leak through
    // crash dumps; disable them outright (best effort).
    {
        struct rlimit cores{};
        setrlimit(RLIMIT_CORE, &cores);
    }
#endif
    Fl::scheme("gtk+");
    Fl_Window::default_xclass("encryptEd");
    EncryptEdWindow win(900, 600);
    win.show();
    win.init_fonts_and_apply();

#ifdef _WIN32
    enable_file_drop(fl_xid(&win), &win);
    std::string initial = initial_path_from_cmdline();
    (void)argc; (void)argv;
#else
    std::string initial = (argc > 1) ? std::string(argv[1]) : std::string{};
#endif
    if (!initial.empty()) win.open_initial_path(initial);

    return Fl::run();
}
