/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file Extractor.cpp
//! @brief Extractor: segments of the histograms of a graph of DatumGui (1D) or regions of its maps (2D), between lines
//! that can be dragged.
#include "Extractor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include "CanvasPane.hpp"
#include "Exceptions.hpp"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace Datum
{
  namespace GUI
  {
  using GRAPHICS::Graph;
  using GRAPHICS::GraphItem;

  // ---------------------------------------------------------------------------------------------- the computation

  MATH::Histogram Extractor::extract(const MATH::Histogram & h, const double low[2], const double high[2],
                                     double lowEdge[2], double highEdge[2])
  {
  unsigned int rank = h.rank();
  if (rank != 1 && rank != 2) throw Exception("An extract is made of a histogram of rank 1 or 2", "Extractor::extract()");
  unsigned int first[2] = { 0, 0 }, last[2] = { 0, 0 };
  for (unsigned int a=0; a<rank; a++)
    {
    Projector::binRange(h, a, low[a], high[a], first[a], last[a]);
    std::vector<double> edges = h.binEdges(a);
    lowEdge[a] = edges[first[a]];
    highEdge[a] = edges[last[a] + 1];
    }
  MATH::Histogram e = rank == 1 ? h.extract1(first[0], last[0]) : h.extract2(first[0], last[0], first[1], last[1]);
  e.setName(h.name() + String("_extract"));
  String what = h.title().isNull() ? h.name() : h.title();
  e.setTitle(what + String(", ") + rangeText(h, lowEdge, highEdge));
  for (unsigned int a=0; a<rank; a++) e.setDimensionTitle(a, h.dimensionTitle(a));
  return e;
  }

  String Extractor::rangeText(const MATH::Histogram & h, const double low[2], const double high[2])
  {
  std::string text;
  for (unsigned int a=0; a<h.rank() && a<2; a++)
    {
    String name = h.dimensionTitle(a);
    if (name.isNull() || name.length() > 24) name = a == 0 ? "x" : "y";
    char part[160];
    std::snprintf(part, sizeof(part), "%.4g < %s < %.4g", low[a], name.data(), high[a]);
    text += (text.empty() ? "" : ", ") + std::string(part);
    }
  return String(text);
  }

  // ---------------------------------------------------------------------------------------------- extractions

  namespace
  {
  //! The histograms of a graph that an extraction takes: the maps (plane view), else the histograms of rank 1.
  std::vector<std::size_t> histogramsOf(const Graph & g, unsigned int & rank)
  {
    std::vector<std::size_t> maps, curves;
    for (std::size_t k=0; k<g.items().size(); k++)
      {
      const GraphItem & item = g.items()[k];
      const MATH::Histogram * h = item.kind() == GraphItem::kData ? dynamic_cast<const MATH::Histogram *>(item.data()) : nullptr;
      if (!h) continue;
      if (h->rank() == 2 && !g.is3D()) maps.push_back(k);
      else if (h->rank() == 1) curves.push_back(k);
      }
    rank = maps.empty() ? 1 : 2;
    return maps.empty() ? curves : maps;
  }
  }

  CanvasPane * Extractor::paneOf(const std::string & windowId)
  {
  return windowId.empty() || !_panes.find ? nullptr : _panes.find(windowId);
  }

  Extractor::Extraction * Extractor::extraction(int id)
  {
  for (auto & e : _extractions) if (e->id == id) return e.get();
  return nullptr;
  }

  bool Extractor::extractable(CanvasPane & pane, int graph)
  {
  if (graph < 0 || graph >= pane.canvas().nGraphs()) return false;
  unsigned int rank;
  return !histogramsOf(pane.canvas().graph(graph), rank).empty();
  }

  bool Extractor::sourcesShown(const Extraction & e)
  {
  if (e.sources.empty()) return false;
  CanvasPane * pane = paneOf(e.sourcePane);
  if (!pane || e.graph < 0 || e.graph >= pane->canvas().nGraphs()) return false;
  const Graph & g = pane->canvas().graph(e.graph);
  if (e.rank == 2 && g.is3D()) return false;
  for (const MATH::Histogram * h : e.sources)
    {
    bool found = false;
    for (const GraphItem & item : g.items()) if (item.data() == h) found = true;   // pointers compared only
    if (!found) return false;
    }
  return true;
  }

  void Extractor::mark(Extraction & e)
  {
  CanvasPane * pane = paneOf(e.sourcePane);
  if (!pane) return;
  for (unsigned int a=0; a<e.rank; a++)
    {
    CanvasPane::RangeMarker m;
    m.id = markerId(e.id, int(a));
    m.graph = e.graph;
    m.axis = int(a);
    m.low = e.low[a];
    m.high = e.high[a];
    pane->setRangeMarker(m);
    }
  }

  void Extractor::unmark(Extraction & e)
  {
  CanvasPane * pane = paneOf(e.sourcePane);
  if (!pane) return;
  for (int a=0; a<2; a++) pane->removeRangeMarker(markerId(e.id, a));
  }

  void Extractor::refresh(Extraction & e)
  {
  if (e.sources.empty()) return;
  double low[2] = { e.low[0], e.low[1] }, high[2] = { e.high[0], e.high[1] };
  for (std::size_t k=0; k<e.sources.size(); k++)
    {
    double lowEdge[2] = { 0.0, 0.0 }, highEdge[2] = { 0.0, 0.0 };
    MATH::Histogram x = extract(*e.sources[k], low, high, lowEdge, highEdge);
    if (k == 0)                                                // the ranges: on the bins of the first histogram
      for (unsigned int a=0; a<e.rank; a++) { e.low[a] = lowEdge[a]; e.high[a] = highEdge[a]; }
    if (k < e.histograms.size()) *e.histograms[k] = x;         // the same objects: the graph shows them
    else e.histograms.emplace_back(new MATH::Histogram(x));
    }
  CanvasPane * target = paneOf(e.targetPane);
  if (!target || target->canvas().nGraphs() == 0) return;
  Graph & g = target->canvas().graph(0);
  g.clearItems();
  for (std::size_t k=0; k<e.histograms.size(); k++) g.show(GraphItem(e.histograms[k].get(), e.labels[k]));
  }

  bool Extractor::request(CanvasPane & pane, int graph)
  {
  if (graph < 0 || graph >= pane.canvas().nGraphs()) return false;
  const Graph & g = pane.canvas().graph(graph);
  unsigned int rank;
  std::vector<std::size_t> items = histogramsOf(g, rank);
  if (items.empty()) return false;
  std::string source = pane.windowId();
  Extraction * e = nullptr;
  for (auto & x : _extractions) if (x->sourcePane == source && x->graph == graph && !x->sources.empty()) e = x.get();
  if (e && paneOf(e->targetPane) && sourcesShown(*e))         // already shown: its lines shown again
    {
    mark(*e);
    return true;
    }
  if (!e)
    {
    _extractions.emplace_back(new Extraction());
    e = _extractions.back().get();
    e->id = _nextId++;
    e->sourcePane = source;
    e->graph = graph;
    }
  e->rank = rank;
  e->sources.clear();
  e->labels.clear();
  for (std::size_t k : items)
    {
    const GraphItem & item = g.items()[k];
    const MATH::Histogram * h = dynamic_cast<const MATH::Histogram *>(item.data());
    e->sources.push_back(h);
    e->labels.push_back(item.label().isNull() ? h->name() : item.label());
    }
  // the lines at a quarter and three quarters of the ranges shown (in the scale of the axis)
  const Graph::View view = g.view();
  for (unsigned int a=0; a<rank; a++)
    {
    double lo = view.minimum(int(a)), hi = view.maximum(int(a));
    if (view.log(int(a)) && lo > 0.0 && hi > 0.0)
      {
      double l0 = std::log10(lo), l1 = std::log10(hi);
      e->low[a] = std::pow(10.0, l0 + 0.25*(l1 - l0));
      e->high[a] = std::pow(10.0, l0 + 0.75*(l1 - l0));
      }
    else
      {
      e->low[a] = lo + 0.25*(hi - lo);
      e->high[a] = lo + 0.75*(hi - lo);
      }
    }
  if (!paneOf(e->targetPane))
    {
    if (!_panes.addBelow) return false;
    CanvasPane & target = _panes.addBelow(source);
    target.divide(1, 1);
    target.setSelectedGraph(0);
    target.setTitle(String("Extract of ") + (e->labels.size() == 1 ? e->labels[0] : String("the histograms of a graph")));
    e->targetPane = target.windowId();
    }
  try { refresh(*e); }
  catch (const std::exception & x) { if (onStatus) onStatus(String("No extract: ") + String(x.what())); return false; }
  mark(*e);
  if (onStatus)
    onStatus(String("Extract: ") + rangeText(*e->sources[0], e->low, e->high)
             + String(rank == 1 ? ": drag the two lines to change the range" : ": drag the lines to change the ranges")
             + String("; right-click the extract to save or cancel it"));
  return true;
  }

  bool Extractor::moved(int marker, double low, double high, bool final)
  {
  if (marker < kMarkerBase) return false;
  int id = (marker - kMarkerBase)/2, axis = (marker - kMarkerBase)%2;
  Extraction * e = extraction(id);
  if (!e || !sourcesShown(*e) || unsigned(axis) >= e->rank) return true;
  e->low[axis] = low;
  e->high[axis] = high;
  try { refresh(*e); }
  catch (const std::exception & x) { if (onStatus) onStatus(String("No extract: ") + String(x.what())); return true; }
  if (final)
    {
    mark(*e);                                                  // the lines on the edges of the bins kept
    if (onStatus) onStatus(String("Extract: ") + rangeText(*e->sources[0], e->low, e->high));
    }
  return true;
  }

  void Extractor::update()
  {
  for (std::size_t k=_extractions.size(); k-- > 0; )
    {
    Extraction & e = *_extractions[k];
    CanvasPane * target = paneOf(e.targetPane);
    bool shown = false;
    if (target && target->canvas().nGraphs() > 0 && !e.histograms.empty())
      for (const GraphItem & item : target->canvas().graph(0).items()) if (item.data() == e.histograms[0].get()) shown = true;
    if (!shown)                                                // its canvas closed or showing something else: dropped
      {
      unmark(e);
      if (_saveId == e.id) _saveId = 0;
      for (auto & h : e.histograms) _retired.push_back(std::move(h));
      _extractions.erase(_extractions.begin() + long(k));
      continue;
      }
    if (e.sources.empty()) continue;
    if (!sourcesShown(e)) { e.sources.clear(); unmark(e); continue; }   // the extract stays as it is
    CanvasPane * source = paneOf(e.sourcePane);
    if (source && !source->rangeMarker(markerId(e.id, 0))) mark(e);     // the canvas was drawn again
    }
  }

  void Extractor::cancel(int id)
  {
  Extraction * e = extraction(id);
  if (!e) return;
  unmark(*e);
  e->sources.clear();
  if (_panes.close) _panes.close(e->targetPane);
  if (onStatus) onStatus("Extraction cancelled: nothing saved");
  }

  void Extractor::contextMenu(CanvasPane & pane, int graph)
  {
  for (auto & x : _extractions)
    {
    Extraction & e = *x;
    if (e.targetPane == pane.windowId())
      {
      ImGui::Separator();
      if (!e.histograms.empty()) ImGui::TextDisabled("%s", e.histograms[0]->title().data());
      if (ImGui::MenuItem("Save Extraction...")) openSave(e.id);
      ImGui::SetItemTooltip("Add the extract (each histogram under a name) to the folder selected in the browser (a new "
                            "data file if that folder cannot be changed)");
      if (ImGui::MenuItem("Cancel Extraction")) cancel(e.id);
      ImGui::SetItemTooltip("Remove the lines and close this canvas: nothing is saved");
      return;
      }
    if (e.sourcePane == pane.windowId() && e.graph == graph && !e.sources.empty())
      {
      ImGui::Separator();
      if (ImGui::MenuItem("Cancel Extraction")) cancel(e.id);
      ImGui::SetItemTooltip("Remove the lines and close the canvas of the extract: nothing is saved");
      return;
      }
    }
  }

  void Extractor::openSave(int id)
  {
  Extraction * e = extraction(id);
  if (!e || e->histograms.empty()) return;
  _saveId = id;
  _saveRequested = true;
  _saveNames.clear();
  for (auto & h : e->histograms) _saveNames.push_back(h->name().str());
  _saveMessage = String();
  }

  bool Extractor::save(int id, const std::vector<String> & names, String & message)
  {
  Extraction * e = extraction(id);
  if (!e || e->histograms.empty()) { message = "The extract is no longer shown"; return false; }
  if (names.size() != e->histograms.size()) { message = "One name per histogram is needed"; return false; }
  if (!onSave) { message = "Nowhere to save it"; return false; }
  std::vector<String> clean;
  std::set<std::string> seen;
  for (const String & name : names)                           // every name checked first: all saved, or none
    {
    std::string n = name.str();
    std::size_t a = n.find_first_not_of(" \t"), b = n.find_last_not_of(" \t");
    n = a == std::string::npos ? std::string() : n.substr(a, b - a + 1);
    if (n.empty()) { message = "Give every histogram a name"; return false; }
    if (n.find('/') != std::string::npos) { message = String("A name cannot contain '/': ") + String(n); return false; }
    if (!seen.insert(n).second) { message = String("The name ") + String(n) + String(" is given twice"); return false; }
    String where;
    if (!onSave(nullptr, String(n), where)) { message = where; return false; }
    clean.push_back(String(n));
    }
  String last;
  for (std::size_t k=0; k<clean.size(); k++)
    {
    MATH::Histogram h(*e->histograms[k]);
    h.setName(clean[k]);
    if (!onSave(&h, clean[k], last)) { message = last; return false; }
    }
  message = clean.size() == 1 ? last : String(std::to_string(clean.size()) + " histograms saved: ") + last;
  return true;
  }

  void Extractor::renderDialogs()
  {
  if (_saveRequested) { ImGui::OpenPopup("Save Extraction"); _saveRequested = false; }
  if (!ImGui::BeginPopupModal("Save Extraction", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  Extraction * e = extraction(_saveId);
  if (!e || e->histograms.empty() || _saveNames.size() != e->histograms.size())
    {
    ImGui::TextDisabled("The extract is no longer shown.");
    if (ImGui::Button("Close", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
    }
  ImGui::TextUnformatted(rangeText(*e->histograms[0], e->low, e->high).data());
  ImGui::Spacing();
  bool enter = false;
  for (std::size_t k=0; k<_saveNames.size(); k++)
    {
    ImGui::PushID(int(k));
    if (k == 0 && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(300);
    enter |= ImGui::InputText("##name", &_saveNames[k], ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::TextDisabled("name of the extract of %s", e->labels[k].data());
    ImGui::PopID();
    }
  String where;
  if (onSave && !_saveNames.empty()) onSave(nullptr, String(_saveNames[0]), where);
  if (!where.isNull()) ImGui::TextDisabled("%s", where.data());
  if (!_saveMessage.isNull()) ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.0f), "%s", _saveMessage.data());
  if (ImGui::Button("OK", ImVec2(110, 0)) || enter)
    {
    std::vector<String> names;
    for (const auto & n : _saveNames) names.push_back(String(n));
    String message;
    if (save(_saveId, names, message))
      {
      if (onStatus) onStatus(message);
      _saveId = 0;
      ImGui::CloseCurrentPopup();
      }
    else _saveMessage = message;
    }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { _saveId = 0; ImGui::CloseCurrentPopup(); }
  ImGui::EndPopup();
  }

  } // namespace GUI
} // namespace Datum
