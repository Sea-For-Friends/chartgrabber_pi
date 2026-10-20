// SPDX-License-Identifier: GPL-3.0-or-later
#include "my_charts_panel.h"

#include "config_store.h"
#include "guard.h"
#include "job_controller.h"
#include "look.h"
#include "messages.h"
#include "plugin_log.h"
#include "wx_util.h"

#include <wx/button.h>
#include <wx/dcclient.h>
#include <wx/filename.h>
#include <wx/gauge.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include <algorithm>
#include <cstdlib>
#include <utility>

#include <ocpn_plugin.h>

namespace {

wxButton* makeButton(wxWindow* parent, const wxString& label, int minHeight, bool enabled) {
    auto* b = new wxButton(parent, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    if (minHeight > 0)
        b->SetMinSize(wxSize(minHeight, minHeight)); // touch: square at least
    b->Enable(enabled);
    return b;
}

// -1 = pulse (the size is not known yet), else 0..1000.
void setGauge(wxGauge* g, int permille) {
    if (permille < 0)
        g->Pulse();
    else
        g->SetValue(std::min(permille, 1000));
}

int luminance(const wxColour& c) {
    return (c.Red() * 299 + c.Green() * 587 + c.Blue() * 114) / 1000;
}

} // namespace

FrozenBar::FrozenBar(wxWindow* parent, const Look& look, int permille, const wxString& name)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxSize(parent->FromDIP(180), parent->FromDIP(16)),
                wxBORDER_NONE),
      look_(look), permille_(std::min(std::max(permille, 0), 1000)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetName(name); // the row's words are the primary text; this is the read-only name of the indicator
    Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxPaintDC dc(this);
        draw(dc);
    });
}

// Track: the control colour (dusk / night DILG1) or the system window colour (day). Fill: the grey of
// {80, 128, 176} farthest from the luminance of both the track and the row body, so it stands out on a light
// and on a dark body, and a grey (no hue) never reads as the live blue.
void FrozenBar::draw(wxDC& dc) const {
    const wxColour body = GetParent()->GetBackgroundColour();
    wxColour track = look_.controlBackground();
    if (!track.IsOk())
        track = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);
    int grey = 128, best = -1;
    for (int g : {80, 128, 176}) {
        const int far = std::min(std::abs(g - luminance(track)), std::abs(g - luminance(body)));
        if (far > best) {
            best = far;
            grey = g;
        }
    }
    const wxSize size = GetClientSize();
    dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW)));
    dc.SetBrush(wxBrush(track));
    dc.DrawRectangle(0, 0, size.x, size.y);
    const int end = std::min(size.x * permille_ / 1000, size.x - 1);
    if (end > 1) {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour(grey, grey, grey)));
        dc.DrawRectangle(1, 1, end - 1, size.y - 2);
    }
}

