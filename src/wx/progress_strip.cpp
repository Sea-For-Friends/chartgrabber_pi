// SPDX-License-Identifier: GPL-3.0-or-later
#include "progress_strip.h"

#include "config_store.h"
#include "guard.h"
#include "look.h"
#include "messages.h"

#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/intl.h>
#include <wx/settings.h>

#include <algorithm>

#include <ocpn_plugin.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif
#ifdef __WXGTK__
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
#endif

namespace {

constexpr int kTimerMs = 500;  // re-placement while shown
constexpr int kDetailRows = 8; // rows of the Details fold
constexpr int kTextLines = 3;  // the headline wraps to at most this many lines, the last one ellipsized
constexpr int kGripDip = 16, kGripTouchDip = 24;

// The drag handle: 2 x 3 dots in the text colour.
class Grip : public wxWindow {
public:
    explicit Grip(wxWindow* parent) : wxWindow(parent, wxID_ANY) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxPaintDC dc(this);
            dc.SetBackground(wxBrush(GetBackgroundColour()));
            dc.Clear();
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(GetForegroundColour()));
            const wxSize size = GetClientSize();
            const int dot = FromDIP(2), gap = FromDIP(3);
            const int x0 = (size.x - (2 * dot + gap)) / 2, y0 = (size.y - (3 * dot + 2 * gap)) / 2;
            for (int col = 0; col < 2; ++col)
                for (int row = 0; row < 3; ++row)
                    dc.DrawRectangle(x0 + col * (dot + gap), y0 + row * (dot + gap), dot, dot);
        });
    }
};

} // namespace

namespace {
// The window host's frame (as before): floats over the main window, no taskbar entry, no border.
wxWindow* hostParent(wxWindow* canvas, ProgressStrip::Host host) {
    if (host == ProgressStrip::Host::Canvas)
        return canvas;
    auto* frame = new wxFrame(
        wxGetTopLevelParent(canvas), wxID_ANY, _("Chart Grabber"), wxDefaultPosition, wxDefaultSize,
        wxFRAME_FLOAT_ON_PARENT | wxFRAME_NO_TASKBAR | wxBORDER_NONE
#ifdef __WXMSW__
            | wxSYSTEM_MENU // WS_SYSMENU: Alt+Space and the system Move
            | wxCLOSE_BOX   // without it wx greys the menu's Close; WM_CLOSE -> close handler
#endif
    );
    return frame;
}
} // namespace

