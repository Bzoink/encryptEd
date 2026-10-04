#ifndef ENCRYPTED_PLUGIN_DIALOG_H
#define ENCRYPTED_PLUGIN_DIALOG_H

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Button.H>
#include "plugins/plugins.h"

class PluginDialogs : public plugins::DialogHost {
    std::string title_;
public:
    explicit PluginDialogs(std::string title) : title_(std::move(title)) {}
    plugins::DialogReply show(const plugins::DialogRequest &request) override
    {
        using plugins::DialogKind;
        plugins::DialogReply reply;
        Fl_Window dialog(500, 300, title_.c_str());
        dialog.set_modal();
        Fl_Text_Buffer prompt_buffer;
        prompt_buffer.text(request.prompt.c_str());
        Fl_Text_Display prompt(15, 10, 470, 90);
        prompt.buffer(prompt_buffer);
        prompt.textfont(FL_HELVETICA);
        prompt.textsize(14);
        prompt.wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
        Fl_Input input(15, 110, 470, 30);
        input.maximum_size(65536);
        input.value(request.initial.c_str());
        Fl_Hold_Browser choices(15, 105, 470, 140);
        choices.format_char(0); // Plugin choices are literal text, not browser markup.
        for (const auto &choice : request.choices) choices.add(choice.c_str());
        choices.value(1);
        Fl_Return_Button ok(395, 260, 90, 28, request.kind == DialogKind::Confirm ? "Yes" : "OK");
        Fl_Button no(295, 260, 90, 28, "No");
        Fl_Button cancel(195, 260, 90, 28, "Cancel");
        if (request.kind != DialogKind::Input) input.hide();
        if (request.kind != DialogKind::Choose) choices.hide();
        if (request.kind != DialogKind::Confirm) no.hide();
        // Message dialogs also provide Cancel to stop a sequence of prompts.
        ok.callback([](Fl_Widget *w, void *data) {
            auto &r = *static_cast<plugins::DialogReply *>(data);
            r.accepted = true; r.confirmed = true; w->window()->hide();
        }, &reply);
        no.callback([](Fl_Widget *w, void *data) {
            auto &r = *static_cast<plugins::DialogReply *>(data);
            r.accepted = true; r.confirmed = false; w->window()->hide();
        }, &reply);
        cancel.callback([](Fl_Widget *w, void *) { w->window()->hide(); });
        dialog.callback([](Fl_Widget *w, void *) { w->hide(); });
        dialog.end();
        dialog.show();
        if (request.kind == DialogKind::Input) input.take_focus();
        while (dialog.shown()) Fl::wait();
        if (reply.accepted) {
            if (request.kind == DialogKind::Input) reply.text = input.value();
            if (request.kind == DialogKind::Choose) reply.choice = choices.value();
        }
        // Best effort, consistent with the existing password dialog's input wipe.
        std::string zeros(static_cast<std::size_t>(input.size()), '\0');
        input.replace(0, input.size(), zeros.c_str(), static_cast<int>(zeros.size()));
        input.value("");
        return reply;
    }
};
#endif
