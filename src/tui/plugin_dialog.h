#ifndef TUI_PLUGIN_DIALOG_H
#define TUI_PLUGIN_DIALOG_H

#include "plugins/plugins.h"
#include "tui/cursor.h"
#include <algorithm>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>
#include <ftxui/screen/string.hpp>
extern "C" {
#include "platform/secure_mem.h"
}

// Nested FTXUI loops suspend the editor while Lua synchronously awaits a reply.
class TuiPluginDialogs : public plugins::DialogHost {
public:
    explicit TuiPluginDialogs(std::string title) : title_(std::move(title)) {}
    plugins::DialogReply show(const plugins::DialogRequest &request) override {
        using namespace ftxui;
        auto screen = ScreenInteractive::Fullscreen();
        plugins::DialogReply reply;
        std::string value = request.initial;
        int selected = 0;
        float prompt_scroll = 0;
        bool prompt_overflows = false;
        Box prompt_box{};
        auto scroll_up = Button("Scroll Up", [&] { prompt_scroll = std::max(0.0f, prompt_scroll - 0.2f); }, ButtonOption::Simple());
        auto scroll_down = Button("Scroll Down", [&] { prompt_scroll = std::min(1.0f, prompt_scroll + 0.2f); }, ButtonOption::Simple());
        auto scroll_buttons = Maybe(Container::Horizontal({scroll_up, scroll_down}), &prompt_overflows);
        auto accept = [&] {
            reply.accepted = true; reply.text = value;
            reply.choice = selected + 1; reply.confirmed = true; screen.Exit();
        };
        auto cancel = [&] { screen.Exit(); };
        auto ok = Button(request.kind == plugins::DialogKind::Confirm ? "Yes" : "OK", accept, ButtonOption::Simple());
        auto no = Button(request.kind == plugins::DialogKind::Confirm ? "No" : "Cancel", [&] {
            if (request.kind == plugins::DialogKind::Confirm) reply.accepted = true;
            cancel();
        }, ButtonOption::Simple());
        Component field;
        if (request.kind == plugins::DialogKind::Input) {
            auto option = InputOption::Default();
            option.multiline = false;
            option.on_enter = accept;
            field = Input(&value, "", option);
        } else if (request.kind == plugins::DialogKind::Choose) {
            auto option = MenuOption::Vertical();
            option.on_enter = accept;
            field = Menu(&request.choices, &selected, option);
        }
        Components children{scroll_buttons};
        if (field) children.push_back(field);
        children.push_back(Container::Horizontal(request.kind == plugins::DialogKind::Message
            ? Components{ok} : Components{ok, no}));
        auto container = Container::Vertical(children);
        if (field) field->TakeFocus();
        else if (request.kind == plugins::DialogKind::Confirm) no->TakeFocus();
        else ok->TakeFocus();
        auto renderer = Renderer(container, [&] {
            hide_cursor_for_redraw();
            const int field_height = std::max(1, std::min(12, (Terminal::Size().dimy - 16) / 2));
            int prompt_height = std::max(1, std::min(
                request.kind == plugins::DialogKind::Message ? 24 : 10,
                Terminal::Size().dimy - 14 - (request.kind == plugins::DialogKind::Choose ? field_height : 0)));
            Elements prompt_lines;
            int prompt_rows = 0;
            int prompt_width = std::max(1, std::min(72, Terminal::Size().dimx - 6));
            std::size_t start = 0;
            do {
                auto end = request.prompt.find('\n', start);
                auto line = request.prompt.substr(start, end == std::string::npos ? end : end - start);
                prompt_rows += std::max(1, (string_width(line) + prompt_width - 1) / prompt_width);
                prompt_lines.push_back(line.empty() ? text("") : paragraph(line));
                if (end == std::string::npos) break;
                start = end + 1;
            } while (true);
            prompt_overflows = prompt_rows > prompt_height;
            auto prompt = vbox(std::move(prompt_lines)) | focusPositionRelative(0, prompt_scroll)
                | vscroll_indicator | yframe | size(HEIGHT, LESS_THAN, prompt_height) | reflect(prompt_box);
            Elements rows{prompt};
            if (prompt_overflows) rows.push_back(scroll_buttons->Render());
            rows.push_back(separator());
            if (field) {
                auto content = field->Render();
                if (request.kind == plugins::DialogKind::Choose)
                    content = content | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, field_height);
                rows.push_back(content);
                rows.push_back(separator());
            }
            rows.push_back(children.back()->Render());
            rows.push_back(text("Tab: buttons   Enter: activate   Esc: cancel   PgUp/PgDn: prompt") | dim);
            return window(text(" " + title_ + " "), vbox(std::move(rows)))
                | size(WIDTH, LESS_THAN, 76) | size(HEIGHT, LESS_THAN, Terminal::Size().dimy - 2) | center | cursor_frame;
        });
        auto modal = CatchEvent(renderer, [&](Event event) {
            if (event.is_mouse() && prompt_box.Contain(event.mouse().x, event.mouse().y)) {
                if (event.mouse().button == Mouse::WheelUp) {
                    prompt_scroll = std::max(0.0f, prompt_scroll - 0.2f); return true;
                }
                if (event.mouse().button == Mouse::WheelDown) {
                    prompt_scroll = std::min(1.0f, prompt_scroll + 0.2f); return true;
                }
            }
            if (event == Event::PageDown) { prompt_scroll = std::min(1.0f, prompt_scroll + 0.2f); return true; }
            if (event == Event::PageUp) { prompt_scroll = std::max(0.0f, prompt_scroll - 0.2f); return true; }
            if (event == Event::Escape || event.input() == "\x03") { cancel(); return true; }
            return false;
        });
        screen.Loop(modal);
        if (!value.empty()) txx_secure_zero(value.data(), value.size());
        return reply;
    }
private:
    std::string title_;
};
#endif
