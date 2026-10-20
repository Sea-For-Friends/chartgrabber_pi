// SPDX-License-Identifier: GPL-3.0-or-later
#include "question.h"

#include "guard.h"
#include "look.h"

#include <wx/checkbox.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <utility>

Question::Question(wxWindow* parent, const Look& look, const wxString& title, const wxString& text,
                   Buttons buttons, const wxString& checkBoxLabel, const wxArrayString& choices,
                   std::function<void(bool, bool, int)> onAnswer, const wxString& yesLabel,
                   const wxString& noLabel)
    : wxDialog(parent, wxID_ANY, title), look_(look), onAnswer_(std::move(onAnswer)) {
    const bool yesNo = buttons != Buttons::OkCancel;
    auto* column = new wxBoxSizer(wxVERTICAL);
    auto* label = new wxStaticText(this, wxID_ANY, text);
    label->Wrap(FromDIP(400));
    column->Add(label, 0, wxALL, 12);
    for (size_t i = 0; i < choices.size(); ++i) {
        radios_.push_back(new wxRadioButton(this, wxID_ANY, choices[i], wxDefaultPosition, wxDefaultSize,
                                            i == 0 ? wxRB_GROUP : 0));
        column->Add(radios_.back(), 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    }
    if (!radios_.empty())
        radios_[0]->SetValue(true);
    if (!checkBoxLabel.empty()) {
        check_ = new wxCheckBox(this, wxID_ANY, checkBoxLabel);
        column->Add(check_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    }
    wxSizer* row = CreateStdDialogButtonSizer(
        yesNo ? (buttons == Buttons::Custom ? wxYES_NO : (wxYES_NO | wxNO_DEFAULT)) : (wxOK | wxCANCEL));
    if (const int minHeight = look.minControlHeight(); minHeight > 0)
        for (const int id : {wxID_YES, wxID_NO, wxID_OK, wxID_CANCEL})
            if (wxWindow* b = FindWindow(id))
                b->SetMinSize(wxSize(-1, minHeight));
    if (buttons == Buttons::DeleteCancel) {
        FindWindow(wxID_YES)->SetLabel(_("Delete"));
        FindWindow(wxID_NO)->SetLabel(_("Cancel"));
    }
    if (buttons == Buttons::Custom) {
        FindWindow(wxID_YES)->SetLabel(yesLabel);
        FindWindow(wxID_NO)->SetLabel(noLabel);
    }
    column->Add(row, 0, wxEXPAND | wxALL, 8);
    SetEscapeId(yesNo ? wxID_NO : wxID_CANCEL); // wx would pick the affirmative button of a Yes / No pair
    SetSizerAndFit(column);

    // Dynamic handlers run before wxDialog's own (which would end a modal loop that does not exist).
    const int yes = yesNo ? wxID_YES : wxID_OK, no = yesNo ? wxID_NO : wxID_CANCEL;
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { cg::guarded("question yes", [&] { answer(true); }); }, yes);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { cg::guarded("question no", [&] { answer(false); }); }, no);
    Bind(wxEVT_CLOSE_WINDOW,
         [this](wxCloseEvent&) { cg::guarded("question close", [&] { answer(false); }); });

    look.apply(this);
    lookToken_ = look.subscribe([this] { look_.apply(this); });
    CentreOnParent();
}

Question::~Question() {
    look_.unsubscribe(lookToken_);
}

wxWeakRef<Question> Question::ask(wxWindow* parent, const Look& look, const wxString& title,
                                  const wxString& text, Buttons buttons, const wxString& checkBoxLabel,
                                  std::function<void(bool, bool)> onAnswer) {
    return show(
        new Question(parent, look, title, text, buttons, checkBoxLabel, wxArrayString(),
                     [done = std::move(onAnswer)](bool yes, bool ticked, int) { done(yes, ticked); }));
}

wxWeakRef<Question> Question::askCustom(wxWindow* parent, const Look& look, const wxString& title,
                                        const wxString& text, const wxString& yesLabel,
                                        const wxString& noLabel, const wxString& checkBoxLabel,
                                        std::function<void(bool, bool)> onAnswer) {
    return show(new Question(
        parent, look, title, text, Buttons::Custom, checkBoxLabel, wxArrayString(),
        [done = std::move(onAnswer)](bool yes, bool ticked, int) { done(yes, ticked); }, yesLabel, noLabel));
}

void Question::askModal(wxWindow* parent, const Look& look, const wxString& title, const wxString& text,
                        Buttons buttons, std::function<void(bool)> onAnswer) {
    auto* q = new Question(parent, look, title, text, buttons, wxString(), wxArrayString(),
                           [done = std::move(onAnswer)](bool yes, bool, int) { done(yes); });
    q->ShowModal(); // answer() ends the loop; the window is deleted at the next idle
}

wxWeakRef<Question> Question::askChoice(wxWindow* parent, const Look& look, const wxString& title,
                                        const wxString& text, const wxArrayString& choices,
                                        std::function<void(int)> onAnswer) {
    return show(new Question(parent, look, title, text, Buttons::OkCancel, wxString(), choices,
                             [done = std::move(onAnswer)](bool, bool, int choice) { done(choice); }));
}

wxWeakRef<Question> Question::show(Question* q) {
    wxWeakRef<Question> ref(q);
    q->Show();
    q->Raise();
    return ref;
}

void Question::answer(bool yes) {
    if (!onAnswer_) // a second event before the deferred Destroy: the answer was delivered already
        return;
    const auto done = std::move(onAnswer_);
    onAnswer_ = nullptr;
    const bool ticked = check_ && check_->GetValue();
    int choice = -1;
    for (size_t i = 0; yes && i < radios_.size(); ++i)
        if (radios_[i]->GetValue())
            choice = static_cast<int>(i);
    if (IsModal())
        EndModal(yes ? wxID_YES : wxID_NO);
    Hide();
    Destroy(); // deferred to idle: this handler is still running
    done(yes, ticked, choice);
}
