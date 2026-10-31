// SPDX-License-Identifier: GPL-3.0-or-later
// One-row panel on the primary chart canvas: job progress, results and hints.
// A child of the canvas window, draggable by its body; GUI thread only.
#pragma once

#include "strip_place.h"

#include <wx/button.h>
#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/weakref.h>

#include <functional>
#include <utility>
#include <vector>

class ConfigStore;
class Look;

class ProgressStrip : public wxPanel {
public:
    enum class Role { Job, Hint }; // Job: draggable, remembered in `store`; Hint: top-left, fixed
    // `canvas` = GetOCPNCanvasWindow. Created hidden; Look::apply and subscribe here, unsubscribe in the
    // destructor. `store` may be null (Hint, tests): then the default place is used and nothing is saved.
    // Owners hold the strip as wxWeakRef<ProgressStrip> (the canvas deletes it when OpenCPN closes first) and
    // end it with `if (strip_) strip_->Destroy;`.
    // Host : Window = the panel is the only child of a top-level frame the strip creates
    // (no grip; drag by the body through the compositor); Canvas = a child of `canvas`. Fixed at
    // creation. The default is the chart host.
    enum class Host { Window, Canvas };
    ProgressStrip(wxWindow* canvas, const Look& look, Role role = Role::Job, ConfigStore* store = nullptr,
                  Host host = Host::Canvas);
    ~ProgressStrip() override;
    Host host() const { return frame_ ? Host::Window : Host::Canvas; }
    bool Show(bool show = true) override; // Window host: the frame follows (ShowWithoutActivating / Hide)
    bool Destroy() override; // Window host: hides and deletes the frame NOW (nothing after it uses this)
    // Running mode: one line, a gauge (range 1000; permille -1 = Pulse), the Stop button, "Details".
    void showProgress(const wxString& line, int permille, const wxString& stopLabel);
    // Result / hint mode: headline (may hold '\n'), no gauge, the buttons left to right, then "Details" when
    // there is Details text. A button never deletes the strip itself.
    void showResult(const wxString& headline, const wxString& details,
                    std::vector<std::pair<wxString, std::function<void()>>> buttons);
    // The Details fold's text is: the drag line (Job with a store, until the first drop), the headline when
    // it was cut to three lines, then `details`.
    void setDetails(const wxString& details);
    void openDetails(bool open); // the fold opened or closed by code (first success, )
    std::function<void()> onStop;
    std::function<void()> mouseHookForTest; // run inside the guarded mouse handler (tests make it throw)
    // Window host test seams: null = the real GTK call / the real display check.
    std::function<void(int rootX, int rootY, unsigned time)> beginMoveDragForTest;
    static inline std::function<bool()>
        isWaylandForTest; // static: the constructor reads it too; tests reset it

    // Test seam: the display's work area in screen pixels (null = wxDisplay); the real one cannot be made
    // small.
    static inline std::function<wxRect()> workAreaForTest;

private:
    void place();
    void logPlaced(); // the `placed` line of
    wxString logName() const { return role_ == Role::Job ? "job" : "hint"; }
    int minStripWidth() const;
    void relayout();
    void fitText(int maxWidth); // text_ = headline_ wrapped to maxWidth, at most three lines; sets cut_
    void applyLook();
    void clearResultButtons();
    bool updateDetailsButton();
    bool syncDetails();
    void showAndRaise();
    wxButton* makeButton(const wxString& label);
    void onMouse(wxMouseEvent& e);
    void onWindowMouse(wxMouseEvent& e); // window host: the compositor moves the frame
    void trackWindowDrop();              // X11: store the place once the frame has rested after a move
    bool onWayland() const;
    void endDrag(bool drop);
    wxPoint canvasPoint(wxWindow* from, const wxPoint& p) const;
    cg::StripRect safe() const;

    const Look& look_;
    Role role_;
    ConfigStore* store_;
    ConfigStore* winStore_ =
        nullptr;                 // window host, Job: the remembered place (X11 only; store_ stays null there)
    wxWeakRef<wxWindow> canvas_; // GetOCPNCanvasWindow: the area the strip is placed in; null once deleted
                                 // (the window host's frame outlives the canvas at OpenCPN shutdown)
    wxFrame* frame_ = nullptr;   // Window host: the panel's parent
    cg::StripAnchor anchor_;     // DIP
    int lookToken_ = 0;
    wxBoxSizer* column_ = nullptr;
    wxBoxSizer* row_ = nullptr;
    wxWindow* grip_ = nullptr;
    wxStaticText* text_ = nullptr;
    wxString headline_; // the text as given; text_ holds it wrapped (wxStaticText::Wrap changes the label)
    bool cut_ = false;  // the headline needed more than three lines
    wxString details_;
    wxGauge* gauge_ = nullptr;
    wxButton* stop_ = nullptr;
    wxButton* detailsButton_ = nullptr;
    wxTextCtrl* detailsText_ = nullptr;
    std::vector<wxButton*> resultButtons_;
    wxTimer timer_;
    bool running_ = true;
    bool detailsOpen_ = false;
    bool pressed_ = false, dragging_ = false;
    bool moved_ = false;  // window host: a begin-move happened, place no longer sets the position
    wxPoint pressScreen_; // window host: the press point in screen pixels
    wxPoint lastTick_,
        storedAt_; // window host (X11): the frame's position at the last tick / last stored drop
    bool haveTick_ = false, haveStored_ = false;
    wxPoint
        lastSet_; // window host: where place() left the frame; another position = moved by the system menu
    bool haveSet_ = false;
    wxPoint pressPoint_, pressPos_; // canvas client pixels
    bool placedSinceShow_ = false;  // the log's `placed` line of this show was written
    wxString lastPlaced_;           // the last `placed` text written
};