MyChartsPanel::MyChartsPanel(wxWindow* parent, ConfigStore& config, const Look& look, AreaTool& area,
                             JobController& jobs)
    : wxPanel(parent), config_(config), look_(look), area_(area), jobs_(jobs), live_(this) {
    chooseUpdate = [this](const wxString& question, std::function<void(int)> answer) {
        wxArrayString choices;
        for (int i = 0; i < 4; ++i)
            choices.Add(updateChoiceText(i));
        question_ = Question::askChoice(wxGetTopLevelParent(this), look_, updateTitleText(), question,
                                        choices, std::move(answer));
    };
    confirmBudget = [this](const wxString& question, std::function<void(bool)> answer) {
        question_ = Question::ask(wxGetTopLevelParent(this), look_, _("Chart Grabber"), question,
                                  Question::Buttons::YesNo, wxString(),
                                  [answer = std::move(answer)](bool yes, bool) { answer(yes); });
    };
    now = [] { return wxDateTime::Now(); };
    confirmDelete = [this](const wxString& question, std::function<void(bool)> answer) {
        question_ = Question::ask(wxGetTopLevelParent(this), look_, _("Chart Grabber"), question,
                                  Question::Buttons::YesNo, wxString(),
                                  [answer = std::move(answer)](bool yes, bool) { answer(yes); });
    };
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { cg::guarded("my charts live", [&] { onLiveTick(); }); });
    oneJob_ = new wxStaticText(this, wxID_ANY, oneJobAtATimeText());
    oneJob_->Hide();
    list_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    list_->SetScrollRate(0, 12);
    list_->SetMinSize(wxSize(520, 100)); // the list scrolls; the dialog keeps its size (800 x 480 screens)
    rowsSizer_ = new wxBoxSizer(wxVERTICAL);
    list_->SetSizer(rowsSizer_);
    auto* close = new wxButton(this, wxID_CLOSE, _("Close"));
    if (const int minHeight = look.minControlHeight(); minHeight > 0)
        close->SetMinSize(wxSize(-1, minHeight));
    auto* column = new wxBoxSizer(wxVERTICAL);
    column->Add(oneJob_, 0, wxLEFT | wxRIGHT | wxTOP, 6);
    column->Add(list_, 1, wxEXPAND | wxALL, 6);
    column->Add(close, 0, wxALL, 6);
    SetSizer(column);

    // The page is shown by the dialog's page switch: read the directory again then.
    Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
        cg::guarded("my charts show", [&] {
            if (!closing_ && e.IsShown())
                refresh();
            e.Skip();
        });
    });
    look.apply(this);
    lookToken_ = look.subscribe([this] {
        if (!closing_) {
            look_.apply(this);
            colourGauges();
            // the dialog applies the look to all its descendants after this callback: colour once more then
            CallAfter([this] {
                if (!closing_)
                    colourGauges();
            });
        }
    });
    refresh();
}

MyChartsPanel::~MyChartsPanel() {
    closing_ = true;
    live_.Stop();
    delete question_.get(); // its callback holds this panel; immediate, not Destroy
    look_.unsubscribe(lookToken_);
}

void MyChartsPanel::refresh() {
    // A refresh may run inside the handler of a button of the old rows: Destroy defers the deletion.
    rowsSizer_->Clear(false);
    liveGauge_ = nullptr; // the old rows are destroyed below
    liveWords_ = nullptr;
    liveButton_ = nullptr;
    liveId_.clear();
    const wxWindowList old = list_->GetChildren();
    for (wxWindow* w : old) {
        w->Hide();
        w->Destroy();
    }
    charts_ = config_.loadCharts();
    unreadable_ = config_.unreadableChartIds();
    for (const cg::ChartRecord& c : charts_)
        addRow(&c, c.id);
    for (const std::string& id : unreadable_)
        addRow(nullptr, id);
    look_.apply(this);
    colourGauges();
    const bool busy = jobs_.busy();
    if (busy && !live_.IsRunning())
        live_.Start(1000);
    else if (!busy)
        live_.Stop();
    oneJob_->Show(busy);
    list_->FitInside();
    list_->Layout();
    Layout();
}

// look.apply resets the gauges' colours; the strip's rule gives them the control colour.
void MyChartsPanel::colourGauges() {
    std::vector<wxWindow*> todo = {list_};
    while (!todo.empty()) {
        wxWindow* w = todo.back();
        todo.pop_back();
        if (dynamic_cast<wxGauge*>(w))
            w->SetBackgroundColour(look_.controlBackground());
        else if (dynamic_cast<FrozenBar*>(w))
            w->Refresh(); // painted from the look: a scheme switch repaints it
        for (wxWindow* child : w->GetChildren())
            todo.push_back(child);
    }
}

std::optional<cg::LiveProgress> MyChartsPanel::liveOf(const std::string& id) const {
    auto p = jobs_.live();
    if (p && p->chartId != id)
        return std::nullopt;
    return p;
}

