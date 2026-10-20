// SPDX-License-Identifier: GPL-3.0-or-later
// : Question is a modeless dialog; every way of answering delivers
// exactly one call with the right values, the window is gone afterwards, and a question destroyed with its
// parent calls nothing. A window test (cg_gui_tests).
#include "log_capture.h"
#include "look.h"
#include "ocpn_fakes.h"
#include "question.h"
#include "wx_env.h"
#include "wx_loop.h"

#include <gtest/gtest.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/frame.h>
#include <wx/modalhook.h>
#include <wx/radiobut.h>
#include <wx/stattext.h>

#include <functional>
#include <string>
#include <vector>

#include <ocpn_plugin.h>

namespace {

struct Call {
    bool yes;
    bool ticked;
};

class CountingHook : public wxModalDialogHook { // sees every ShowModal of the process
public:
    CountingHook() { Register(); }
    ~CountingHook() override { Unregister(); }
    int entered = 0;

protected:
    int Enter(wxDialog*) override {
        ++entered;
        return wxID_NONE;
    }
    void Exit(wxDialog*) override {}
};

class QuestionTest : public LoopFixture {
protected:
    void SetUp() override {
        if (!wxTheApp || !wxTheApp->IsGUI())
            GTEST_SKIP() << "no display: the Question tests were NOT run";
        LoopFixture::SetUp();
        ocpn_fake::touch = false;
        ocpn_fake::scale = 1.0;
        parent_ = new wxFrame(nullptr, wxID_ANY, "parent");
        parent_->Show();
    }
    void TearDown() override {
        if (parent_)
            parent_->Destroy();
        runFor(40);
        LoopFixture::TearDown();
    }
    wxWeakRef<Question> ask(Question::Buttons b, const wxString& check) {
        return Question::ask(parent_, look_, "Chart Grabber", "A chart with this name exists. Replace it?", b,
                             check, [this](bool yes, bool ticked) { calls_.push_back({yes, ticked}); });
    }
    static wxButton* button(Question* q, int id) { return dynamic_cast<wxButton*>(q->FindWindow(id)); }
    static void press(Question* q, int id) {
        wxButton* b = button(q, id);
        ASSERT_NE(b, nullptr) << id;
        wxCommandEvent e(wxEVT_BUTTON, id);
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    }
    static wxCheckBox* box(Question* q) {
        for (wxWindow* w : q->GetChildren())
            if (auto* c = dynamic_cast<wxCheckBox*>(w))
                return c;
        return nullptr;
    }
    bool gone(wxWeakRef<Question>& q) {
        return runUntil([&] { return !q; }, 5000);
    }

    Look look_;
    wxFrame* parent_ = nullptr;
    std::vector<Call> calls_;
};

} // namespace

TEST_F(QuestionTest, AskReturnsAtOnceWithTheWindowShownAndNoModalLoop) {
    CountingHook hook;
    auto q = ask(Question::Buttons::YesNo, wxString());
    ASSERT_TRUE(q);
    EXPECT_TRUE(q->IsShown());
    EXPECT_FALSE(q->IsModal());
    EXPECT_TRUE(calls_.empty());
    EXPECT_EQ(box(q), nullptr) << "no label: no check box";
    EXPECT_EQ(q->GetTitle(), "Chart Grabber");
    EXPECT_EQ(q->GetParent(), parent_);
    runFor(60); // the loop runs normally while the question is open
    EXPECT_TRUE(q);
    EXPECT_EQ(hook.entered, 0);
    press(q, wxID_NO);
    EXPECT_TRUE(gone(q));
}