ProgressStrip::ProgressStrip(wxWindow* canvas, const Look& look, Role role, ConfigStore* store, Host host)
    : wxPanel(hostParent(canvas, host), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE),
      look_(look), role_(role), store_(role == Role::Job && host == Host::Canvas ? store : nullptr),
      canvas_(canvas), timer_(this) {
    if (host == Host::Window && role == Role::Job)
        winStore_ = store;
    if (host == Host::Window) {
        frame_ = static_cast<wxFrame*>(GetParent());
        auto* box = new wxBoxSizer(wxVERTICAL);
        box->Add(this, 1, wxEXPAND);
        frame_->SetSizer(box);
    }
    Hide();
    SetBackgroundStyle(wxBG_STYLE_PAINT); // GTK fills no background of a child of the chart canvas: paint it
    Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();
    });
    if (role_ == Role::Hint)
        anchor_ = {cg::StripCorner::TopLeft, cg::kStripMargin, cg::kStripMargin};
    else
        anchor_ =
            (store_                      ? store_->stripAnchor()
             : winStore_ && !onWayland() ? winStore_->stripAnchor() // Wayland: nothing is read
                                         : std::nullopt)
                .value_or(
                    cg::StripAnchor{cg::StripCorner::BottomLeft, cg::kStripDefaultDx, cg::kStripDefaultDy});
    column_ = new wxBoxSizer(wxVERTICAL);
    detailsText_ = new wxTextCtrl(this, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
                                  wxTE_MULTILINE | wxTE_READONLY);
    detailsText_->SetMinSize(wxSize(-1, kDetailRows * detailsText_->GetCharHeight() + 8));
    detailsText_->Hide();
    row_ = new wxBoxSizer(wxHORIZONTAL);
    const bool draggable = role_ == Role::Job && !frame_; // the chart host: grip, capture
    if (draggable) {
        grip_ = new Grip(this);
        grip_->SetToolTip(_("Drag to move. Double-click to put back."));
        row_->Add(grip_, 0, wxEXPAND | wxRIGHT, 6);
    }
    text_ = new wxStaticText(this, wxID_ANY, wxString());
    gauge_ = new wxGauge(this, wxID_ANY, 1000, wxDefaultPosition, wxSize(160, -1));
    stop_ = makeButton(_("Stop"));
    detailsButton_ = makeButton(_("Details"));
    row_->Add(text_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    row_->Add(gauge_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    row_->Add(stop_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    row_->Add(detailsButton_, 0, wxALIGN_CENTER_VERTICAL);
    column_->Add(detailsText_, 0, wxEXPAND | wxALL, 4);
    column_->Add(row_, 0, wxEXPAND | wxALL, 4);
    SetSizer(column_);

    if (role_ == Role::Job) { // both hosts drag by the body; only the chart host has a grip and a capture
        detailsText_->SetCursor(wxCursor(wxCURSOR_IBEAM));
        for (wxWindow* w : {static_cast<wxWindow*>(this), static_cast<wxWindow*>(text_),
                            static_cast<wxWindow*>(gauge_), grip_}) {
            if (!w)
                continue;
            w->SetCursor(wxCursor(wxCURSOR_SIZING));
            const auto handler = [this](wxMouseEvent& e) { cg::guarded("strip mouse", [&] { onMouse(e); }); };
            w->Bind(wxEVT_LEFT_DOWN, handler);
            w->Bind(wxEVT_LEFT_UP, handler);
            w->Bind(wxEVT_MOTION, handler);
            w->Bind(wxEVT_LEFT_DCLICK, handler);
        }
    }
    if (draggable)
        Bind(wxEVT_MOUSE_CAPTURE_LOST,
             [this](wxMouseCaptureLostEvent&) { cg::guarded("strip capture lost", [&] { endDrag(true); }); });
    if (frame_) { // the chart host leaves the keys to OpenCPN
        const auto escape = [this] {
            if (running_ && onStop)
                onStop();
            else if (!running_)
                Hide(); // what every result Close does
        };
        frame_->Bind(wxEVT_CHAR_HOOK, [this, escape](wxKeyEvent& e) {
            cg::guarded("strip key", [&] {
                if (e.GetKeyCode() != WXK_ESCAPE)
                    return e.Skip();
                escape();
            });
        });
        // the system menu's Close / Alt+F4 act like Escape; the frame is never destroyed behind the strip's
        // back
        frame_->Bind(wxEVT_CLOSE_WINDOW, [this, escape](wxCloseEvent& e) {
            cg::guarded("strip close", [&] {
                if (!e.CanVeto())
                    return e.Skip();
                e.Veto();
                escape();
            });
        });
    }
    stop_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        cg::guarded("strip stop", [&] {
            if (onStop)
                onStop();
        });
    });
    detailsButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        cg::guarded("strip details", [&] { openDetails(!detailsOpen_); });
    });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        cg::guarded("strip timer", [&] {
            trackWindowDrop();
            place();
        });
    });
    Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
        cg::guarded("strip show", [&] { // the placement timer runs only while the strip is shown
            if (e.IsShown()) {
                timer_.Start(kTimerMs);
                placedSinceShow_ = false;
            } else {
                timer_.Stop();
            }
            cg::logFile("strip " + logName() + (e.IsShown() ? " shown" : " hidden"));
            e.Skip();
        });
    });
    lookToken_ = look_.subscribe([this] { applyLook(); });
    applyLook();
    const wxWindow* parent = frame_ ? frame_->GetParent() : GetParent(); // the canvas / the frame's parent
    cg::logFile(wxString::Format("strip %s created on %s: parent %s %dx%d", logName(),
                                 frame_ ? "window" : "chart", parent->GetClassInfo()->GetClassName(),
                                 parent->GetSize().x, parent->GetSize().y));
}

