// SPDX-License-Identifier: GPL-3.0-or-later
// and 4 : the two guard
// templates and the load-guard decision.
#include "guard.h"
#include "log_capture.h"
#include "wx_env.h"

#include <gtest/gtest.h>

#include <wx/defs.h>

#include <dlfcn.h>
#include <stdexcept>
#include <string>
#include <vector>

TEST(Guard, StdExceptionIsCaughtLoggedOnceAndReportedAsFailure) {
    LogCapture log;
    bool result = true;
    EXPECT_NO_THROW(result = cg::guarded("where-1", [] { throw std::runtime_error("kaput ёж"); }));
    EXPECT_FALSE(result);
    EXPECT_EQ(log.lines, std::vector<std::string>{"ChartGrabber: where-1: kaput ёж"});
}

TEST(Guard, NonStdThrowIsCaughtAsUnknownException) {
    LogCapture log;
    bool result = true;
    EXPECT_NO_THROW(result = cg::guarded("where-2", [] { throw 42; }));
    EXPECT_FALSE(result);
    EXPECT_EQ(log.lines, std::vector<std::string>{"ChartGrabber: where-2: unknown exception"});
}

TEST(Guard, NormalCallRunsReturnsTrueAndLogsNothing) {
    LogCapture log;
    int ran = 0;
    EXPECT_TRUE(cg::guarded("where-3", [&] { ran = 7; }));
    EXPECT_EQ(ran, 7);
    EXPECT_TRUE(log.lines.empty());
}

TEST(GuardOr, ThrowGivesTheFallbackAndOneLogLine) {
    LogCapture log;
    int a = 0, b = 0;
    EXPECT_NO_THROW(a = cg::guardedOr("where-4", 11, []() -> int { throw std::logic_error("bad"); }));
    EXPECT_NO_THROW(b = cg::guardedOr("where-5", 12, []() -> int { throw 'x'; }));
    EXPECT_EQ(a, 11);
    EXPECT_EQ(b, 12);
    EXPECT_EQ(log.lines, (std::vector<std::string>{"ChartGrabber: where-4: bad",
                                                   "ChartGrabber: where-5: unknown exception"}));
}

TEST(GuardOr, NormalCallReturnsItsValueAndLogsNothing) {
    LogCapture log;
    EXPECT_EQ(cg::guardedOr("where-6", false, [] { return true; }), true);
    EXPECT_EQ(cg::guardedOr("where-7", 5, [] { return 9; }), 9);
    EXPECT_TRUE(log.lines.empty());
}

// off exactly when (host has the symbol) != (build has wxUSE_HOTKEY).
TEST(LoadGuard, OffExactlyWhenHostAndBuildDisagree) {
    EXPECT_FALSE(cg::loadGuardOff(true, true));
    EXPECT_TRUE(cg::loadGuardOff(true, false));
    EXPECT_TRUE(cg::loadGuardOff(false, true));
    EXPECT_FALSE(cg::loadGuardOff(false, false));
}

// Proof 5: the wx this test binary runs against is the one it was built with, so the guard is "on" here. The
// symbol is the one of (`nm -D libwx_gtk3u_core-3.2.so` lists it when wxUSE_HOTKEY is 1).
TEST(LoadGuard, TheBuildOfThisMachineIsNotRefused) {
    const bool host = dlsym(RTLD_DEFAULT, "_ZN12wxWindowBase14RegisterHotKeyEiii") != nullptr;
    EXPECT_EQ(host, wxUSE_HOTKEY != 0);
    EXPECT_FALSE(cg::loadGuardOff(host, wxUSE_HOTKEY != 0));
}