TEST_F(QuestionTest, EveryAnswerDeliversExactlyOneCallAndTheWindowGoes) {
    struct Row {
        const char* name;
        Question::Buttons b;
        bool tick;
        std::function<void(Question*)> act;
        Call expect;
    };
    const auto key = [](Question* q, int code) {
        wxKeyEvent e(wxEVT_CHAR_HOOK);
        e.m_keyCode = code;
        e.SetEventObject(q);
        q->GetEventHandler()->ProcessEvent(e);
    };
    const std::vector<Row> rows = {
        {"Yes", Question::Buttons::YesNo, false, [](Question* q) { press(q, wxID_YES); }, {true, false}},
        {"No", Question::Buttons::YesNo, false, [](Question* q) { press(q, wxID_NO); }, {false, false}},
        {"Escape (YesNo)",
         Question::Buttons::YesNo,
         false,
         [&](Question* q) { key(q, WXK_ESCAPE); },
         {false, false}},
        {"close box (YesNo)",
         Question::Buttons::YesNo,
         false,
         [](Question* q) { q->Close(); },
         {false, false}},
        {"OK ticked",
         Question::Buttons::OkCancel,
         true,
         [](Question* q) { press(q, wxID_OK); },
         {true, true}},
        {"OK not ticked",
         Question::Buttons::OkCancel,
         false,
         [](Question* q) { press(q, wxID_OK); },
         {true, false}},
        {"Cancel ticked",
         Question::Buttons::OkCancel,
         true,
         [](Question* q) { press(q, wxID_CANCEL); },
         {false, true}},
        {"Escape (OkCancel)",
         Question::Buttons::OkCancel,
         false,
         [&](Question* q) { key(q, WXK_ESCAPE); },
         {false, false}},
        {"close box (OkCancel)",
         Question::Buttons::OkCancel,
         false,
         [](Question* q) { q->Close(); },
         {false, false}},
        {"Delete",
         Question::Buttons::DeleteCancel,
         false,
         [](Question* q) { press(q, wxID_YES); },
         {true, false}},
        {"Cancel (DeleteCancel)",
         Question::Buttons::DeleteCancel,
         false,
         [](Question* q) { press(q, wxID_NO); },
         {false, false}},
        {"Escape (DeleteCancel)",
         Question::Buttons::DeleteCancel,
         false,
         [&](Question* q) { key(q, WXK_ESCAPE); },
         {false, false}},
        {"close box (DeleteCancel)",
         Question::Buttons::DeleteCancel,
         false,
         [](Question* q) { q->Close(); },
         {false, false}},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        calls_.clear();
        const bool withBox = row.b == Question::Buttons::OkCancel;
        auto q = ask(row.b, withBox ? wxString("Don't show again") : wxString());
        ASSERT_TRUE(q);
        if (withBox) {
            ASSERT_NE(box(q), nullptr);
            EXPECT_EQ(box(q)->GetLabel(), "Don't show again");
            box(q)->SetValue(row.tick);
        }
        row.act(q);
        ASSERT_EQ(calls_.size(), 1u);
        EXPECT_EQ(calls_[0].yes, row.expect.yes);
        EXPECT_EQ(calls_[0].ticked, row.expect.ticked);
        EXPECT_FALSE(q->IsShown()) << "hidden before the callback";
        EXPECT_TRUE(gone(q));
        EXPECT_EQ(calls_.size(), 1u) << "nothing more after the window went";
    }
}

// DeleteCancel is the Yes / No pair with the labels Delete / Cancel; Cancel is the
// default.
TEST_F(QuestionTest, DeleteCancelShowsDeleteAndCancelAndCancelIsTheDefault) {
    auto q = ask(Question::Buttons::DeleteCancel, wxString());
    ASSERT_TRUE(q);
    EXPECT_EQ(button(q, wxID_YES)->GetLabel(), "Delete");
    EXPECT_EQ(button(q, wxID_NO)->GetLabel(), "Cancel");
    EXPECT_EQ(wxWindow::FindFocus(), static_cast<wxWindow*>(button(q, wxID_NO)));
    EXPECT_EQ(button(q, wxID_OK), nullptr);
    press(q, wxID_NO);
    EXPECT_TRUE(gone(q));
}

