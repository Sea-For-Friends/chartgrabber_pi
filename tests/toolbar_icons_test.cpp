// SPDX-License-Identifier: GPL-3.0-or-later
// / the three toolbar icons. OpenCPN renders them with
// nanosvg: plain svg / g / path / rect, fill and stroke attributes, no CSS, filter, mask or text. The colours
// are the literals of the spec: MUI_flat glyph #b4b4b4, idle square #282828, toggled square #898989.
#include <gtest/gtest.h>

#include <wx/xml/xml.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace {

const char* const kFiles[] = {"chartgrabber_tool.svg", "chartgrabber_tool_toggled.svg",
                              "chartgrabber_tool_rollover_toggled.svg"};

std::string dataPath(const std::string& n) {
    return std::string(CG_SOURCE_DIR) + "/data/" + n;
}

// The fill of the background rect ("" = none) and the stroke of the glyph group.
void walk(const wxXmlNode* n, std::set<std::string>& names, std::set<std::string>& attrs,
          std::string& rectFill, std::string& stroke, int& rects, std::string& rectGeom) {
    for (const wxXmlNode* c = n->GetChildren(); c; c = c->GetNext()) {
        if (c->GetType() != wxXML_ELEMENT_NODE)
            continue;
        names.insert(c->GetName().ToStdString());
        for (const wxXmlAttribute* a = c->GetAttributes(); a; a = a->GetNext())
            attrs.insert(a->GetName().ToStdString());
        if (c->GetName() == "rect") {
            rectFill = c->GetAttribute("fill").ToStdString();
            ++rects;
            rectGeom = c->GetAttribute("x").ToStdString() + "," + c->GetAttribute("y").ToStdString() + "," +
                       c->GetAttribute("width").ToStdString() + "," + c->GetAttribute("height").ToStdString();
        }
        if (c->GetName() == "g")
            stroke = c->GetAttribute("stroke").ToStdString();
        walk(c, names, attrs, rectFill, stroke, rects, rectGeom);
    }
}

} // namespace

TEST(ToolbarIcons, NanosvgSubsetAndTheMuiFlatColours) {
    struct Expect {
        const char* file;
        const char* rectFill;
    };
    const Expect table[] = {{"chartgrabber_tool.svg", "#282828"},
                            {"chartgrabber_tool_toggled.svg", "#898989"},
                            {"chartgrabber_tool_rollover_toggled.svg", "#898989"}};
    const std::set<std::string> okNames = {"g", "path", "rect"};
    const std::set<std::string> okAttrs = {"d",
                                           "x",
                                           "y",
                                           "width",
                                           "height",
                                           "fill",
                                           "stroke",
                                           "stroke-width",
                                           "stroke-linecap",
                                           "stroke-linejoin"};
    for (const Expect& e : table) {
        wxXmlDocument doc;
        ASSERT_TRUE(doc.Load(wxString::FromUTF8(dataPath(e.file)))) << e.file;
        const wxXmlNode* root = doc.GetRoot();
        ASSERT_NE(root, nullptr) << e.file;
        EXPECT_EQ(root->GetName(), "svg") << e.file;
        EXPECT_EQ(root->GetAttribute("viewBox"), "0 0 24 24") << e.file;
        std::set<std::string> names, attrs;
        std::string rectFill, stroke, rectGeom;
        int rects = 0;
        walk(root, names, attrs, rectFill, stroke, rects, rectGeom);
        EXPECT_EQ(rects, 1) << e.file;
        EXPECT_EQ(rectGeom, "-1,-1,26,26") << e.file; // larger than the view box: no light tile edge survives
        EXPECT_TRUE(names.count("path")) << e.file;
        for (const std::string& n : names)
            EXPECT_TRUE(okNames.count(n)) << e.file << ": element " << n;
        for (const std::string& a : attrs)
            EXPECT_TRUE(okAttrs.count(a)) << e.file << ": attribute " << a;
        EXPECT_EQ(rectFill, e.rectFill) << e.file;
        EXPECT_EQ(stroke, "#b4b4b4") << e.file; // same glyph in every state: no brighter running glyph
    }
}

// the four names are gone (OpenCPN's icon cache keys on the file name, ).
TEST(ToolbarIcons, TheP0035FileNamesAreGone) {
    for (const char* n : {"chartgrabber.svg", "chartgrabber_rollover.svg", "chartgrabber_toggled.svg",
                          "chartgrabber_rollover_toggled.svg"}) {
        EXPECT_FALSE(std::filesystem::exists(dataPath(n))) << n;
    }
}
