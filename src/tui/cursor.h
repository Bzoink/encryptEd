#ifndef TUI_CURSOR_H
#define TUI_CURSOR_H

#include <ftxui/dom/node.hpp>
#include <utility>
#include <iostream>

// FTXUI 5 moves the terminal cursor around while repainting, but only sets
// its visibility at the end. Hide it before Render() returns control to Draw()
// so it cannot flash at the intermediate drawing positions.
inline void hide_cursor_for_redraw() {
    std::cout << "\x1b[?25l";
}

// Start every frame with a hidden cursor. Visible input fields opt in via
// focusCursor decorators, rather than inheriting the previous frame's cursor.
class CursorFrame : public ftxui::Node {
public:
    explicit CursorFrame(ftxui::Element child) : Node({std::move(child)}) {}
    void ComputeRequirement() override {
        children_[0]->ComputeRequirement();
        requirement_ = children_[0]->requirement();
    }
    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        children_[0]->SetBox(box);
    }
    void Render(ftxui::Screen &screen) override {
        screen.SetCursor({0, 0, ftxui::Screen::Cursor::Hidden});
        children_[0]->Render(screen);
        const auto cursor = screen.cursor();
        if (cursor.x < 0 || cursor.y < 0 || cursor.x >= screen.dimx() || cursor.y >= screen.dimy())
            screen.SetCursor({0, 0, ftxui::Screen::Cursor::Hidden});
    }
};

inline ftxui::Element cursor_frame(ftxui::Element child) {
    return std::make_shared<CursorFrame>(std::move(child));
}
#endif
