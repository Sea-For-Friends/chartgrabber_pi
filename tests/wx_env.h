// SPDX-License-Identifier: GPL-3.0-or-later
// One gtest Environment that gives every wx test the application object and trait services they need.
// It registers itself once, from wx_env.cpp; a wx test only has to be linked with it. No test file may
// start wx on its own (two setups leaked and replaced each other's wxAppConsole).
//

// RULE for a test that creates a window (wxFrame, wxDialog, a control): its file goes into
// CG_GUI_TEST_SOURCES in tests/CMakeLists.txt (binary cg_gui_tests, ctest label "gui";
// each window test runs on its own Xvfb through tools/xvfb_test.sh), never into CG_TEST_SOURCES
// (cg_tests is a console wxAppConsole with no display). Start the test with `if (!wxTheApp ||
// !wxTheApp->IsGUI) GTEST_SKIP << "...NOT run";`. Do not choose the app kind by a gtest filter or an
// environment variable. The environment also sets the wx log target to stderr: a test that wants to see log
// lines installs its own wxLog and restores the old one.
//

// RULE for the OpenCPN API fakes (GetGlobalColor, DimeWindow, GetpPrivateApplicationDataLocation, the scale
// and touch queries, the canvas functions): they live ONLY in tests/ocpn_fakes.cpp, compiled into both
// binaries; a test file never defines one (a second definition is a duplicate symbol the day both land in one
// binary). A value a test must steer goes through tests/ocpn_fakes.h; a new fake is added to that file.
#pragma once
//

// RULE for a test whose fixture starts a child process (a fake CLI): its TearDown calls
// expectNoProcessesUnder(<the test's temp dir>) after it released the runner and BEFORE it removes the dir. A
// child that is still alive fails the test and is killed there, so a failing or hung run leaves no orphan.
// A wx assertion fails the running test (stderr line with a stack), it never opens a dialog.
#include <chrono>
#include <string>
#include <vector>

// The pids of other processes whose command line or working directory is inside `dir`.
std::vector<int> processesUnder(const std::string& dir);
// Waits up to `grace` for those processes to end; fails the current test for each that remains and SIGKILLs
// it.
void expectNoProcessesUnder(const std::string& dir,
                            std::chrono::milliseconds grace = std::chrono::milliseconds(2000));
