/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestProjector.cpp
//! @brief Test program of the projections of maps in DatumGui (Projector, the range lines of CanvasPane), drawn
//! without a screen (run by ctest).
//
// Dear ImGui runs without a window or renderer; canvas panes are drawn in windows of their own, and the mouse and keys
// are simulated as a user would use them:
//  - the computation: bin ranges snapped to the nearest edges, projections onto x and y, the text of the range;
//  - Project onto X, whole y range: a pane below with the projection, two horizontal lines on the map at the ends;
//  - the upper line dragged by its handle: projected again when the mouse rests, and when it is released, the line then
//    on the edge of the bins summed; Escape puts a line back;
//  - Project onto Y, x range chosen: the range dragged across the map (Escape cancels a choice);
//  - the context menu of the canvas of a projection opened; Save Projection (name typed, Enter), a name already used;
//  - the map no longer shown: the projection stays, without lines; its canvas closed: the projection dropped.
// No frame may show two widgets with the same identifier. Prints each failed check and exits with the number of
// failures (0: all passed).
//
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"
#include "CanvasPane.hpp"
#include "Projector.hpp"
#include "ImGuiPainter.hpp"
#include "Histogram.hpp"
#include "Folder.hpp"
#include "LatexTextures.hpp"

using namespace Datum;
using namespace Datum::GUI;
using Datum::GRAPHICS::Graph;
using Datum::GRAPHICS::GraphItem;
using Datum::GRAPHICS::Point2D;

