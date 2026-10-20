// SPDX-License-Identifier: GPL-3.0-or-later
// Collects the wxLog messages of one test: installs itself as the active log target, restores the
// old one at the end of the scope.
#pragma once

#include <wx/log.h>

#include <string>
#include <vector>

class LogCapture : public wxLog {
public:
    LogCapture() : previous_(wxLog::SetActiveTarget(this)) {}
    ~LogCapture() override { wxLog::SetActiveTarget(previous_); }
    std::vector<std::string> lines;

protected:
    void DoLogRecord(wxLogLevel, const wxString& msg, const wxLogRecordInfo&) override {
        lines.push_back(msg.utf8_string());
    }

private:
    wxLog* previous_;
};
