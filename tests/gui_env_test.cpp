// SPDX-License-Identifier: GPL-3.0-or-later
// The window-test binary's environment (wx_env.cpp): the log must not be a message box.
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/log.h>

namespace {

// A GUI wxApp logs into wxLogGui, which opens a modal message box at wxEntryCleanup; with nobody to click it
// the test process hung. The environment must have installed wxLogStderr.
TEST(GuiEnv, LogGoesToStderrNotToAMessageBox) {
    if (!wxTheApp || !wxTheApp->IsGUI())
        GTEST_SKIP() << "no display: the GUI environment was NOT run";
    wxLog* target = wxLog::GetActiveTarget();
    EXPECT_NE(dynamic_cast<wxLogStderr*>(target), nullptr);
    EXPECT_EQ(dynamic_cast<wxLogGui*>(target), nullptr);
}

} // namespace
