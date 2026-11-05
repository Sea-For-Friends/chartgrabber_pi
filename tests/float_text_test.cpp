// SPDX-License-Identifier: GPL-3.0-or-later
// Cg::parseDouble / cg::formatNumber behave as std::from_chars / std::to_chars(double) did, in any
// locale.
#include "cli_records.h"
#include "job_files.h"

#include <gtest/gtest.h>

#include <array>
#include <clocale>
#include <cmath>
#include <cstring>
#include <limits>
#include <locale>
#include <string>
#include <variant>

namespace cg {
namespace {

struct ParseCase {
    const char* text;
    bool ok;
    double value; // bit-exact when ok (NaN: only isnan)
};

const ParseCase kParse[] = {
    {"0", true, 0.0},     {"-0.5", true, -0.5}, {"12.25", true, 12.25}, {"1e3", true, 1000.0},
    {"1E-2", true, 0.01}, {".5", true, 0.5},    {"5.", true, 5.0},      {"-.5", true, -0.5},
    {"0.1", true, 0.1},   {"-0", true, -0.0},   {"", false, 0},         {" 1", false, 0},
    {"1 ", false, 0},     {"+1", false, 0},     {"1,5", false, 0},      {"1.5x", false, 0},
    {"--1", false, 0},    {"0x10", false, 0},   {"-", false, 0},        {"1e", false, 0},
    {"1e-400", false, 0}, {"1e400", false, 0},  {"nan(", false, 0},     {"infx", false, 0},
    {"0X1p3", false, 0},
};

const double kInf = std::numeric_limits<double>::infinity();

void checkParseTable() {
    for (const auto& c : kParse) {
        double v = 12345.0;
        const bool ok = parseDouble(c.text, v);
        EXPECT_EQ(ok, c.ok) << '"' << c.text << '"';
        if (ok && c.ok) {
            EXPECT_EQ(std::memcmp(&v, &c.value, sizeof v), 0) << '"' << c.text << "\" gave " << v;
        }
    }
    double v = 0;
    ASSERT_TRUE(parseDouble("inf", v));
    EXPECT_EQ(v, kInf);
    ASSERT_TRUE(parseDouble("-Infinity", v));
    EXPECT_EQ(v, -kInf);
    ASSERT_TRUE(parseDouble("nan", v));
    EXPECT_TRUE(std::isnan(v));
    ASSERT_TRUE(parseDouble("NaN(a_1)", v));
    EXPECT_TRUE(std::isnan(v));
    // 400 digits: out of range for from_chars
    EXPECT_FALSE(parseDouble(std::string("1") + std::string(399, '0'), v));
    EXPECT_FALSE(parseDouble(std::string(400, '9'), v));
}

struct FormatCase {
    double value;
    std::array<const char*, 7> text; // decimals 0..6
};

const FormatCase kFormat[] = {
    {0.0, {"0", "0.0", "0.00", "0.000", "0.0000", "0.00000", "0.000000"}},
    {-0.0, {"-0", "-0.0", "-0.00", "-0.000", "-0.0000", "-0.00000", "-0.000000"}},
    {0.5, {"0", "0.5", "0.50", "0.500", "0.5000", "0.50000", "0.500000"}},
    {1.5, {"2", "1.5", "1.50", "1.500", "1.5000", "1.50000", "1.500000"}},
    {2.5, {"2", "2.5", "2.50", "2.500", "2.5000", "2.50000", "2.500000"}},
    {-1.5, {"-2", "-1.5", "-1.50", "-1.500", "-1.5000", "-1.50000", "-1.500000"}},
    {0.125, {"0", "0.1", "0.12", "0.125", "0.1250", "0.12500", "0.125000"}},
    {-12.3456785, {"-12", "-12.3", "-12.35", "-12.346", "-12.3457", "-12.34568", "-12.345678"}},
    {1e15,
     {"1000000000000000", "1000000000000000.0", "1000000000000000.00", "1000000000000000.000",
      "1000000000000000.0000", "1000000000000000.00000", "1000000000000000.000000"}},
    {123456.789,
     {"123457", "123456.8", "123456.79", "123456.789", "123456.7890", "123456.78900", "123456.789000"}},
};

void checkFormatTable() {
    for (const auto& c : kFormat) {
        for (int d = 0; d <= 6; ++d) {
            EXPECT_EQ(formatNumber(c.value, d), c.text[static_cast<std::size_t>(d)]) << c.value << " / " << d;
        }
    }
}

// A comma-decimal, dot-grouping C++ locale needs no installed catalogue.
struct CommaPunct : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    char do_thousands_sep() const override { return '.'; }
    std::string do_grouping() const override { return "\3"; }
};

TEST(FloatText, ParseTable) {
    checkParseTable();
}

TEST(FloatText, FormatTable) {
    checkFormatTable();
}

// The CLI record path (num<double>): same line as cli_records_test EstimateAllFields; the bad ones are
// rejected whole.
void checkEstimateRecord() {
    const auto r = parseStdoutLine("estimate\t{G}\t12\t900\t1000\t0.2500\t0.0500\t123456\t1\t987654");
    ASSERT_TRUE(r);
    const auto& e = std::get<EstimateRecord>(*r);
    EXPECT_EQ(e.sharePresent, 0.25);
    EXPECT_EQ(e.shareMarkedMissing, 0.05);
    for (const char* bad : {"0,2500", "nan", "inf", "+0.25", " 0.25", "0.25 "}) {
        EXPECT_FALSE(parseStdoutLine(std::string("estimate\t{G}\t12\t900\t1000\t") + bad + "\t0.05\t1\t1\t1"))
            << bad;
    }
}

TEST(FloatText, RecordFields) {
    checkEstimateRecord();
}

TEST(FloatText, CommaDecimalCxxGlobalLocale) {
    const std::locale old = std::locale::global(std::locale(std::locale::classic(), new CommaPunct));
    checkParseTable();
    checkFormatTable();
    checkEstimateRecord();
    EXPECT_EQ(formatNumber(1234567.5, 1), "1234567.5");
    double v = 0;
    EXPECT_FALSE(parseDouble("1,5", v));
    ASSERT_TRUE(parseDouble("1234.5", v));
    EXPECT_EQ(v, 1234.5);
    std::locale::global(old);
}

TEST(FloatText, CommaDecimalCLocale) {
    const std::string old = std::setlocale(LC_ALL, nullptr);
    const char* got = nullptr;
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "ru_RU.UTF-8", "de_DE"}) {
        got = std::setlocale(LC_ALL, name);
        if (got != nullptr) {
            break;
        }
    }
    if (got == nullptr) {
        std::setlocale(LC_ALL, old.c_str());
        GTEST_SKIP() << "no comma-decimal C locale installed: the C-locale assertions were NOT run";
    }
    checkParseTable();
    checkFormatTable();
    checkEstimateRecord();
    std::setlocale(LC_ALL, old.c_str());
}

} // namespace
} // namespace cg