namespace
{
int nChecks = 0, nFailures = 0, conflicts = 0;

void check(bool ok, const std::string & what, int line)
{
  nChecks++;
  if (ok) return;
  nFailures++;
  std::printf("FAILED (line %d): %s\n", line, what.c_str());
}

#define CHECK(c) check((c), #c, __LINE__)

bool near(double a, double b) { return std::fabs(a - b) <= 1.0E-9*(std::fabs(a) + std::fabs(b) + 1.0); }

//! The panes of a window, as DatumGui keeps them: each drawn in a window of its own, one below the other.
struct Window
{
  std::vector<std::unique_ptr<CanvasPane>> panes;
  int counter = 0;
  Projector projector;
  Window()
  {
    add();
    Projector::Panes p;
    p.find = [this](const std::string & id) -> CanvasPane * { for (auto & q : panes) if (id == q->windowId()) return q.get(); return nullptr; };
    p.addBelow = [this](const std::string &) -> CanvasPane & { return add(); };
    p.close = [this](const std::string & id) { for (std::size_t k=0; k<panes.size(); k++) if (id == panes[k]->windowId()) { panes.erase(panes.begin() + long(k)); return; } };
    projector.setPanes(p);
  }
  CanvasPane & add()
  {
    panes.emplace_back(new CanvasPane(String("Canvas ") + String(std::to_string(++counter))));
    CanvasPane & pane = *panes.back();
    pane.setShowToolbar(false);
    pane.onProject = [this, &pane](int graph, int item, int axis, bool whole) { projector.request(pane, graph, item, axis, whole); };
    pane.onRangeMoved = [this](int id, double low, double high, bool final) { projector.moved(id, low, high, final); };
    pane.contextMenuExtras = [this, &pane](int graph) { projector.contextMenu(pane, graph); };
    return pane;
  }
  //! Window k: 600 x 400 at (0, 410 k).
  void frame(int n=1)
  {
    for (int f=0; f<n; f++)
      {
      ImGui::GetIO().DeltaTime = 1.0f/60.0f;
      ImGui::NewFrame();
      for (std::size_t k=0; k<panes.size(); k++)
        {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 410.0f*float(k)));
        ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
        ImGui::Begin(panes[k]->windowLabel(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        panes[k]->render(600.0f, 400.0f, k == 0, ImGui::GetFont());
        ImGui::End();
        }
      projector.update();
      projector.renderDialogs();
      ImGui::Render();
      if (ImGui::GetCurrentContext()->DebugDrawIdConflictsId != 0) conflicts++;
      }
  }
  //! Screen position of data (x, y) in graph 0 of pane k (as drawn at the last frame).
  ImVec2 screen(int k, double x, double y)
  {
    CanvasPane & pane = *panes[std::size_t(k)];
    float rx, ry, rw, rh;
    pane.canvasRect(rx, ry, rw, rh);
    ImGuiPainter painter(nullptr, nullptr, rx, ry, rw, rh);
    const Graph & g = pane.canvas().graph(0);
    float sx, sy;
    painter.toScreen(g.toCanvas(g.view(), x, y), sx, sy);
    return ImVec2(sx, sy);
  }
  //! The mouse moved to a point (a frame drawn).
  void mouse(ImVec2 p, int frames=1) { ImGui::GetIO().AddMousePosEvent(p.x, p.y); frame(frames); }
  void press(bool down, int button=0) { ImGui::GetIO().AddMouseButtonEvent(button, down); frame(); }
  void key(ImGuiKey k) { ImGui::GetIO().AddKeyEvent(k, true); frame(); ImGui::GetIO().AddKeyEvent(k, false); frame(); }
};

//! A map: 20 bins in x [0, 10], 10 in y [0, 5]; bin (i, j) holds 1 + i + 10 j.
MATH::Histogram map()
{
  MATH::Histogram h("map", "counts", 20, 0.0, 10.0, 10, 0.0, 5.0);
  h.setDimensionTitle(0, "x");
  h.setDimensionTitle(1, "y");
  for (unsigned int i=0; i<20; i++)
    for (unsigned int j=0; j<10; j++)
      {
      double v = 1.0 + i + 10.0*j;
      h.setBinAt(i, j, 1, v, std::sqrt(v));
      }
  return h;
}

bool sameValues(const MATH::Histogram & a, const MATH::Histogram & b)
{
  if (a.nBinsTotal() != b.nBinsTotal()) return false;
  for (unsigned int k=0; k<a.nBinsTotal(); k++) if (!near(a.binValueAtGlobal(k), b.binValueAtGlobal(k))) return false;
  return true;
}

void testComputation()
{
  MATH::Histogram h = map();
  unsigned int first, last;
  Projector::binRange(h, 1, 1.1, 2.6, first, last);           // edges 1.0 and 2.5 (bins 2..4)
  CHECK(first == 2 && last == 4);
  Projector::binRange(h, 1, 2.6, 1.1, first, last);           // reversed
  CHECK(first == 2 && last == 4);
  Projector::binRange(h, 1, 1.1, 1.2, first, last);           // the same edge: one bin
  CHECK(first == 2 && last == 2);
  Projector::binRange(h, 1, 7.0, 9.0, first, last);           // beyond the axis: the last bin
  CHECK(first == 9 && last == 9);
  double lo, hi;
  MATH::Histogram px = Projector::project(h, 0, 1.1, 2.6, lo, hi);
  CHECK(lo == 1.0 && hi == 2.5 && px.rank() == 1 && px.nBins(0) == 20 && px.name() == String("map_px"));
  CHECK(sameValues(px, h.project2DOnto1D(0, 2, 4)) && near(px.binValueAt(3), (4 + 20) + (4 + 30) + (4 + 40)));
  CHECK(px.title().str().find("1 < y < 2.5") != std::string::npos);
  MATH::Histogram py = Projector::project(h, 1, 0.0, 10.0, lo, hi);
  CHECK(lo == 0.0 && hi == 10.0 && py.nBins(0) == 10 && sameValues(py, h.project2DOnto1D(1)) && py.name() == String("map_py"));
}

void testInteraction()
{
  Window w;
  MATH::Histogram h = map();
  CanvasPane & source = *w.panes[0];
  source.divide(1, 1);
  source.canvas().graph(0).show(GraphItem(&h, "map"));
  w.frame(3);
  // ---- onto x, the whole range of y
  CHECK(w.projector.request(source, 0, 0, 0, true));
  w.frame(2);
  CHECK(w.panes.size() == 2 && w.projector.projections().size() == 1);
  if (w.projector.projections().size() != 1) return;
  const Projector::Projection & p = *w.projector.projections()[0];
  CHECK(p.whole && p.low == 0.0 && p.high == 5.0 && p.histogram && sameValues(*p.histogram, h.project2DOnto1D(0)));
  CHECK(w.panes[1]->canvas().graph(0).items().size() == 1 && w.panes[1]->canvas().graph(0).items()[0].data() == p.histogram.get());
  const CanvasPane::RangeMarker * m = source.rangeMarker(p.id);
  CHECK(m && m->axis == 1 && m->low == 0.0 && m->high == 5.0);
  // ---- the upper line dragged by its handle (right end) from y = 5 down to y = 2.6, at rest, then released
  ImVec2 frameRight = w.screen(0, 10.0, 5.0);
  w.mouse(ImVec2(frameRight.x - 2.0f, frameRight.y + 1.0f), 2);
  w.press(true);
  ImVec2 to = w.screen(0, 10.0, 2.6);
  for (int s=1; s<=5; s++) w.mouse(ImVec2(frameRight.x - 2.0f, frameRight.y + (to.y - frameRight.y)*float(s)/5.0f));
  w.frame(30);                                                 // at rest for half a second: projected again
  CHECK(!p.whole && p.low == 0.0 && near(p.high, 2.5) && sameValues(*p.histogram, h.project2DOnto1D(0, 0, 4)));
  CHECK(m && std::fabs(m->high - 2.6) < 0.05);                 // the line stays where it is dragged
  w.press(false);
  m = source.rangeMarker(p.id);
  CHECK(m && near(m->high, 2.5) && m->low == 0.0);             // released: on the edge of the bins summed
  CHECK(p.histogram->title().str().find("0 < y < 2.5") != std::string::npos);
  // ---- the lower line dragged, then Escape: back where it was, projected as before
  ImVec2 lower = w.screen(0, 5.0, 0.0);
  w.mouse(ImVec2(lower.x, lower.y - 1.0f), 2);
  w.press(true);
  w.mouse(w.screen(0, 5.0, 1.5), 3);
  w.key(ImGuiKey_Escape);
  w.press(false);
  m = source.rangeMarker(p.id);
  CHECK(m && m->low == 0.0 && near(m->high, 2.5) && sameValues(*p.histogram, h.project2DOnto1D(0, 0, 4)));
  // a line moved by a program
  source.moveRangeLine(p.id, 0, 1.1);
  CHECK(near(p.low, 1.0) && sameValues(*p.histogram, h.project2DOnto1D(0, 2, 4)));
  // ---- onto y, a range of x chosen with the mouse; Escape cancels a choice
  CHECK(w.projector.request(source, 0, 0, 1, false) && source.selectingRange());
  w.frame();
  w.mouse(w.screen(0, 3.0, 2.0), 2);
  w.key(ImGuiKey_Escape);
  CHECK(!source.selectingRange() && w.projector.projections().size() == 1);
  CHECK(w.projector.request(source, 0, 0, 1, false) && source.selectingRange());
  w.frame();
  w.mouse(w.screen(0, 2.1, 2.0), 2);
  w.press(true);
  for (int s=1; s<=4; s++) w.mouse(w.screen(0, 2.1 + (5.9 - 2.1)*s/4.0, 2.0));
  w.press(false);
  w.frame(2);
  CHECK(!source.selectingRange() && w.projector.projections().size() == 2 && w.panes.size() == 3);
  if (w.projector.projections().size() != 2) return;
  const Projector::Projection & q = *w.projector.projections()[1];
  CHECK(q.axis == 1 && near(q.low, 2.0) && near(q.high, 6.0) && sameValues(*q.histogram, h.project2DOnto1D(1, 4, 11)));
  const CanvasPane::RangeMarker * mq = source.rangeMarker(q.id);
  CHECK(mq && mq->axis == 0 && near(mq->low, 2.0) && near(mq->high, 6.0));
  // ---- the context menu of the canvas of a projection, then Save Projection
  ImVec2 inside = w.screen(1, 5.0, 0.0);
  w.mouse(ImVec2(inside.x, inside.y - 40.0f), 2);
  w.press(true, 1);
  w.press(false, 1);
  w.frame(2);
  w.key(ImGuiKey_Escape);
  Folder folder("saved");
  int asked = 0;
  w.projector.onSave = [&](const MATH::Histogram * histogram, const String & name, String & message)
    {
    if (!histogram) { message = "Added to the folder saved"; return true; }
    asked++;
    if (folder.contains(name)) { message = name + String(" is already there"); return false; }
    folder.add(new MATH::Histogram(*histogram), name);
    message = name + String(" saved");
    return true;
    };
  String message;
  CHECK(w.projector.save(p.id, "slice", message) && folder.get<MATH::Histogram>("slice") != nullptr);
  CHECK(sameValues(*folder.get<MATH::Histogram>("slice"), *p.histogram) && folder.get<MATH::Histogram>("slice")->name() == String("slice"));
  CHECK(!w.projector.save(p.id, "slice", message) && message.str().find("already") != std::string::npos);
  CHECK(!w.projector.save(p.id, "a/b", message) && !w.projector.save(p.id, "  ", message));
  w.projector.openSave(q.id);                                  // the dialog: the name proposed, Enter
  w.frame(3);
  w.key(ImGuiKey_Enter);
  CHECK(folder.get<MATH::Histogram>("map_py") != nullptr && asked == 3);
  // ---- the map no longer shown: the projections stay, without lines
  source.canvas().graph(0).clearItems();
  w.frame(2);
  CHECK(w.projector.projections().size() == 2 && !source.rangeMarker(p.id) && !source.rangeMarker(q.id));
  CHECK(w.projector.projections()[0]->source == nullptr);
  source.moveRangeLine(p.id, 0, 3.0);                          // no line any more: nothing happens
  // ---- a canvas of a projection closed: that projection dropped
  w.panes.erase(w.panes.begin() + 1);
  w.frame(2);
  CHECK(w.projector.projections().size() == 1 && w.projector.projections()[0]->axis == 1);
}
}

namespace Datum
{
  namespace GUI
  {
  // the textures of LaTeX need OpenGL: none here (the text is drawn without them)
  LatexTextures & LatexTextures::instance() { static LatexTextures t; return t; }
  LatexTextures::LatexTextures() : _textures(), _rasterizer(nullptr) {}
  LatexTextures::~LatexTextures() {}
  const LatexTextures::Texture * LatexTextures::texture(const GRAPHICS::LatexImage &, float, int) { return nullptr; }
  void LatexTextures::collect(int) {}
  void LatexTextures::clear() {}
  }
}

int main()
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO & io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(1200, 1300);
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  try
    {
    testComputation();
    testInteraction();
    }
  catch (const std::exception & e)
    {
    std::printf("FAILED: exception: %s\n", e.what());
    nFailures++;
    }
  CHECK(conflicts == 0);
  ImGui::DestroyContext();
  std::printf("TestProjector: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