// askCustom is the Yes / No pair with the caller's labels; the default button is
// yes (Enter presses the default item), Escape and the close box answer no, the check box state reaches
// onAnswer, one call each; a question destroyed with its parent calls nothing.
TEST_F(QuestionTest, AskCustomCarriesTheCallersLabelsAndYesIsTheDefault) {
    const auto key = [](Question* q, int code) {
        wxKeyEvent e(wxEVT_CHAR_HOOK);
        e.m_keyCode = code;
        e.SetEventObject(q);
        q->GetEventHandler()->ProcessEvent(e);
    };
    struct Row {
        const char* name;
        bool tick;
        std::function<void(Question*)> act;
        Call expect;
    };
    const std::vector<Row> rows = {
        {"Use", false, [](Question* q) { press(q, wxID_YES); }, {true, false}},
        {"Use ticked", true, [](Question* q) { press(q, wxID_YES); }, {true, true}},
        {"Keep ticked", true, [](Question* q) { press(q, wxID_NO); }, {false, true}},
        {"Keep", false, [](Question* q) { press(q, wxID_NO); }, {false, false}},
        {"Escape", false, [&](Question* q) { key(q, WXK_ESCAPE); }, {false, false}},
        {"Escape ticked", true, [&](Question* q) { key(q, WXK_ESCAPE); }, {false, true}},
        {"close box", false, [](Question* q) { q->Close(); }, {false, false}},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        calls_.clear();
        auto q = Question::askCustom(parent_, look_, "Chart Grabber", "Most detail 1:1.5k needs a lot.",
                                     "Use 1:5.8k", "Keep 1:1.5k", "Don't show again",
                                     [this](bool yes, bool ticked) { calls_.push_back({yes, ticked}); });
        ASSERT_TRUE(q);
        EXPECT_EQ(button(q, wxID_YES)->GetLabel(), "Use 1:5.8k");
        EXPECT_EQ(button(q, wxID_NO)->GetLabel(), "Keep 1:1.5k");
        EXPECT_EQ(q->GetDefaultItem(), static_cast<wxWindow*>(button(q, wxID_YES))) << "Enter answers yes";
        EXPECT_EQ(wxWindow::FindFocus(), static_cast<wxWindow*>(button(q, wxID_YES)));
        ASSERT_NE(box(q), nullptr);
        box(q)->SetValue(row.tick);
        row.act(q);
        ASSERT_EQ(calls_.size(), 1u);
        EXPECT_EQ(calls_[0].yes, row.expect.yes);
        EXPECT_EQ(calls_[0].ticked, row.expect.ticked);
        EXPECT_TRUE(gone(q));
        EXPECT_EQ(calls_.size(), 1u);
    }
}

TEST_F(QuestionTest, AskCustomDestroyedWithItsParentCallsNothingAndFitsTheScreenInTouchMode) {
    ocpn_fake::touch = true;
    auto q = Question::askCustom(
        parent_, look_, "Chart Grabber",
        "Most detail 1:1.5k needs about 87 000 tiles for this area, about 1.7 GB to download. Detail finer "
        "than 1:5k is for a harbour: draw a small area around it for that, as a second chart.\n\nSuggested: "
        "a smaller area, or most detail 1:5.8k.",
        "Use 1:12.000.000", "Keep 1:12.000.000", "Don't show again",
        [this](bool yes, bool ticked) { calls_.push_back({yes, ticked}); });
    ASSERT_TRUE(q);
    EXPECT_LE(q->GetSize().x, 800);
    EXPECT_LE(q->GetSize().y, 480);
    EXPECT_GE(button(q, wxID_YES)->GetSize().y, look_.minControlHeight());
    EXPECT_GE(button(q, wxID_NO)->GetSize().y, look_.minControlHeight());
    EXPECT_GE(look_.minControlHeight(), 48);
    delete parent_;
    parent_ = nullptr;
    EXPECT_FALSE(q);
    runFor(60);
    EXPECT_TRUE(calls_.empty());
}

