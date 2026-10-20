// SPDX-License-Identifier: GPL-3.0-or-later
// A question that does not run a nested modal loop : a plain dialog shown
// with Show, so OpenCPN keeps running (and may close and unload the plugin) while it is open. GUI thread.
#pragma once

#include <wx/arrstr.h>
#include <wx/dialog.h>
#include <wx/weakref.h>

#include <functional>
#include <vector>

class Look;
class wxCheckBox;
class wxRadioButton;

class Question : public wxDialog {
public:
    // YesNo: default No; OkCancel: default OK; DeleteCancel: the Yes / No pair labelled Delete / Cancel,
    // default Cancel (onAnswer yes = Delete). Escape and the close box answer No / Cancel.
    enum class Buttons { YesNo, OkCancel, DeleteCancel, Custom };

    // Shows the question and returns at once. `onAnswer(yes, ticked)` is called ONCE, after the window is
    // hidden; a question destroyed with its parent calls nothing. The parent is the top-level window of the
    // asker, so the question dies with it. `checkBoxLabel` empty: no check box (ticked is then false).
    static wxWeakRef<Question> ask(wxWindow* parent, const Look& look, const wxString& title,
                                   const wxString& text, Buttons buttons, const wxString& checkBoxLabel,
                                   std::function<void(bool yes, bool ticked)> onAnswer);
    // The Yes / No pair with the caller's labels; default button = yes (Enter), Escape and the close
    // box answer no. `onAnswer(yes, ticked)` as ask.
    static wxWeakRef<Question> askCustom(wxWindow* parent, const Look& look, const wxString& title,
                                         const wxString& text, const wxString& yesLabel,
                                         const wxString& noLabel, const wxString& checkBoxLabel,
                                         std::function<void(bool yes, bool ticked)> onAnswer);
    // The same question run modally (for an asker that is itself inside OpenCPN's modal loop, where a
    // modeless window under the GTK grab may take no input). Returns after the answer; `onAnswer` as above.
    static void askModal(wxWindow* parent, const Look& look, const wxString& title, const wxString& text,
                         Buttons buttons, std::function<void(bool yes)> onAnswer);
    // OK / Cancel and one radio button per choice in a column (selection 0). `onAnswer(choice)` is called
    // ONCE after the window is hidden: the selected index on OK, -1 on Cancel, Escape or the close box.
    static wxWeakRef<Question> askChoice(wxWindow* parent, const Look& look, const wxString& title,
                                         const wxString& text, const wxArrayString& choices,
                                         std::function<void(int choice)> onAnswer);
    ~Question() override;

private:
    Question(wxWindow* parent, const Look& look, const wxString& title, const wxString& text, Buttons buttons,
             const wxString& checkBoxLabel, const wxArrayString& choices,
             std::function<void(bool, bool, int)> onAnswer, const wxString& yesLabel = wxString(),
             const wxString& noLabel = wxString());
    static wxWeakRef<Question> show(Question* q);
    void answer(bool yes);

    const Look& look_;
    int lookToken_ = 0;
    wxCheckBox* check_ = nullptr;
    std::vector<wxRadioButton*> radios_;
    std::function<void(bool yes, bool ticked, int choice)> onAnswer_;
};
