// SPDX-License-Identifier: GPL-3.0-or-later
// When OpenCPN's chart list may be updated: on OpenCPN 5.14+ every update re-parses all charts and a second
// one while the first runs can leave the list half built, so requests are merged into at most one call per
// 30 s and none is made while OpenCPN's Options dialog is shown. No wx; std only.
#pragma once

#include <cstdint>
#include <optional>

namespace cg {

class RescanThrottle {
public:
    static constexpr std::int64_t kMinGapMs = 30000;     // 5.14+: at most one call per 30 s
    static constexpr std::int64_t kOptionsPollMs = 2000; // re-check while the Options dialog is shown
    enum class Decision { CallNow, Wait };

    // A request at nowMs. asyncDb: OpenCPN 5.14+ or not known yet; optionsShown: the Options dialog is shown.
    // Without asyncDb (5.10, 5.12: the call is synchronous) and with no dialog: always CallNow.
    // Pending requests are one call.
    Decision request(std::int64_t nowMs, bool asyncDb, bool optionsShown);
    // The wait timer fired: CallNow when a request is pending and the gap and the dialog allow it, else Wait.
    Decision due(std::int64_t nowMs, bool asyncDb, bool optionsShown);
    void called(std::int64_t nowMs); // the caller made the call: pending cleared, the gap starts
    bool pending() const { return pending_; }
    // Milliseconds to arm the one-shot timer while the answer is Wait; 0 when nothing is pending or a call is
    // allowed.
    std::int64_t waitMs(std::int64_t nowMs, bool optionsShown) const;

private:
    bool gapOver(std::int64_t nowMs) const; // a clock step backwards counts as over
    std::optional<std::int64_t> last_;
    bool pending_ = false;
};

} // namespace cg