ProgressStrip::~ProgressStrip() {
    // Destroying a shown window delivers wxEVT_SHOW(false) to it AFTER this body (MSW: DestroyWindow, in
    // ~wxWindow), when the [this] handlers below touch a dead object (timer_) and OpenCPN crashes:
    // hide it here.
    if (IsShown())
        wxPanel::Show(false); // not Show(): the window host's frame is already being deleted
    cg::logFile("strip " + logName() + " destroyed");
    timer_.Stop();
    look_.unsubscribe(lookToken_);
}

bool ProgressStrip::Show(bool show) {
    if (!frame_)
        return wxPanel::Show(show);
    const bool changed = wxPanel::Show(show); // the panel's own wxEVT_SHOW runs the placement timer
    if (show) {
        place(); // the frame is placed before it appears
        if (!frame_->IsShown())
            frame_->ShowWithoutActivating(); // never takes the keyboard focus (wx gtk/toplevel.h:57)
        logPlaced();
    } else {
        frame_->Hide();
    }
    return changed;
}

bool ProgressStrip::Destroy() {
    if (!frame_)
        return wxPanel::Destroy();
    wxFrame* f = frame_; // wxFrame::Destroy would wait for idle time, too late in DeInit
    f->Hide();
    delete f; // deletes this panel as its child: nothing below may use this
    return true;
}

wxButton* ProgressStrip::makeButton(const wxString& label) {
    auto* b = new wxButton(this, wxID_ANY, label);
    b->SetCanFocus(false);
    if (const int h = look_.minControlHeight(); h > 0)
        b->SetMinSize(wxSize(-1, h));
    if (grip_)
        b->SetCursor(wxCursor(wxCURSOR_ARROW)); // buttons only click, they never start a drag
    return b;
}

void ProgressStrip::applyLook() {
    look_.apply(this);
    if (frame_) {
        frame_->SetBackgroundColour(look_.bodyBackground());
        frame_->SetForegroundColour(look_.bodyText());
    }
    const wxColour bg = look_.bodyBackground(), fg = look_.bodyText();
    for (wxWindow* w : {static_cast<wxWindow*>(this), static_cast<wxWindow*>(text_), grip_}) {
        if (!w) // the Hint role has no grip
            continue;
        w->SetBackgroundColour(bg);
        w->SetForegroundColour(fg);
    }
    // The controls: DimeControl gives buttons DILG1 only (no foreground) and never touches a wxGauge; set
    // them here, and not through DimeWindow, so a dark system theme gets the same.
    const wxColour controlBg = look_.controlBackground(), controlFg = look_.controlText();
    gauge_->SetBackgroundColour(controlBg);
    std::vector<wxButton*> buttons = {stop_, detailsButton_};
    buttons.insert(buttons.end(), resultButtons_.begin(), resultButtons_.end());
    for (wxButton* b : buttons) {
        b->SetBackgroundColour(controlBg);
        b->SetForegroundColour(controlFg);
    }
    Refresh();
}

void ProgressStrip::clearResultButtons() {
    for (wxButton* b : resultButtons_) {
        row_->Detach(b);
        b->Hide();
        b->Destroy(); // NOT deferred for a child window: never call this from the button's own handler
    }
    resultButtons_.clear();
}

// The text of the Details fold: drag line, the cut headline, then the caller's text.
bool ProgressStrip::syncDetails() {
    wxString t;
    if (store_ && !store_->stripHintShown())
        t << stripHintText() << "\n\n";
    if (cut_)
        t << headline_ << "\n\n";
    t << details_;
    if (details_.empty())
        t.Trim();
    if (t == detailsText_->GetValue())
        return false;
    detailsText_->ChangeValue(t);
    return true;
}