// askChoice: one radio button per choice, selection 0; OK gives the selected index,
// Cancel, Escape and the close box give -1; one call each; a question destroyed with its parent calls
// nothing.
TEST_F(QuestionTest, AskChoiceReturnsTheSelectedIndexOnOkAndMinusOneOtherwise) {
    struct Row {
        const char* name;
        int select; // -1: leave the default
        std::function<void(Question*)> act;
        int expect;
    };
    const auto key = [](Question* q, int code) {
        wxKeyEvent e(wxEVT_CHAR_HOOK);
        e.m_keyCode = code;
        e.SetEventObject(q);
        q->GetEventHandler()->ProcessEvent(e);
    };
    const std::vector<Row> rows = {
        {"OK, the default", -1, [](Question* q) { press(q, wxID_OK); }, 0},
        {"OK, the third", 2, [](Question* q) { press(q, wxID_OK); }, 2},
        {"OK, the last", 3, [](Question* q) { press(q, wxID_OK); }, 3},
        {"Cancel, the second selected", 1, [](Question* q) { press(q, wxID_CANCEL); }, -1},
        {"Escape, the second selected", 1, [&](Question* q) { key(q, WXK_ESCAPE); }, -1},
        {"close box, the second selected", 1, [](Question* q) { q->Close(); }, -1},
    };
    wxArrayString choices;
    for (const char* c : {"All", "1 month", "6 \xC3\xA9 months", "12 months"})
        choices.Add(wxString::FromUTF8(c));
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        std::vector<int> got;
        auto q =
            Question::askChoice(parent_, look_, "Update", "Download again the tiles older than:", choices,
                                [&](int c) { got.push_back(c); });
        ASSERT_TRUE(q);
        EXPECT_TRUE(q->IsShown());
        EXPECT_FALSE(q->IsModal());
        std::vector<wxRadioButton*> radios;
        for (wxWindow* w : q->GetChildren())
            if (auto* r = dynamic_cast<wxRadioButton*>(w))
                radios.push_back(r);
        ASSERT_EQ(radios.size(), 4u);
        EXPECT_EQ(radios[2]->GetLabel(), wxString::FromUTF8("6 \xC3\xA9 months"));
        EXPECT_TRUE(radios[0]->GetValue()) << "selection 0 by default";
        EXPECT_EQ(button(q, wxID_YES), nullptr);
        if (row.select >= 0)
            radios[row.select]->SetValue(true);
        row.act(q);
        ASSERT_EQ(got.size(), 1u);
        EXPECT_EQ(got[0], row.expect);
        EXPECT_FALSE(q->IsShown());
        EXPECT_TRUE(gone(q));
        EXPECT_EQ(got.size(), 1u);
    }
    std::vector<int> got;
    auto q = Question::askChoice(parent_, look_, "Update", "x", choices, [&](int c) { got.push_back(c); });
    delete parent_;
    parent_ = nullptr;
    EXPECT_FALSE(q);
    runFor(60);
    EXPECT_TRUE(got.empty());
}

TEST_F(QuestionTest, ASecondEventBeforeTheWindowGoesDeliversNothing) {
    LogCapture log;
    auto q = ask(Question::Buttons::YesNo, wxString());
    ASSERT_TRUE(q);
    Question* raw = q;
    press(raw, wxID_YES);
    press(raw, wxID_NO); // still alive until idle
    q->Close();
    ASSERT_EQ(calls_.size(), 1u);
    EXPECT_TRUE(calls_[0].yes);
    EXPECT_TRUE(log.lines.empty()) << "an empty answer function would be called and logged";
}

TEST_F(QuestionTest, DestroyingTheParentWithTheQuestionOpenCallsNothing) {
    auto q = ask(Question::Buttons::OkCancel, "Don't show again");
    ASSERT_TRUE(q);
    delete parent_; // what DeInit does to the main dialog
    parent_ = nullptr;
    EXPECT_FALSE(q) << "destroyed with its parent";
    runFor(60);
    EXPECT_TRUE(calls_.empty());
}

TEST_F(QuestionTest, TheDefaultIsNoForYesNoAndOkForOkCancelAndTouchGrowsTheButtons) {
    ocpn_fake::touch = true;
    ocpn_fake::scale = 1.0;
    auto yn = ask(Question::Buttons::YesNo, wxString());
    ASSERT_TRUE(yn);
    EXPECT_EQ(wxWindow::FindFocus(), static_cast<wxWindow*>(button(yn, wxID_NO)));
    EXPECT_GE(button(yn, wxID_YES)->GetSize().y, look_.minControlHeight());
    EXPECT_GE(button(yn, wxID_NO)->GetSize().y, 48);
    press(yn, wxID_NO);
    auto oc = ask(Question::Buttons::OkCancel, wxString());
    EXPECT_GE(button(oc, wxID_OK)->GetSize().y, 48);
    EXPECT_TRUE(button(oc, wxID_OK)->IsThisEnabled());
    press(oc, wxID_CANCEL);
    EXPECT_TRUE(gone(oc));
}
