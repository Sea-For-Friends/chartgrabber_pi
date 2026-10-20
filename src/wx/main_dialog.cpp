// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_dialog.h"

#include "area_tool.h"
#include "config_store.h"
#include "guard.h"
#include "job_controller.h"
#include "look.h"
#include "messages.h"
#include "my_charts_panel.h"
#include "new_chart_panel.h"
#include "progress_strip.h"

#include <wx/intl.h>
#include <wx/panel.h>
#include <wx/sizer.h>

#include <algorithm>

#include <ocpn_plugin.h>

MainDialog::MainDialog(wxWindow* parent, ConfigStore& config, const Look& look, AreaTool& area,
                       JobController& jobs)
    : wxDialog(parent, wxID_ANY, _("Chart Grabber"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE),
      config_(config), look_(look), area_(area), jobs_(jobs) {
    newChartButton_ = new wxToggleButton(this, wxID_ANY, _("New chart"));
    myChartsButton_ = new wxToggleButton(this, wxID_ANY, _("My charts"));
    pages_ = new wxSimplebook(this);
    newChart_ = new NewChartPanel(pages_, config, look, area, jobs);
    pages_->AddPage(newChart_, _("New chart"), true);
    myChartsPage_ = makeMyChartsPage(pages_);
    pages_->AddPage(myChartsPage_, _("My charts"));
    newChart_->onDraw = [this] { drawArea(); };
    newChart_->onStarted = [this] { hideDialog(); };

    if (const int minHeight = look.minControlHeight(); minHeight > 0) {
        newChartButton_->SetMinSize(wxSize(-1, minHeight));
        myChartsButton_->SetMinSize(wxSize(-1, minHeight));
    }
    auto* tabs = new wxBoxSizer(wxHORIZONTAL);
    tabs->Add(newChartButton_, 0, wxRIGHT, 6);
    tabs->Add(myChartsButton_, 0);
    auto* column = new wxBoxSizer(wxVERTICAL);
    column->Add(tabs, 0, wxALL, 8);
    column->Add(pages_, 1, wxEXPAND);
    SetSizer(column);
    // Touch mode only : the whole dialog, the window manager's decoration included, fits 800 x 480;
    // what the tab row takes, the New chart page does not have; its rows scroll instead.
    if (look.minControlHeight() > 0) {
        tabsHeight_ = tabs->CalcMin().y;
        const int decoration = measuredDecoration();
        capHeight(decoration > 0 ? decoration : FromDIP(40));
        Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { // GTK: the frame extents arrive after the window is mapped
            e.Skip();
            cg::guarded("dialog size", [&] {
                if (const int d = measuredDecoration(); d > 0 && !closing_)
                    capHeight(d);
            });
        });
    }

    newChartButton_->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        cg::guarded("new chart tab", [&] {
            if (!closing_)
                selectPage(0);
        });
    });
    myChartsButton_->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        cg::guarded("my charts tab", [&] {
            if (!closing_)
                selectPage(1);
        });
    });
    Bind(
        wxEVT_BUTTON,
        [this](wxCommandEvent&) {
            cg::guarded("close button", [&] {
                if (!closing_)
                    hideDialog();
            });
        },
        wxID_CLOSE);
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        cg::guarded("dialog key", [&] {
            if (e.GetKeyCode() != WXK_ESCAPE) {
                e.Skip();
                return;
            }
            hideDialog();
        });
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
        cg::guarded("dialog close", [&] { hideDialog(); });
    }); // no Skip, no Veto: the plugin deletes us

    look.apply(this);
    lookToken_ = look.subscribe([this] {
        if (!closing_)
            look_.apply(this);
    });
    selectPage(0);
    CentreOnParent();
}

// Title bar + frame as the toolkit reports it (GetSize minus GetClientSize); 0 while unknown: GTK knows the
// frame extents only after the window manager has mapped the window. Until then the constructor uses an
// allowance of 40 DIP (a title bar and a frame of a usual window manager; xfwm4 in the VM: see the record),
// and the size event after the first Show replaces it with the measured value.
int MainDialog::measuredDecoration() const {
    return std::max(GetSize().y - GetClientSize().y, 0);
}

