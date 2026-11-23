/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestPlotRegroup.cpp
//! @brief Tests of PlotDocument::planRegroup() and regroup(): patterns, labels, limits per graph and per canvas,
//! emptied graphs and canvases, styles kept, undo.
#include <cstdio>
#include <fstream>
#include <string>
#include "PlotDocument.hpp"

using namespace Datum;
using namespace Datum::GRAPHICS;
using Path = PlotDocument::Path;

namespace
{
int nChecks = 0, nFailures = 0;
void check(bool ok, const std::string & what, int line)
{
  nChecks++;
  if (ok) return;
  nFailures++;
  std::printf("FAILED (line %d): %s\n", line, what.c_str());
}
#define CHECK(condition) check((condition), #condition, __LINE__)

const char * kText =
  "<PlotHistos version=\"1\">\n"
  "  <Input id=\"a\" file=\"a.datum\"/>\n"
  "  <Input id=\"b\" file=\"b.datum\"/>\n"
  "  <Window title=\"W\" columns=\"2\" rows=\"1\">\n"
  "    <Canvas name=\"c1\" title=\"C1\" width=\"700\" height=\"500\" columns=\"2\" rows=\"1\" file=\"c1.pdf\">\n"
  "      <Background color=\"white\"/>\n"
  "      <Graph title=\"pip\" legend=\"true\" cell=\"0\">\n"
  "        <Axis which=\"x\" title=\"p_{T}\"/>\n"
  "        <Histogram input=\"a\" path=\"Ana/PiP/n1_pt\"/>\n"
  "        <Histogram input=\"a\" path=\"Ana/PiP/n1_eta\"/>\n"
  "      </Graph>\n"
  "      <Graph title=\"pim\">\n"
  "        <Histogram input=\"a\" path=\"Ana/PiM/n1_pt\"><Marker color=\"red\"/></Histogram>\n"
  "      </Graph>\n"
  "    </Canvas>\n"
  "    <Canvas name=\"c2\">\n"
  "      <Graph>\n"
  "        <Histogram input=\"a\" path=\"Ana/KP/n1_pt\"/>\n"
  "        <Histogram input=\"b\" path=\"Ana/PiP/n1_pt\" label=\"other run\"/>\n"
  "      </Graph>\n"
  "    </Canvas>\n"
  "  </Window>\n"
  "</PlotHistos>\n";

PlotDocument make()
{
  const char * file = "test_regroup.xml";
  std::ofstream out(file);
  out << kText;
  out.close();
  PlotDocument d;
  String error;
  bool ok = d.read(file, error);
  CHECK(ok);
  return d;
}

const XmlElement & windowOf(const PlotDocument & d) { return *d.element(d.children(Path(), PlotDocument::kWindow)[0]); }

std::vector<const XmlElement *> canvases(const PlotDocument & d) { return windowOf(d).children("Canvas"); }

void testPreview()
{
  PlotDocument d = make();
  PlotDocument::RegroupOptions o;
  o.pattern = "*_pt";
  PlotDocument::RegroupPlan plan = d.planRegroup(o);
  CHECK(plan.error.isNull());
  CHECK(plan.entries.size() == 4);
  CHECK(plan.graphs.size() == 1 && plan.canvases.size() == 1);
  CHECK(plan.canvasNames.size() == 1 && plan.canvasNames[0] == "pt");
  CHECK(plan.entries[0].location == "W / c1 / graph 1");
  CHECK(plan.entries[1].location == "W / c1 / graph 2");
  // the wildcard matches "n1" for all: labels from the parts of input/path that differ
  CHECK(plan.entries[0].label == "PiP");
  CHECK(plan.entries[1].label == "PiM");
  CHECK(plan.entries[2].label == "KP");
  CHECK(plan.entries[3].label.isNull());   // has a label of its own
  CHECK(d.version() == 0 || !d.isDirty());   // nothing changed by a preview

  o.pattern = "n1_*";   // wildcards matched by the name; "pt" three times: the parts of the paths that differ
  plan = d.planRegroup(o);
  CHECK(plan.entries.size() == 5);
  CHECK(plan.entries[1].label == "PiP/n1_eta");
  o.pattern = "*/n1_eta; *PiM/n1_pt";   // what the wildcards match is distinct: it is the label
  plan = d.planRegroup(o);
  CHECK(plan.entries.size() == 2 && plan.entries[0].label == "Ana/PiP" && plan.entries[1].label == "Ana");

  o.pattern = "Ana/Pi?/n1_pt";   // with '/': the whole path
  CHECK(d.planRegroup(o).entries.size() == 3);
  o.pattern = "ana/pi?/N1_PT";
  CHECK(d.planRegroup(o).entries.empty());
  o.caseSensitive = false;
  CHECK(d.planRegroup(o).entries.size() == 3);
  o.caseSensitive = true;

  o.pattern = "*KP*; *PiM*";   // several patterns
  CHECK(d.planRegroup(o).entries.size() == 2);

  o.pattern = "Pi[PM]/n1_(pt|eta)";
  o.regularExpression = true;
  CHECK(d.planRegroup(o).entries.size() == 4);
  o.pattern = "(";
  plan = d.planRegroup(o);
  CHECK(!plan.error.isNull() && plan.entries.empty());
  o.regularExpression = false;

  o.pattern = "nothing*";
  CHECK(!d.planRegroup(o).error.isNull());
  o.pattern = "  ";
  CHECK(!d.planRegroup(o).error.isNull());

  // scope: the second canvas only
  o.pattern = "*_pt";
  Path window = d.children(Path(), PlotDocument::kWindow)[0];
  o.scope = d.children(window, PlotDocument::kCanvas)[1];
  plan = d.planRegroup(o);
  CHECK(plan.entries.size() == 2);
  CHECK(plan.entries[0].location == "W / c2 / graph 1");
}

void testRegroup()
{
  PlotDocument d = make();
  PlotDocument::RegroupOptions o;
  o.pattern = "*_pt";
  o.graphTitle = "p_{T} spectra";
  Path c = d.regroup(o);
  CHECK(!c.empty());
  std::vector<const XmlElement *> cs = canvases(d);
  // c1 keeps the eta histogram; its emptied graph and the emptied c2 are removed; the new canvas comes after c1
  CHECK(cs.size() == 2);
  CHECK(cs[0]->attribute("name") == "c1" && cs[0]->children("Graph").size() == 1);
  const XmlElement * pt = cs[1];
  CHECK(d.element(c) == pt);
  CHECK(pt->attribute("name") == "pt" && pt->attribute("width") == "700" && pt->child("Background") != nullptr);
  CHECK(!pt->hasAttribute("columns"));
  std::vector<const XmlElement *> graphs = pt->children("Graph");
  CHECK(graphs.size() == 1);
  const XmlElement & g = *graphs[0];
  CHECK(g.attribute("title") == "p_{T} spectra" && g.attribute("legend") == "true" && !g.hasAttribute("cell"));
  CHECK(g.child("Axis") != nullptr);
  std::vector<const XmlElement *> items = g.children("Histogram");
  CHECK(items.size() == 4);
  CHECK(items[0]->attribute("label") == "PiP" && items[3]->attribute("label") == "other run");
  CHECK(items[1]->child("Marker") != nullptr);   // style of the item kept
  // grids fitted: c1 (2 x 1) holds one graph now; the window (2 x 1) holds two canvases
  CHECK(cs[0]->attribute("columns") == "1" && cs[0]->attribute("rows") == "1");
  CHECK(windowOf(d).attribute("columns") == "2" && windowOf(d).attribute("rows") == "1");
  CHECK(d.isDirty());
  CHECK(d.undo());
  CHECK(canvases(d).size() == 2 && canvases(d)[1]->attribute("name") == "c2");

  // at most 2 per graph and 1 graph per canvas: 2 canvases; the window grid grows
  o.itemsPerGraph = 2;
  o.graphsPerCanvas = 1;
  o.canvasName = "spectra";
  PlotDocument::RegroupPlan plan;
  c = d.regroup(o, &plan);
  CHECK(plan.canvasNames.size() == 2 && plan.canvasNames[0] == "spectra" && plan.canvasNames[1] == "spectra_2");
  cs = canvases(d);
  CHECK(cs.size() == 3);
  CHECK(cs[1]->attribute("name") == "spectra" && cs[2]->attribute("name") == "spectra_2");
  CHECK(cs[1]->children("Graph")[0]->children("Histogram").size() == 2);
  CHECK(windowOf(d).attribute("rows") == "2");
  CHECK(d.undo());

  // 3 per graph, 2 graphs per canvas: one canvas of two graphs; empty graphs kept when asked
  o.itemsPerGraph = 3;
  o.graphsPerCanvas = 2;
  o.canvasName = "";
  o.removeEmpty = false;
  d.regroup(o);
  cs = canvases(d);
  CHECK(cs.size() == 3);
  CHECK(cs[1]->children("Graph").size() == 2);
  CHECK(cs[0]->children("Graph").size() == 2 && cs[2]->children("Graph").size() == 1);
  CHECK(d.undo());

  // one graph per canvas, two canvases per window: four canvases in two new windows after W
  o.itemsPerGraph = 1;
  o.graphsPerCanvas = 1;
  o.canvasesPerWindow = 2;
  o.removeEmpty = true;
  PlotDocument::RegroupPlan plan2;
  c = d.regroup(o, &plan2);
  std::vector<Path> windows = d.children(Path(), PlotDocument::kWindow);
  CHECK(plan2.windows.size() == 2 && windows.size() == 3);
  CHECK(d.element(windows[1])->attribute("title") == "pt" && d.element(windows[2])->attribute("title") == "pt 2");
  CHECK(d.element(windows[1])->children("Canvas").size() == 2 && d.element(windows[2])->children("Canvas").size() == 2);
  CHECK(c.size() == 2 && c[0] == windows[1][0] && c[1] == 0);   // the first new canvas
  CHECK(d.element(windows[0])->children("Canvas").size() == 1);   // W keeps c1 only
  CHECK(d.element(windows[0])->attribute("columns") == "1");
  CHECK(d.undo());
}
//! A canvas moved out of a window, a graph out of a canvas: the grids and sizes shrink to what is left
void testShrink()
{
  PlotDocument d = make();
  Path w = d.children(Path(), PlotDocument::kWindow)[0];
  XmlElement * window = d.element(w);
  window->setAttribute("width", 1240);
  window->setAttribute("height", 520);
  Path second = d.addWindow();
  std::vector<Path> canvasPaths = d.children(w, PlotDocument::kCanvas);
  bool ok = false;
  d.move(canvasPaths[1], second, ok);               // c2 to the new window
  CHECK(ok);
  window = d.element(w);
  CHECK(window->attribute("columns") == "1" && window->attribute("rows") == "1");
  CHECK(window->attribute("width") == "620");        // one cell of 620 left
  CHECK(window->attribute("height") == "520");       // one row: unchanged
  // a graph removed from c1 (2 x 1, 700 x 500): 1 x 1, 350 x 500
  Path c1 = d.children(w, PlotDocument::kCanvas)[0];
  CHECK(d.remove(d.children(c1, PlotDocument::kGraph)[1]));
  const XmlElement * canvas = d.element(c1);
  CHECK(canvas->attribute("columns") == "1" && canvas->attribute("width") == "350");
  CHECK(canvas->attribute("height") == "500");
  // moved back: the window grows again (2 x 1, 1240 x 520)
  Path back = d.children(second, PlotDocument::kCanvas)[0];
  d.move(back, w, ok);
  CHECK(ok);
  window = d.element(w);
  CHECK(window->attribute("columns") == "2" && window->attribute("rows") == "1");
  CHECK(window->attribute("width") == "1240" && window->attribute("height") == "520");
  CHECK(d.undo() && d.element(w)->attribute("columns") == "1");
}
} // namespace

int main()
{
  testPreview();
  testRegroup();
  testShrink();
  std::remove("test_regroup.xml");
  std::printf("%d/%d checks passed\n", nChecks - nFailures, nChecks);
  return nFailures == 0 ? 0 : 1;
}
