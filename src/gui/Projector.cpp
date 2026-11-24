/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file Projector.cpp
//! @brief Projector: projections of maps (histograms of rank 2) onto x or y in DatumGui, over a slice that can be dragged.
#include "Projector.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "CanvasPane.hpp"
#include "Profile.hpp"
#include "Exceptions.hpp"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace Datum
{
  namespace GUI
  {
  using GRAPHICS::Graph;
  using GRAPHICS::GraphItem;

  namespace
  {
  //! The index of the edge nearest to a value.
  unsigned int nearestEdge(const std::vector<double> & edges, double x)
  {
    unsigned int best = 0;
    for (unsigned int k=1; k<edges.size(); k++) if (std::fabs(edges[k] - x) < std::fabs(edges[best] - x)) best = k;
    return best;
  }
  }

  // ---------------------------------------------------------------------------------------------- the computation

  void Projector::binRange(const MATH::Histogram & h, unsigned int axis, double low, double high, unsigned int & first,
                           unsigned int & last)
  {
  if (low > high) std::swap(low, high);
  std::vector<double> edges = h.binEdges(axis);
  unsigned int n = unsigned(edges.size()) - 1;
  unsigned int a = nearestEdge(edges, low), b = nearestEdge(edges, high);
  if (b <= a) { if (a >= n) a = n - 1; b = a + 1; }   // at least one bin
  first = a;
  last = b - 1;
  }

  MATH::Histogram Projector::project(const MATH::Histogram & h, int keptAxis, double low, double high, double & lowEdge,
                                     double & highEdge)
  {
  if (h.rank() != 2) throw Exception("A projection is made of a histogram of rank 2", "Projector::project()");
  unsigned int kept = keptAxis == 0 ? 0u : 1u, summed = 1u - kept;
  unsigned int first, last;
  binRange(h, summed, low, high, first, last);
  std::vector<double> edges = h.binEdges(summed);
  lowEdge = edges[first];
  highEdge = edges[last + 1];
  MATH::Histogram p = h.project2DOnto1D(kept, first, last);
  p.setName(h.name() + String(kept == 0 ? "_px" : "_py"));
  String what = h.title().isNull() ? h.name() : h.title();
  p.setTitle(what + String(", ") + rangeText(h, keptAxis, lowEdge, highEdge));
  return p;
  }

  String Projector::rangeText(const MATH::Histogram & h, int keptAxis, double low, double high)
  {
  unsigned int summed = keptAxis == 0 ? 1u : 0u;
  String name = h.dimensionTitle(summed);
  if (name.isNull() || name.length() > 24) name = summed == 0 ? "x" : "y";
  char text[160];
  std::snprintf(text, sizeof(text), "%.4g < %s < %.4g", low, name.data(), high);
  return String(text);
  }

  // ---------------------------------------------------------------------------------------------- projections

  CanvasPane * Projector::paneOf(const std::string & windowId)
  {
  return windowId.empty() || !_panes.find ? nullptr : _panes.find(windowId);
  }

  Projector::Projection * Projector::projection(int id)
  {
  for (auto & p : _projections) if (p->id == id) return p.get();
  return nullptr;
  }

  Projector::Projection * Projector::projectionIn(const CanvasPane & pane)
  {
  for (auto & p : _projections) if (p->targetPane == pane.windowId()) return p.get();
  return nullptr;
  }

  bool Projector::sourceShown(const Projection & p)
  {
  if (!p.source) return false;
  CanvasPane * pane = const_cast<Projector *>(this)->paneOf(p.sourcePane);
  if (!pane || p.graph < 0 || p.graph >= pane->canvas().nGraphs()) return false;
  const Graph & g = pane->canvas().graph(p.graph);
  if (g.is3D()) return false;
  for (const GraphItem & item : g.items()) if (item.data() == p.source) return true;   // pointers compared only
  return false;
  }

  void Projector::mark(Projection & p)
  {
  CanvasPane * pane = paneOf(p.sourcePane);
  if (!pane) return;
  CanvasPane::RangeMarker m;
  m.id = p.id;
  m.graph = p.graph;
  m.axis = p.axis == 0 ? 1 : 0;   // onto x: a range of y, horizontal lines
  m.low = p.low;
  m.high = p.high;
  pane->setRangeMarker(m);
  }

  void Projector::refresh(Projection & p)
  {
  if (!p.source) return;
  double lowEdge, highEdge;
  MATH::Histogram r = project(*p.source, p.axis, p.low, p.high, lowEdge, highEdge);
  p.low = lowEdge;
  p.high = highEdge;
  unsigned int summed = p.axis == 0 ? 1u : 0u;
  std::vector<double> edges = p.source->binEdges(summed);
  p.whole = lowEdge <= edges.front() && highEdge >= edges.back();
  if (!p.histogram) p.histogram.reset(new MATH::Histogram(r));
  else *p.histogram = r;                                       // the same object: the graph shows it
  CanvasPane * target = paneOf(p.targetPane);
  if (!target || target->canvas().nGraphs() == 0) return;
  Graph & g = target->canvas().graph(0);
  g.clearItems();
  g.show(GraphItem(p.histogram.get(), p.label));               // titles from the projection: the range in the title
  }

  bool Projector::request(CanvasPane & pane, int graph, int item, int axis, bool whole)
  {
  if (graph < 0 || graph >= pane.canvas().nGraphs()) return false;
  const Graph & g = pane.canvas().graph(graph);
  if (item < 0 || item >= int(g.items().size()) || g.is3D()) return false;
  const GraphItem & shown = g.items()[std::size_t(item)];
  const MATH::Histogram * h = dynamic_cast<const MATH::Histogram *>(shown.data());
  if (!h || h->rank() != 2 || dynamic_cast<const MATH::Profile *>(h)) return false;
  std::string source = pane.windowId();
  String label = shown.label().isNull() ? h->name() : shown.label();
  int kept = axis == 0 ? 0 : 1;
  // made (or the projection of the same map onto the same axis replaced) once the range is known
  auto make = [this, source, graph, h, label, kept](double low, double high)
    {
    Projection * p = nullptr;
    for (auto & q : _projections)
      if (q->sourcePane == source && q->graph == graph && q->source == h && q->axis == kept) p = q.get();
    if (!p)
      {
      _projections.emplace_back(new Projection());
      p = _projections.back().get();
      p->id = _nextId++;
      p->sourcePane = source;
      p->graph = graph;
      p->source = h;
      p->axis = kept;
      }
    p->label = label;
    p->low = low;
    p->high = high;
    if (!sourceShown(*p)) { if (onStatus) onStatus("The map is no longer shown: no projection"); return; }
    if (!paneOf(p->targetPane))
      {
      if (!_panes.addBelow) return;
      CanvasPane & target = _panes.addBelow(source);
      target.divide(1, 1);
      target.setSelectedGraph(0);
      target.setTitle(String("Projection of ") + label + String(kept == 0 ? " onto x" : " onto y"));
      p->targetPane = target.windowId();
      }
    try { refresh(*p); }
    catch (const std::exception & e) { if (onStatus) onStatus(String("No projection: ") + String(e.what())); return; }
    mark(*p);
    if (onStatus)
      onStatus(String("Projection of ") + label + String(kept == 0 ? " onto x, " : " onto y, ")
               + rangeText(*h, kept, p->low, p->high) + String(": drag the lines on the map to change the range"));
    };
  unsigned int summed = kept == 0 ? 1u : 0u;
  std::vector<double> edges = h->binEdges(summed);
  if (whole) { make(edges.front(), edges.back()); return true; }
  pane.selectRange(graph, kept == 0 ? 1 : 0, make);
  if (onStatus) onStatus(String("Drag across the map ") + String(kept == 0 ? "up or down to choose the range of y"
                                                                           : "left or right to choose the range of x")
                         + String(" summed (Escape: cancel)"));
  return true;
  }

  void Projector::moved(int id, double low, double high, bool final)
  {
  Projection * p = projection(id);
  if (!p || !sourceShown(*p)) return;
  p->low = low;
  p->high = high;
  try { refresh(*p); }
  catch (const std::exception & e) { if (onStatus) onStatus(String("No projection: ") + String(e.what())); return; }
  if (final)
    {
    mark(*p);                                                  // the lines on the edges of the bins summed
    if (onStatus) onStatus(String("Projection of ") + p->label + String(": ") + rangeText(*p->source, p->axis, p->low, p->high));
    }
  }

  void Projector::update()
  {
  for (std::size_t k=_projections.size(); k-- > 0; )
    {
    Projection & p = *_projections[k];
    CanvasPane * target = paneOf(p.targetPane);
    bool shown = false;
    if (target && target->canvas().nGraphs() > 0)
      for (const GraphItem & item : target->canvas().graph(0).items()) if (item.data() == p.histogram.get()) shown = true;
    CanvasPane * source = paneOf(p.sourcePane);
    if (!shown)                                                // its canvas closed or showing something else: dropped
      {
      if (source) source->removeRangeMarker(p.id);
      if (_saveId == p.id) _saveId = 0;
      if (p.histogram) _retired.push_back(std::move(p.histogram));
      _projections.erase(_projections.begin() + long(k));
      continue;
      }
    if (!p.source) continue;
    if (!sourceShown(p))                                       // the map is no longer shown: the projection stays as is
      {
      p.source = nullptr;
      if (source) source->removeRangeMarker(p.id);
      continue;
      }
    if (!source->rangeMarker(p.id)) mark(p);                   // the canvas of the map was drawn again
    }
  }

  void Projector::contextMenu(CanvasPane & pane, int /*graph*/)
  {
  Projection * p = projectionIn(pane);
  if (!p) return;
  ImGui::Separator();
  if (p->histogram) ImGui::TextDisabled("%s", p->histogram->title().data());
  if (ImGui::MenuItem("Save Projection...")) openSave(p->id);
  ImGui::SetItemTooltip("Add the projection, under a name, to the folder selected in the browser (a new data file if "
                        "that folder cannot be changed)");
  if (ImGui::MenuItem("Close Projection"))
    {
    if (CanvasPane * source = paneOf(p->sourcePane)) source->removeRangeMarker(p->id);
    if (_panes.close) _panes.close(p->targetPane);
    }
  }

  void Projector::openSave(int id)
  {
  Projection * p = projection(id);
  if (!p || !p->histogram) return;
  _saveId = id;
  _saveRequested = true;
  _saveName = p->histogram->name().str();
  _saveMessage = String();
  _saveError = false;
  }

  bool Projector::save(int id, const String & name, String & message)
  {
  Projection * p = projection(id);
  if (!p || !p->histogram) { message = "The projection is no longer shown"; return false; }
  std::string n = name.str();
  std::size_t a = n.find_first_not_of(" \t"), b = n.find_last_not_of(" \t");
  n = a == std::string::npos ? std::string() : n.substr(a, b - a + 1);
  if (n.empty()) { message = "Give the projection a name"; return false; }
  if (n.find('/') != std::string::npos) { message = "A name cannot contain '/'"; return false; }
  if (!onSave) { message = "Nowhere to save it"; return false; }
  MATH::Histogram h(*p->histogram);
  h.setName(String(n));
  return onSave(&h, String(n), message);
  }

  void Projector::renderDialogs()
  {
  if (_saveRequested) { ImGui::OpenPopup("Save Projection"); _saveRequested = false; }
  if (!ImGui::BeginPopupModal("Save Projection", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  Projection * p = projection(_saveId);
  if (!p || !p->histogram)
    {
    ImGui::TextDisabled("The projection is no longer shown.");
    if (ImGui::Button("Close", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
    }
  ImGui::TextUnformatted(p->histogram->title().data());
  ImGui::Spacing();
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(360);
  bool enter = ImGui::InputText("name", &_saveName, ImGuiInputTextFlags_EnterReturnsTrue);
  String where;
  if (onSave) onSave(nullptr, String(_saveName), where);       // where it goes
  if (!where.isNull()) ImGui::TextDisabled("%s", where.data());
  if (!_saveMessage.isNull())
    {
    if (_saveError) ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.0f), "%s", _saveMessage.data());
    else ImGui::TextDisabled("%s", _saveMessage.data());
    }
  if (ImGui::Button("OK", ImVec2(110, 0)) || enter)
    {
    String message;
    if (save(_saveId, String(_saveName), message))
      {
      if (onStatus) onStatus(message);
      _saveId = 0;
      ImGui::CloseCurrentPopup();
      }
    else { _saveMessage = message; _saveError = true; }
    }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { _saveId = 0; ImGui::CloseCurrentPopup(); }
  ImGui::EndPopup();
  }

  } // namespace GUI
} // namespace Datum
