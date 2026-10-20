// SPDX-License-Identifier: GPL-3.0-or-later
// The My charts page : one row per chart job directory, newest
// first, with Show / Resume as the primary button and a "More..." menu for the rest. GUI thread only.
#pragma once

#include "job_files.h"
#include "job_progress.h"
#include "question.h"

#include <wx/control.h>
#include <wx/datetime.h>
#include <wx/panel.h>
#include <wx/string.h>
#include <wx/timer.h>
#include <wx/weakref.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class AreaTool;
class ConfigStore;
class JobController;
class Look;
class wxButton;
class wxDC;
class wxGauge;
class wxMenu;
class wxScrolledWindow;
class wxSizer;
class wxStaticText;

// The frozen bar of a row that is not running : a track and a GREY fill up to `permille`.
// Painted from the Look (a disabled wxGauge draws an empty track on GTK); a read-only indicator, no focus.
class FrozenBar : public wxControl {
public:
    FrozenBar(wxWindow* parent, const Look& look, int permille, const wxString& name);
    bool AcceptsFocus() const override { return false; }
    void draw(wxDC& dc) const; // the whole bar into a DC of the control's size
    int permille() const { return permille_; }

private:
    const Look& look_;
    int permille_;
};

class MyChartsPanel : public wxPanel {
public:
    MyChartsPanel(wxWindow* parent, ConfigStore& config, const Look& look, AreaTool& area,
                  JobController& jobs);
    ~MyChartsPanel() override;

    // Re-reads loadCharts + unreadableChartIds and rebuilds the rows; buttons follow jobs.busy.
    void refresh();
    std::size_t rows() const { return charts_.size() + unreadable_.size(); }

    enum class Action { Show, Resume, TopUp, BuildFromSaved, Delete, ShowFile, Update, Stop, UpdateWeather };
    // What a row button or menu item does, for the chart (or unreadable directory) with this id.
    void act(Action action, const std::string& id);
    // The questions are modeless : each seam asks and returns at once; `answer` is called once
    // later and never when the panel is destroyed first. The defaults are Question windows (Yes / No, No
    // default).
    std::function<void(const wxString& question, std::function<void(bool yes)> answer)> confirmDelete;

    // Update... : `answer(choice)`: 0..3 = updateChoiceText(i), -1 = Cancel (default: Question::
    // askChoice, selection 0); the over-budget question is a Yes / No Question; the clock is now.
    std::function<void(const wxString& question, std::function<void(int choice)> answer)> chooseUpdate;
    std::function<void(const wxString& question, std::function<void(bool yes)> answer)> confirmBudget;
    std::function<wxDateTime()> now;

    // The items of the More... menu of this chart (item id = 1 + Action); disabled while a job runs.
    void fillMenu(wxMenu& menu, const cg::ChartRecord& chart) const;

    // The live row : sets its gauge, words and Stop button from jobs.live; nothing when
    // the live row is not built or live is nullopt or names another chart. Never rebuilds a row.
    void updateLiveRow();
    // The 1 s timer's handler (public for the tests): nothing while closing or not on screen, else
    // updateLiveRow.
    void onLiveTick();

    // Called after a Delete removed a row, so the owner can update the page button count.
    std::function<void()> onDeleted;

private:
    void addRow(const cg::ChartRecord* chart, const std::string& id);
    void showMenu(wxWindow* anchor, const cg::ChartRecord& chart);
    void showFile(const cg::ChartRecord& chart);
    void deleteChart(const std::string& id);
    void updateChart(const std::string& id);
    void updateWeather(const std::string& id);
    wxString dateColumn(const cg::ChartRecord& chart) const;
    void startUpdate(const std::string& id, int choice);
    // True (and the open question raised) when a question of this panel is open: a second request raises it.
    bool raiseOpenQuestion();
    // Called by every answer: false when the panel is closing; else enables the panel again.
    bool answered();

    // The live LiveProgress when it names chart `id`.
    std::optional<cg::LiveProgress> liveOf(const std::string& id) const;
    void colourGauges();

    ConfigStore& config_;
    const Look& look_;
    AreaTool& area_;
    JobController& jobs_;
    int lookToken_ = 0;
    bool closing_ = false;
    wxWeakRef<Question> question_;
    wxTimer live_;
    wxGauge* liveGauge_ = nullptr; // the live row's widgets; nullptr before every rebuild
    wxStaticText* liveWords_ = nullptr;
    wxButton* liveButton_ = nullptr;
    std::string liveId_;
    wxStaticText* oneJob_ = nullptr; // the line above the list while a job runs
    wxScrolledWindow* list_ = nullptr;
    wxSizer* rowsSizer_ = nullptr;
    std::vector<cg::ChartRecord> charts_;
    std::vector<std::string> unreadable_;
};