// Shows the Details button when there is something to show; returns true when the layout must be redone.
bool ProgressStrip::updateDetailsButton() {
    const bool want = running_ || !detailsText_->IsEmpty();
    bool changed = detailsButton_->IsShown() != want;
    detailsButton_->Show(want);
    if (!running_ && detailsText_->IsEmpty() && detailsOpen_) {
        detailsOpen_ = false;
        detailsText_->Hide();
        changed = true;
    }
    return changed;
}

void ProgressStrip::showAndRaise() {
    Show();
    if (!frame_)
        Raise(); // a later sibling of OpenCPN's GL child must draw above it
}

void ProgressStrip::relayout() {
    if (look_.minControlHeight() > 0) { // the touch setting may have changed since the buttons were made
        for (wxButton* b : {stop_, detailsButton_})
            b->SetMinSize(wxSize(-1, look_.minControlHeight()));
    }
    if (grip_)
        grip_->SetMinSize(wxSize(FromDIP(look_.touch() ? kGripTouchDip : kGripDip), -1));
    place();
}

void ProgressStrip::fitText(int maxWidth) {
    text_->SetLabelText(headline_);
    text_->Wrap(maxWidth);
    const wxString wrapped = text_->GetLabelText();
    std::vector<wxString> lines;
    for (size_t at = 0;;) {
        const size_t nl = wrapped.find('\n', at);
        lines.push_back(wrapped.substr(at, nl == wxString::npos ? wxString::npos : nl - at));
        if (nl == wxString::npos)
            break;
        at = nl + 1;
    }
    cut_ = lines.size() > static_cast<size_t>(kTextLines);
    if (!cut_)
        return;
    // Lines past the last kept one are joined into it and cut with an ellipsis (wxControl::Ellipsize).
    wxString last = lines[kTextLines - 1];
    for (size_t i = kTextLines; i < lines.size(); ++i)
        last += " " + lines[i];
    wxClientDC dc(text_);
    dc.SetFont(text_->GetFont());
    wxString out;
    for (int i = 0; i < kTextLines - 1; ++i)
        out += lines[i] + "\n";
    text_->SetLabelText(out + wxControl::Ellipsize(last, dc, wxELLIPSIZE_END, maxWidth));
}

cg::StripRect ProgressStrip::safe() const {
    const wxSize cs = canvas_->GetClientSize();
    return cg::safeArea(cs.x, cs.y, GetChartbarHeight());
}

// The headline's floor : the row's fixed parts plus kStripMinTextWidth, at least
// kStripMinWidth.
int ProgressStrip::minStripWidth() const {
    int fixed = (GetSize() - GetClientSize()).x + 2 * 4; // panel border + the column's border
    for (const wxSizerItem* item : row_->GetChildren()) {
        if (!item->IsShown())
            continue;
        if (item->GetWindow() == text_)
            fixed += 8; // the headline item's own right border; its text is not counted
        else
            fixed += item->GetMinSizeWithBorder().x;
    }
    return std::max(FromDIP(cg::kStripMinWidth), fixed + FromDIP(cg::kStripMinTextWidth));
}

