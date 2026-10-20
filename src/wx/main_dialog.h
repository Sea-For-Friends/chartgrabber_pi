// SPDX-License-Identifier: GPL-3.0-or-later
// The main dialog of Chart Grabber : modeless, not
// resizable, owned by the plugin (deleted only in DeInit). Pages New chart / My charts. GUI thread only.
#pragma once

#include "progress_strip.h"
#include "types.h"

#include <wx/dialog.h>
#include <wx/simplebook.h>
#include <wx/tglbtn.h>
#include <wx/weakref.h>

#include <optional>

class AreaTool;
class ConfigStore;
class JobController;
class Look;
class NewChartPanel;

class MainDialog : public wxDialog {
public:
    MainDialog(wxWindow* parent, ConfigStore& config, const Look& look, AreaTool& area, JobController& jobs);
    ~MainDialog() override; // abandons a drawing, deletes the pages first, then the hint strip

    void showNewChart(cg::AreaKind kind, std::optional<cg::BBox> box); // a running drawing is abandoned
    void present();                 // toolbar on an existing dialog
    void drawArea();                //

    void showMyCharts();            // calls it
    void chartsChanged();           // JobController::onChartsChanged relay
    void busyChanged(bool busy);    // JobController::onBusyChanged relay
    void capHeight(int decoration); // touch: the New chart body cap for this decoration height (px)
    void settingsSaved();           // OK of the strip's Settings window: the page reads its sources again

private:
    int measuredDecoration() const;
    void hideDialog();
    void selectPage(int page); // 0 New chart, 1 My charts
    void endDrawing();
    void showHint(const wxString& text);
    wxWindow* makeMyChartsPage(wxWindow* parent); // an empty panel; replaces the body
    void refreshMyCharts();                       // empty; replaces the body

    ConfigStore& config_;
    const Look& look_;
    AreaTool& area_;
    JobController& jobs_;
    int lookToken_ = 0;
    int tabsHeight_ = 0;
    int cap_ = 0;
    bool closing_ = false;
    wxToggleButton* newChartButton_ = nullptr;
    wxToggleButton* myChartsButton_ = nullptr;
    wxSimplebook* pages_ = nullptr;
    NewChartPanel* newChart_ = nullptr;
    wxWindow* myChartsPage_ = nullptr;
    wxWeakRef<ProgressStrip> hint_; // Role::Hint on the canvas, created on the first drawArea
};