void MainDialog::capHeight(int decoration) {
    const int cap = FromDIP(480) - tabsHeight_ - 16 - decoration;
    if (cap == cap_)
        return;
    cap_ = cap;
    newChart_->limitHeight(cap);
}

MainDialog::~MainDialog() {
    closing_ = true;
    if (area_.drawing())
        area_.abandon(); // no done call: its owner is going away
    area_.onAreaError = nullptr;
    area_.setShownArea(std::nullopt);
    pages_->DeleteAllPages(); // runs ~NewChartPanel, which may pump events
    newChart_ = nullptr;
    myChartsPage_ = nullptr;
    if (hint_)
        hint_->Destroy();
    look_.unsubscribe(lookToken_);
}

wxWindow* MainDialog::makeMyChartsPage(wxWindow* parent) {
    auto* panel = new MyChartsPanel(parent, config_, look_, area_, jobs_);
    myChartsButton_->SetLabel(myChartsButtonText(panel->rows()));
    panel->onDeleted = [this, panel] {
        myChartsButton_->SetLabel(myChartsButtonText(panel->rows()));
        Layout();
    };
    return panel;
}

void MainDialog::refreshMyCharts() {
    auto* panel = static_cast<MyChartsPanel*>(myChartsPage_); // made by makeMyChartsPage
    panel->refresh();
    myChartsButton_->SetLabel(myChartsButtonText(panel->rows()));
    Layout();
}

void MainDialog::hideDialog() {
    if (closing_)
        return;
    newChart_->outlineArea(false);
    Hide();
}

void MainDialog::selectPage(int page) {
    newChartButton_->SetValue(page == 0);
    myChartsButton_->SetValue(page == 1);
    pages_->ChangeSelection(page);
    newChart_->outlineArea(page == 0);
    Layout();
    Fit();
}

void MainDialog::endDrawing() {
    if (hint_)
        hint_->Hide();
    area_.onAreaError = nullptr;
}

void MainDialog::showHint(const wxString& text) {
    hint_->showResult(text, wxString(), {{uiText(UiText::Cancel), [this] { area_.cancel(); }}});
}

void MainDialog::showNewChart(cg::AreaKind kind, std::optional<cg::BBox> box) {
    if (closing_)
        return;
    if (area_.drawing()) {
        area_.abandon();
        endDrawing();
    }
    selectPage(0);
    newChart_->setArea(kind, box);
    Show();
    Raise();
}

void MainDialog::settingsSaved() {
    if (newChart_)
        newChart_->reloadSources();
}

void MainDialog::present() {
    if (closing_)
        return;
    if (area_.drawing()) {
        area_.cancel(); // its done shows the dialog
        return;
    }
    if (IsShown()) {
        Raise();
        return;
    }
    if (pages_->GetSelection() == 0)
        newChart_->refreshArea();
    else
        refreshMyCharts();
    Show();
    Raise();
}

void MainDialog::drawArea() {
    if (closing_)
        return;
    if (area_.drawing())
        area_.abandon();
    Hide();
    if (!hint_)
        hint_ = new ProgressStrip(GetOCPNCanvasWindow(), look_, ProgressStrip::Role::Hint, nullptr,
                                  config_.loadSettings().stripOnChart ? ProgressStrip::Host::Canvas
                                                                      : ProgressStrip::Host::Window);
    showHint(uiText(UiText::DrawHint));
    area_.onAreaError = [this](cg::AreaError e) {
        if (!closing_)
            showHint(areaErrorText(e) + "\n" + uiText(UiText::DrawHint));
    };
    area_.startDrawing([this](std::optional<cg::BBox> box) {
        if (closing_)
            return;
        endDrawing();
        if (box) {
            showNewChart(cg::AreaKind::Rectangle, box);
            return;
        }
        selectPage(0); // the page never changed its area: it shows the one it had
        Show();
        Raise();
        newChart_->outlineArea(true);
    });
}

void MainDialog::showMyCharts() {
    if (closing_)
        return;
    selectPage(1);
    refreshMyCharts();
    Show();
    Raise();
}

void MainDialog::chartsChanged() {
    if (!closing_)
        refreshMyCharts();
}

void MainDialog::busyChanged(bool busy) {
    if (closing_)
        return;
    newChart_->setBusy(busy);
    refreshMyCharts();
}