void ProgressStrip::place() {
    if (dragging_) // the pointer places the strip until the drop
        return;
    if (!canvas_) // the window host's frame outlives the canvas when OpenCPN closes: nothing to place in
        return;
    const wxSize cs = canvas_->GetClientSize();
    const int margin = FromDIP(cg::kStripMargin);
    const wxSize border = GetSize() - GetClientSize();
    for (int pass = 0; pass < 2; ++pass) {
        const int cap = cg::stripWidth(cs.x, cs.x, minStripWidth(), margin); // the row may have changed
        const int room = std::max(cap - border.x, 1);
        fitText(room);
        column_->Layout();
        if (const int over = column_->GetMinSize().x - room; over > 0) {
            // the other items of the row take part of the width: the text gets what is left of it
            fitText(std::max(text_->GetBestSize().x - over, 1));
            column_->Layout();
        }
        const bool changed = syncDetails() | updateDetailsButton(); // a new text may change the row
        if (!changed)
            break;
    }
    wxRect work; // window host, not Wayland: the display's work area (screen pixels), else empty
    if (frame_ && !onWayland() && workAreaForTest) {
        work = workAreaForTest();
    } else if (frame_ && !onWayland()) {
        const wxPoint mid = frame_->GetPosition() + wxPoint(frame_->GetSize().x / 2, frame_->GetSize().y / 2);
        int d = wxDisplay::GetFromPoint(moved_ ? mid : canvas_->ClientToScreen(wxPoint(0, 0)));
        work = wxDisplay(d == wxNOT_FOUND ? 0u : static_cast<unsigned>(d)).GetClientArea();
    }
    const int fullDetails = kDetailRows * detailsText_->GetCharHeight() + 8;
    detailsText_->SetMinSize(wxSize(-1, fullDetails));
    wxSize best = column_->GetMinSize() + border;
    if (!work.IsEmpty() && detailsText_->IsShown() && best.y > work.height - 2 * margin) {
        // too low for the Details fold: it gets shorter (two rows at least) and scrolls
        const int floorH = 2 * detailsText_->GetCharHeight() + 8;
        const int h = std::max(fullDetails - (best.y - (work.height - 2 * margin)), floorH);
        detailsText_->SetMinSize(wxSize(-1, h));
        best = column_->GetMinSize() + border;
    }
    const int width = cg::stripWidth(best.x, cs.x, minStripWidth(), margin);
    const cg::StripAnchor px{anchor_.corner, FromDIP(anchor_.dx), FromDIP(anchor_.dy)};
    const cg::StripRect r = cg::placeStrip(px, width, best.y, safe(), margin);
#ifdef __WXMSW__ // GTK keeps its old behaviour: a window manager may shift the frame by itself
    if (frame_ && !moved_ && haveSet_ && frame_->IsShown() && frame_->GetPosition() != lastSet_) {
        moved_ = true; // not by place(): the system menu's Move (Alt+Space); it stays where it is put
        haveTick_ = false;
    }
#endif
    if (frame_) { // screen pixels; the sizer makes the panel fill the frame
        // moved: the compositor / window manager placed it, only the size follows (and the work-area clamp)
        const wxPoint at = moved_ ? frame_->GetPosition() : canvas_->ClientToScreen(wxPoint(r.x, r.y));
        cg::StripRect f{at.x, at.y, r.width, r.height};
        if (!work.IsEmpty())
            f = cg::fitInWorkArea(f, {work.x, work.y, work.width, work.height}, margin);
        if (moved_ && f.x == at.x && f.y == at.y)
            frame_->SetSize(f.width, f.height);
        else
            frame_->SetSize(f.x, f.y, f.width, f.height);
        lastSet_ = frame_->GetPosition();
        haveSet_ = true;
        frame_->Layout();
    } else {
        SetSize(r.x, r.y, r.width, r.height);
    }
    Layout();
    logPlaced();
}

// The first placement after each show goes to the log; with Debug on, so does every later one whose numbers
// differ from the last line written (the 500 ms timer would otherwise write two lines a second).
void ProgressStrip::logPlaced() {
    if (!IsShown() || (frame_ && !frame_->IsShown())) // the window host's frame is not up yet: Show logs
        return;
    if (placedSinceShow_ && !cg::pluginLogDebug()) // hot path (500 ms timer): nothing to write, do not format
        return;
    const wxWindow* top = frame_ ? static_cast<const wxWindow*>(frame_) : this;
    const wxSize cs = canvas_->GetClientSize();
    const wxString text =
        wxString::Format("strip %s placed: %d,%d %dx%d, corner %s, canvas %dx%d, chart bar %d, on screen %s",
                         logName(), top->GetPosition().x, top->GetPosition().y, top->GetSize().x,
                         top->GetSize().y, wxString(std::string(cg::cornerKey(anchor_.corner))), cs.x, cs.y,
                         GetChartbarHeight(), IsShownOnScreen() ? "yes" : "no");
    if (!placedSinceShow_) {
        placedSinceShow_ = true;
        cg::logFile(text);
        lastPlaced_ = text;
    } else if (cg::pluginLogDebug() && text != lastPlaced_) {
        cg::logDebug(text);
        lastPlaced_ = text;
    }
}