void MyChartsPanel::updateLiveRow() {
    if (closing_ || !liveGauge_)
        return;
    const auto p = liveOf(liveId_);
    if (!p)
        return;
    setGauge(liveGauge_, p->permille);
    liveWords_->SetLabel(liveRowText(*p));
    const bool stopping = p->stage == cg::LiveStage::Stopping;
    const wxString label = stopping ? stoppingButtonText() : stopButtonText();
    liveButton_->Enable(!stopping);
    if (liveButton_->GetLabel() != label) {
        liveButton_->SetLabel(label);
        liveButton_->GetParent()->Layout();
        list_->Layout();
    }
}

void MyChartsPanel::onLiveTick() {
    if (!closing_ && IsShownOnScreen())
        updateLiveRow();
}

// The date column: the creation date, or for a weather chart the picture's time and age.
wxString MyChartsPanel::dateColumn(const cg::ChartRecord& chart) const {
    const auto taken = chart.weather ? cg::snapshotEpoch(chart.snapshot) : std::nullopt;
    if (!taken)
        return cg::fromUtf8(chart.created.substr(0, 10));
    const wxString age = snapshotAgeText(static_cast<std::int64_t>(now().GetTicks()) - *taken);
    return cg::fromUtf8(cg::snapshotTimeText(chart.snapshot)) + (age.empty() ? wxString() : " (" + age + ")");
}

