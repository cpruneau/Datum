/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestGraphDefaults.cpp
//! @brief Tests of GraphDefaults: the settings new graphs start with, written and read back; the placement of the axis
//! titles and the texts of the legend in plot files.

#include <cstdio>
#include "Graph.hpp"
#include "GraphDefaults.hpp"
#include "TextAttributes.hpp"
#include "XmlElement.hpp"

using namespace Datum;
using namespace Datum::GRAPHICS;

namespace
{
int nChecks = 0, nFailures = 0;
void check(bool ok, const char * what, int line)
{
nChecks++;
if (!ok) { nFailures++; std::printf("FAILED (line %d): %s\n", line, what); }
}
} // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main()
{
  GraphDefaults & defaults = GraphDefaults::session();
  const GraphDefaults saved = defaults;

  // ---- the defaults: graphs as before
  {
    Graph g;
    CHECK(g.titlePointSize() == 0 && g.legendPointSize() == 0 && g.xAxis().nMajorTicks() == 5);
    CHECK(g.xAxis().titleAlignment() == TextAttributes::kCenter && g.legendTextAttributes().justification() == TextAttributes::kLeft);
    CHECK(g.legendFill().style() == 0 && g.legendLine().style() == 0);
    CHECK(g.titleAttributes().font() == TextAttributes::defaultTextAttributes()->font());
  }

  // ---- changed: new graphs take them
  defaults.title.font = 132;  defaults.title.size = 18;
  defaults.axisTitles.size = 14;  defaults.axisLabels.font = 82;  defaults.axisLabels.color = 4;
  defaults.legend.font = 22;  defaults.legend.size = 11;
  defaults.nDivisions = 8;
  defaults.axisTitleAlignment = TextAttributes::kRight;
  defaults.legendBackground = true;  defaults.legendFillColor = 5;
  defaults.legendBorder = true;  defaults.legendLineStyle = 2;  defaults.legendLineWidth = 1.5f;  defaults.legendLineColor = 3;
  defaults.legendJustification = TextAttributes::kCenter;
  {
    Graph g;
    CHECK(g.titleAttributes().font() == 132 && g.titlePointSize() == 18);
    CHECK(g.yAxis().titlePointSize() == 14 && g.xAxis().textAttributes().font() == 82 && g.yAxis().textAttributes().color() == 4);
    CHECK(g.xAxis().nMajorTicks() == 8 && g.yAxis().titleAlignment() == TextAttributes::kRight);
    CHECK(g.legendTextAttributes().font() == 22 && g.legendPointSize() == 11 && g.legendTextAttributes().justification() == TextAttributes::kCenter);
    CHECK(g.legendFill().style() == 1001 && g.legendFill().color() == 5);
    CHECK(g.legendLine().style() == 2 && g.legendLine().width() == 1.5f && g.legendLine().color() == 3);
    Graph copy(g);
    CHECK(copy.yAxis().titleAlignment() == TextAttributes::kRight && copy.legendTextAttributes().font() == 22);
  }

  // ---- written and read back
  {
    XmlElement e("GraphDefaults");
    defaults.write(e);
    GraphDefaults other;
    other.read(e);
    CHECK(other.title.font == 132 && other.title.size == 18 && other.axisLabels.color == 4 && other.legend.size == 11);
    CHECK(other.nDivisions == 8 && other.axisTitleAlignment == TextAttributes::kRight);
    CHECK(other.legendBackground && other.legendFillColor == 5 && other.legendBorder && other.legendLineStyle == 2);
    CHECK(other.legendLineWidth == 1.5f && other.legendLineColor == 3 && other.legendJustification == TextAttributes::kCenter);
    GraphDefaults untouched;
    untouched.read(XmlElement("GraphDefaults"));     // nothing given: nothing changed
    CHECK(untouched.nDivisions == 5 && untouched.title.font == 0 && !untouched.legendBorder);
  }

  defaults = saved;
  std::printf("TestGraphDefaults: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