wxPoint ProgressStrip::canvasPoint(wxWindow* from, const wxPoint& p) const {
    return canvas_->ScreenToClient(from->ClientToScreen(p));
}

bool ProgressStrip::onWayland() const {
    if (isWaylandForTest)
        return isWaylandForTest();
#if defined(__WXGTK__) && defined(GDK_WINDOWING_WAYLAND)
    return GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default());
#else
    return false;
#endif
}

// Window host. A left press, then motion beyond the drag threshold: the toolkit's compositor-driven move
// starts once (Move/SetSize is ignored by a Wayland compositor) and the strip leaves the mouse to it until
// the next press; no capture, the window manager owns the pointer and the release.
void ProgressStrip::onWindowMouse(wxMouseEvent& e) {
    auto* from = wxDynamicCast(e.GetEventObject(), wxWindow);
    if (!from)
        return;
    const wxPoint at = from->ClientToScreen(e.GetPosition());
    const wxEventType type = e.GetEventType();
    if (type == wxEVT_LEFT_DOWN) {
        pressed_ = true;
        pressScreen_ = at;
    } else if (type == wxEVT_MOTION && pressed_ && e.LeftIsDown()) {
        if (!cg::dragStarted(at.x - pressScreen_.x, at.y - pressScreen_.y, look_.touch()))
            return;
        pressed_ = false;
        moved_ = true;
        haveTick_ = false;
        unsigned time = static_cast<unsigned>(e.GetTimestamp());
#ifdef __WXGTK__
        if (!time)
            time = gtk_get_current_event_time();
#endif
        if (beginMoveDragForTest)
            beginMoveDragForTest(at.x, at.y, time);
#ifdef __WXGTK__
        else
            gtk_window_begin_move_drag(GTK_WINDOW(frame_->GetHandle()), 1, at.x, at.y, time);
#endif
#ifdef __WXMSW__
        else { // the native caption drag: the system runs the move loop until the release
            ::ReleaseCapture();
            ::SendMessage(static_cast<HWND>(frame_->GetHandle()), WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
#endif
    } else if (type == wxEVT_LEFT_UP) {
        pressed_ = false;
    } else if (type == wxEVT_LEFT_DCLICK && !onWayland()) { // the place is only remembered on X11
        pressed_ = moved_ = haveTick_ = haveStored_ = haveSet_ = false;
        anchor_ = {cg::StripCorner::BottomLeft, cg::kStripDefaultDx, cg::kStripDefaultDy};
        if (winStore_)
            winStore_->setStripAnchor(std::nullopt);
        place();
    }
}

// X11, window host: the frame's position equal on two ticks after a move is the drop; its anchor is stored
// once.
void ProgressStrip::trackWindowDrop() {
    if (!frame_ || !moved_ || !canvas_ || onWayland())
        return;
    const wxPoint pos = frame_->GetPosition();
    const bool rested = haveTick_ && pos == lastTick_ && !(haveStored_ && pos == storedAt_);
    lastTick_ = pos;
    haveTick_ = true;
    if (!rested)
        return;
    const wxPoint c = canvas_->ScreenToClient(pos);
    const wxSize size = frame_->GetSize();
    const cg::StripAnchor a = cg::anchorFromDrop({c.x, c.y, size.x, size.y}, safe());
    anchor_ = {a.corner, ToDIP(a.dx), ToDIP(a.dy)};
    storedAt_ = pos;
    haveStored_ = true;
    if (winStore_)
        winStore_->setStripAnchor(anchor_);
}

void ProgressStrip::onMouse(wxMouseEvent& e) {
    if (mouseHookForTest)
        mouseHookForTest();
    if (frame_)
        return onWindowMouse(e);
    auto* from = wxDynamicCast(e.GetEventObject(), wxWindow);
    if (!from)
        return;
    const wxPoint at = canvasPoint(from, e.GetPosition());
    const wxEventType type = e.GetEventType();
    if (type == wxEVT_LEFT_DOWN) {
        pressed_ = true;
        pressPoint_ = at;
        pressPos_ = GetPosition();
    } else if (type == wxEVT_MOTION && pressed_ && e.LeftIsDown()) {
        if (!dragging_) {
            if (!cg::dragStarted(at.x - pressPoint_.x, at.y - pressPoint_.y, look_.touch()))
                return;
            dragging_ = true;
            CaptureMouse();
        }
        const cg::StripRect s = safe();
        const int margin = FromDIP(cg::kStripMargin);
        const wxSize size = GetSize();
        const wxPoint want = pressPos_ + (at - pressPoint_);
        // clamped fully inside the safe area (a strip larger than the room keeps the margin side)
        const int x = std::max(std::min(want.x, s.x + s.width - margin - size.x), s.x + margin);
        const int y = std::max(std::min(want.y, s.y + s.height - margin - size.y), s.y + margin);
        Move(x, y);
    } else if (type == wxEVT_LEFT_UP) {
        endDrag(true);
    } else if (type == wxEVT_LEFT_DCLICK) {
        pressed_ = false;
        anchor_ = {cg::StripCorner::BottomLeft, cg::kStripDefaultDx, cg::kStripDefaultDy};
        if (store_)
            store_->setStripAnchor(std::nullopt);
        place();
    }
}

// Left up or capture lost: after a drag it is the drop (the nearest corner and the distances are stored).
void ProgressStrip::endDrag(bool drop) {
    const bool wasDragging = dragging_;
    pressed_ = dragging_ = false;
    if (HasCapture())
        ReleaseMouse();
    if (!wasDragging || !drop)
        return;
    const cg::StripAnchor a =
        cg::anchorFromDrop({GetPosition().x, GetPosition().y, GetSize().x, GetSize().y}, safe());
    anchor_ = {a.corner, ToDIP(a.dx), ToDIP(a.dy)};
    if (store_) {
        store_->setStripAnchor(anchor_);
        store_->setStripHintShown();
    }
    place();
}

void ProgressStrip::openDetails(bool open) {
    detailsOpen_ = open;
    detailsText_->Show(open);
    relayout();
}

void ProgressStrip::showProgress(const wxString& line, int permille, const wxString& stopLabel) {
    running_ = true;
    clearResultButtons();
    headline_ = line;
    text_->SetLabelText(line);
    gauge_->Show();
    if (permille < 0)
        gauge_->Pulse();
    else
        gauge_->SetValue(std::clamp(permille, 0, 1000));
    stop_->SetLabel(stopLabel);
    stop_->Show();
    relayout(); // the children are the ones applyLook already styled
    showAndRaise();
}

void ProgressStrip::showResult(const wxString& headline, const wxString& details,
                               std::vector<std::pair<wxString, std::function<void()>>> buttons) {
    running_ = false;
    clearResultButtons();
    headline_ = headline;
    details_ = details;
    text_->SetLabelText(headline);
    gauge_->Hide();
    stop_->Hide();
    for (auto& [label, fn] : buttons) {
        wxButton* b = makeButton(label);
        // fn may call showProgress / showResult, which destroy this button at once: run it after the handler
        // returns (~wxEvtHandler drops the pending call if the strip is deleted first).
        b->Bind(wxEVT_BUTTON, [this, fn](wxCommandEvent&) {
            cg::guarded("strip result button", [&] {
                if (fn)
                    CallAfter([fn] { cg::guarded("strip result action", [&] { fn(); }); });
            });
        });
        row_->Insert(row_->GetItemCount() - 1, b, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        resultButtons_.push_back(b);
    }
    applyLook();
    relayout();
    showAndRaise();
}

void ProgressStrip::setDetails(const wxString& details) {
    details_ = details;
    syncDetails();
    detailsText_->ShowPosition(detailsText_->GetLastPosition());
    updateDetailsButton();
    if (IsShown())
        relayout();
}