void MyChartsPanel::addRow(const cg::ChartRecord* chart, const std::string& id) {
    const int minHeight = look_.minControlHeight();
    const bool busy = jobs_.busy();
    auto* row = new wxPanel(list_);
    auto* name = new wxStaticText(row, wxID_ANY, chart ? cg::fromUtf8(chart->name) : cg::fromUtf8(id),
                                  wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    auto* left = new wxBoxSizer(wxVERTICAL);
    left->Add(name, 0, wxEXPAND);
    if (chart) {
        auto* details = new wxBoxSizer(wxHORIZONTAL);
        details->Add(new wxStaticText(row, wxID_ANY, "z" + cg::fromUtf8(cg::formatZoomList(chart->zooms))), 0,
                     wxRIGHT, 12);
        details->Add(new wxStaticText(row, wxID_ANY, formatBytes(chart->bytes)), 0, wxRIGHT, 12);
        details->Add(new wxStaticText(row, wxID_ANY, dateColumn(*chart)), 0);
        left->Add(details, 0);
    }
    const auto live = chart && chart->state == cg::ChartState::Running ? liveOf(id) : std::nullopt;
    // Unfinished rows have a third line (gauge + words, ); Complete and unreadable rows keep
    // their state text right of the details.
    const bool unfinished = chart && chart->state != cg::ChartState::Complete;
    auto* line = new wxBoxSizer(wxHORIZONTAL);
    if (unfinished) {
        auto* third = new wxBoxSizer(wxHORIZONTAL);
        const auto frozen = cg::frozenPermille(*chart);
        wxString text = live ? liveRowText(*live) : frozenRowText(*chart);
        if (live || frozen) {
            wxWindow* bar;
            if (live) {
                auto* gauge = new wxGauge(row, wxID_ANY, 1000, wxDefaultPosition, wxSize(FromDIP(180), -1));
                setGauge(gauge, live->permille);
                liveGauge_ = gauge;
                bar = gauge;
            } else {
                bar = new FrozenBar(row, look_, *frozen, text);
            }
            third->Add(bar, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        }
        auto* words = new wxStaticText(row, wxID_ANY, text, wxDefaultPosition, wxDefaultSize,
                                       wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
        words->SetMinSize(wxSize(FromDIP(60), -1)); // the sizer, not the text, gives it its width
        third->Add(words, 1, wxALIGN_CENTER_VERTICAL);
        left->Add(third, 0, wxEXPAND | wxTOP, 2);
        if (live)
            liveWords_ = words;
    }
    if (chart && chart->weather && !chart->credit.empty()) { // BELOW the third line
        const wxString credit = creditText(chart->credit);
        auto* line3 = new wxStaticText(row, wxID_ANY, credit, wxDefaultPosition, wxDefaultSize,
                                       wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
        line3->SetMinSize(wxSize(FromDIP(60), -1));
        line3->SetToolTip(credit);
        left->Add(line3, 0, wxEXPAND | wxTOP, 2);
    }
    line->Add(left, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    if (!unfinished)
        line->Add(new wxStaticText(row, wxID_ANY, chart ? chartStateText(*chart) : unreadableStateText()), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    // The primary button: Show (Complete, Missing), Resume (Interrupted), Delete (unreadable); then
    // "More...".
    const auto primary = [&](const wxString& label, Action a, bool enabled) -> wxButton* {
        wxButton* b = makeButton(row, label, minHeight, enabled);
        b->Bind(wxEVT_BUTTON, [this, a, id](wxCommandEvent&) {
            cg::guarded("my charts button", [&] {
                if (!closing_)
                    act(a, id);
            });
        });
        line->Add(b, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        return b;
    };
    if (!chart) {
        primary(_("Delete"), Action::Delete, !busy);
    } else {
        if (chart->state == cg::ChartState::Complete || chart->state == cg::ChartState::Missing)
            primary(showButtonText(), Action::Show, true); // Show stays enabled while a job runs
        else if (chart->state == cg::ChartState::Interrupted)
            primary(resumeButtonText(), Action::Resume, !busy);
        else if (live) { // Stop is the running row's primary button
            const bool stopping = live->stage == cg::LiveStage::Stopping;
            liveButton_ =
                primary(stopping ? stoppingButtonText() : stopButtonText(), Action::Stop, !stopping);
            liveId_ = id;
        }
        wxButton* more = makeButton(row, _("More..."), minHeight, true);
        more->Bind(wxEVT_BUTTON, [this, more, id](wxCommandEvent&) {
            cg::guarded("my charts more", [&] {
                if (closing_)
                    return;
                const auto it = std::ranges::find(charts_, id, &cg::ChartRecord::id);
                if (it != charts_.end())
                    showMenu(more, *it);
            });
        });
        line->Add(more, 0, wxALIGN_CENTER_VERTICAL);
    }
    row->SetSizer(line);
    rowsSizer_->Add(row, 0, wxEXPAND | wxALL, 4);
    rowsSizer_->Add(new wxStaticLine(list_), 0, wxEXPAND);
}

void MyChartsPanel::fillMenu(wxMenu& menu, const cg::ChartRecord& chart) const {
    const bool busy = jobs_.busy();
    const bool downloadable =
        chart.state == cg::ChartState::Complete || chart.state == cg::ChartState::Missing;
    const bool finished = chart.state != cg::ChartState::Running;
    const bool live = chart.state == cg::ChartState::Running && liveOf(chart.id);
    // Show, Show in folder and the live row's Show stay enabled while a job runs; the rest follows !busy.
    const auto add = [&](Action a, const wxString& label, bool enabled) {
        menu.Append(1 + static_cast<int>(a), label)->Enable(enabled);
    };
    if (live)
        add(Action::Show, showButtonText(), true);
    if (chart.weather) { // a picture is taken again, never topped up, updated by date or rebuilt
        if (downloadable || chart.state == cg::ChartState::Failed)
            add(Action::UpdateWeather, updateWeatherMenuText(), !busy);
        if (finished)
            add(Action::Delete, _("Delete"), !busy);
    } else if (downloadable) {
        add(Action::TopUp, _("Top up"), !busy);
        add(Action::Update, updateMenuText(), !busy);
    }
    if (finished && !chart.weather) {
        add(Action::BuildFromSaved, buildFromSavedButtonText(), !busy);
        add(Action::Delete, _("Delete"), !busy);
    }
    add(Action::ShowFile, _("Show in folder"), true);
}

void MyChartsPanel::showMenu(wxWindow* anchor, const cg::ChartRecord& chart) {
    wxMenu menu;
    fillMenu(menu, chart);
    // The popup runs a nested event loop: a job ending meanwhile refreshes the panel and reassigns charts_,
    // so `chart` dangles after it; only this copy of the id is used afterwards.
    const std::string id = chart.id;
    // GetPopupMenuSelectionFromUser returns after the menu closed: the action then runs outside the menu.
    const int chosen = anchor->GetPopupMenuSelectionFromUser(menu, 0, anchor->GetSize().y);
    if (chosen == wxID_NONE || closing_)
        return;
    act(static_cast<Action>(chosen - 1), id);
}

bool MyChartsPanel::raiseOpenQuestion() {
    if (!question_ || !question_->IsShown())
        return false;
    question_->Raise();
    return true;
}

bool MyChartsPanel::answered() {
    if (closing_)
        return false;
    Enable(true);
    return true;
}

void MyChartsPanel::act(Action action, const std::string& id) {
    const auto it = std::ranges::find(charts_, id, &cg::ChartRecord::id);
    if (it == charts_.end()) {
        // An unreadable directory has Delete only (caller: its row button).
        if (action == Action::Delete && std::ranges::find(unreadable_, id) != unreadable_.end() &&
            !jobs_.busy()) {
            if (raiseOpenQuestion())
                return;
            Enable(false);
            confirmDelete(deleteChartQuestion(cg::fromUtf8(id)), [this, id](bool yes) {
                if (!answered() || !yes || std::ranges::find(unreadable_, id) == unreadable_.end() ||
                    jobs_.busy())
                    return;
                config_.deleteChartDir(id);
                refresh();
                if (onDeleted)
                    onDeleted();
            });
        }
        return;
    }
    const cg::ChartRecord chart = *it; // refresh below replaces charts_
    if (action == Action::Show) {
        showOnChart(chart.bbox, area_);
        return;
    }
    if (action == Action::ShowFile) { // opening a folder cannot disturb the job
        showFile(chart);
        return;
    }
    if (action == Action::Stop) { // the running row's button (the job of jobs.live only)
        if (liveOf(chart.id)) {
            jobs_.stop();
            updateLiveRow();
        }
        return;
    }
    if (jobs_.busy())
        return; // every other action is disabled while a job runs
    switch (action) {
    case Action::Resume:
        jobs_.start(chart, JobController::resumeKind(chart), true);
        break;
    case Action::Update:
        if (chart.state == cg::ChartState::Complete || chart.state == cg::ChartState::Missing)
            updateChart(chart.id); // the menu offers it for these rows only
        else
            cg::logLine("Update of a chart that is not complete");
        break;
    case Action::UpdateWeather:
        if (chart.weather &&
            (chart.state == cg::ChartState::Complete || chart.state == cg::ChartState::Missing ||
             chart.state == cg::ChartState::Failed))
            updateWeather(chart.id);
        else
            cg::logLine("Update to latest weather of a row that does not offer it");
        break;
    case Action::TopUp:
        jobs_.start(chart, cg::RunKind::Download, true);
        break;
    case Action::BuildFromSaved:
        jobs_.start(chart, cg::RunKind::BuildFromSaved, true);
        break;
    case Action::Delete:
        deleteChart(chart.id);
        break;
    case Action::ShowFile:
    case Action::Stop:
    case Action::Show:
        break;
    }
}

// The questions below capture the chart ID, never a reference into charts_ (it is replaced by refresh).
void MyChartsPanel::updateChart(const std::string& id) {
    if (raiseOpenQuestion())
        return;
    const auto it = std::ranges::find(charts_, id, &cg::ChartRecord::id);
    if (it == charts_.end())
        return;
    Enable(false);
    chooseUpdate(updateQuestion(cg::fromUtf8(it->name)), [this, id](int choice) {
        if (!answered() || choice < 0 || choice > 3)
            return;
        startUpdate(id, choice);
    });
}

void MyChartsPanel::startUpdate(const std::string& id, int choice) {
    static constexpr int kMonthsBack[] = {0, 1, 6, 12};
    const auto it = std::ranges::find(charts_, id, &cg::ChartRecord::id);
    if (it == charts_.end() || jobs_.busy())
        return;
    const auto start = [this, id, choice] { // the budget answer, or the choice itself
        const auto at = std::ranges::find(charts_, id, &cg::ChartRecord::id);
        if (at == charts_.end() || jobs_.busy())
            return;
        cg::ChartRecord chart = *at;
        const wxDateTime t = now();
        chart.updateBefore = cg::olderThanDate(t.GetYear(), t.GetMonth() + 1, t.GetDay(), t.GetHour(),
                                               t.GetMinute(), t.GetSecond(), kMonthsBack[choice]);
        jobs_.start(chart, cg::RunKind::Update, true); // saves the record with the date
    };
    const cg::Settings settings = config_.loadSettings();
    // The chart's size bounds the download; the CLI's estimate does not model replacing.
    if (settings.slowConnection && it->bytes > static_cast<std::int64_t>(settings.dataBudgetMb) * 1'000'000) {
        Enable(false);
        confirmBudget(overBudgetQuestion(it->bytes, settings.dataBudgetMb), [this, start](bool yes) {
            if (answered() && yes)
                start();
        });
        return;
    }
    start();
}

// Kind Download passes no --replace; the budget question as startUpdate's.
void MyChartsPanel::updateWeather(const std::string& id) {
    const auto it = std::ranges::find(charts_, id, &cg::ChartRecord::id);
    if (it == charts_.end() || jobs_.busy())
        return;
    const auto start = [this, id] {
        const auto at = std::ranges::find(charts_, id, &cg::ChartRecord::id);
        if (at != charts_.end() && !jobs_.busy())
            jobs_.start(*at, cg::RunKind::Download, true);
    };
    const cg::Settings settings = config_.loadSettings();
    if (settings.slowConnection && it->bytes > static_cast<std::int64_t>(settings.dataBudgetMb) * 1'000'000) {
        if (raiseOpenQuestion())
            return;
        Enable(false);
        confirmBudget(overBudgetQuestion(it->bytes, settings.dataBudgetMb), [this, start](bool yes) {
            if (answered() && yes)
                start();
        });
        return;
    }
    start();
}

void MyChartsPanel::showFile(const cg::ChartRecord& chart) {
    const wxString folder = chart.files.empty() ? cg::fromUtf8(config_.chartDir(config_.loadSettings()))
                                                : wxFileName(cg::fromUtf8(chart.files.front())).GetPath();
    if (!wxLaunchDefaultApplication(folder))
        cg::logWarning(wxString::Format("cannot open %s", folder));
}

void MyChartsPanel::deleteChart(const std::string& id) {
    if (raiseOpenQuestion())
        return;
    const auto it = std::ranges::find(charts_, id, &cg::ChartRecord::id);
    if (it == charts_.end())
        return;
    Enable(false);
    confirmDelete(deleteChartQuestion(cg::fromUtf8(it->name)), [this, id](bool yes) {
        if (!answered() || !yes || jobs_.busy())
            return;
        const auto at = std::ranges::find(charts_, id, &cg::ChartRecord::id);
        if (at == charts_.end())
            return;
        const cg::ChartRecord chart = *at; // refresh below replaces charts_
        for (const std::string& f : chart.files) {
            const wxString p = cg::fromUtf8(f);
            if (wxFileExists(p))
                wxRemoveFile(p);
        }
        config_.deleteChartDir(chart.id);
        // OpenCPN's per-file remove call hangs 5.14+ : one rescan drops the gone files instead.
        UpdateChartDBInplace(GetChartDBDirArrayString(), false, false);
        refresh();
        if (onDeleted)
            onDeleted();
    });
}
