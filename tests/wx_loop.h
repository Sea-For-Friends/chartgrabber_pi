// SPDX-License-Identifier: GPL-3.0-or-later
// The local wx event loop of the tests that run a CliRunner (moved out of cli_runner_test.cpp): a
// console loop that is the active one for the test, so wxTimer, END_PROCESS and stopAndWait work.
#pragma once

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/apptrait.h>
#include <wx/evtloop.h>

#include <chrono>
#include <functional>
#include <memory>

class LoopFixture : public testing::Test {
protected:
    using Clock = std::chrono::steady_clock;
    void SetUp() override {
        ASSERT_NE(wxApp::GetInstance(), nullptr);
        loop_.reset(wxApp::GetInstance()->GetTraits()->CreateEventLoop());
        ASSERT_NE(loop_, nullptr);
        activator_ = std::make_unique<wxEventLoopActivator>(loop_.get());
    }
    void TearDown() override {
        activator_.reset();
        loop_.reset();
    }
    // Dispatches events until done or the timeout; true when done became true.
    bool runUntil(const std::function<bool()>& done, int timeoutMs) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!done()) {
            if (Clock::now() >= deadline) {
                return false;
            }
            loop_->DispatchTimeout(20);
        }
        return true;
    }
    void runFor(int ms) {
        const auto until = Clock::now() + std::chrono::milliseconds(ms);
        while (Clock::now() < until) {
            loop_->DispatchTimeout(20);
        }
    }
    std::unique_ptr<wxEventLoopBase> loop_;
    std::unique_ptr<wxEventLoopActivator> activator_;
};
