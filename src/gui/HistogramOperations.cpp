/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file HistogramOperations.cpp
//! @brief HistogramOperations: the Operations menu of DatumGui (sum, difference, product, ratio, scaling of histograms).
#include "HistogramOperations.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include "CanvasPane.hpp"
#include "Profile.hpp"
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
  std::string number(double c)
  {
    char text[32];
    std::snprintf(text, sizeof(text), "%g", c);
    return text;
  }

  //! "A", "2 A", "-A" (a coefficient of 1 not written).
  std::string term(double c, const String & label)
  {
    if (c == 1.0) return label.str();
    if (c == -1.0) return "-" + label.str();
    return number(c) + " " + label.str();
  }

  //! A factor of a product or a ratio: "A", or "(2 A)".
  std::string factor(double c, const String & label)
  {
    return c == 1.0 ? label.str() : "(" + term(c, label) + ")";
  }

  //! A name usable in a folder, from a label.
  std::string nameOf(const String & label)
  {
    std::string s;
    for (char c : label.str()) s += (std::isalnum((unsigned char) c) || c == '_' || c == '-' || c == '.') ? c : '_';
    return s.empty() ? std::string("h") : s;
  }
  }

  // ---------------------------------------------------------------------------------------------- the computation

  const char * HistogramOperations::name(Operation op)
  {
  switch (op)
    {
    case kSum:        return "Sum";
    case kDifference: return "Difference";
    case kProduct:    return "Product";
    case kRatio:      return "Ratio";
    case kScale:      return "Scale";
    }
  return "Operation";
  }

  String HistogramOperations::expression(Operation op, const std::vector<String> & labels, const std::vector<double> & c)
  {
  std::size_t n = std::min(labels.size(), c.size());
  if (n == 0) return String();
  std::string s;
  switch (op)
    {
    case kSum:
      s = term(c[0], labels[0]);
      for (std::size_t k=1; k<n; k++) s += (c[k] < 0 ? " - " : " + ") + term(std::fabs(c[k]), labels[k]);
      break;
    case kDifference:
      s = term(c[0], labels[0]);
      if (n > 1) s += (c[1] < 0 ? " + " : " - ") + term(std::fabs(c[1]), labels[1]);
      break;
    case kProduct:
      s = factor(c[0], labels[0]);
      if (n > 1) s += " x " + factor(c[1], labels[1]);
      break;
    case kRatio:
      s = factor(c[0], labels[0]);
      if (n > 1) s += " / " + factor(c[1], labels[1]);
      break;
    case kScale:
      s = term(c[0], labels[0]);
      break;
    }
  return String(s);
  }

  std::unique_ptr<MATH::Histogram> HistogramOperations::compute(Operation op, const std::vector<const MATH::Histogram *> & operands,
                                                                const std::vector<double> & coefficients,
                                                                const std::vector<String> & labels, String & error)
  {
  std::unique_ptr<MATH::Histogram> result;
  std::size_t n = operands.size();
  if (n < minimumOperands(op)) { error = op == kScale ? "Choose a histogram" : "Choose two histograms (or more for a sum)"; return result; }
  if (n > maximumOperands(op)) { error = op == kScale ? "Choose one histogram" : "Choose two histograms"; return result; }
  if (coefficients.size() != n) { error = "One coefficient per histogram is needed"; return result; }
  for (std::size_t k=0; k<n; k++)
    {
    if (!operands[k]) { error = "A histogram chosen is no longer shown"; return result; }
    if (!std::isfinite(coefficients[k])) { error = "A coefficient is not a number"; return result; }
    if (k > 0 && (operands[k]->rank() != operands[0]->rank() || !operands[0]->compatibleWith(*operands[k])))
      {
      String a = k < labels.size() ? labels[k] : String("a histogram");
      String b = labels.empty() ? String("the first") : labels[0];
      error = a + String(" and ") + b + String(" do not have the same binning");
      return result;
      }
    }
  if (op == kRatio && coefficients[1] == 0.0) { error = "The coefficient of the divisor is 0"; return result; }
  try
    {
    // the first operand copied (a HistogramSyst stays one: its systematic uncertainties are propagated; a profile
    // gives a histogram of its values)
    if (dynamic_cast<const MATH::Profile *>(operands[0])) result.reset(new MATH::Histogram(*operands[0]));
    else result.reset(operands[0]->clone());
    result->scale(coefficients[0]);
    switch (op)
      {
      case kSum:        for (std::size_t k=1; k<n; k++) result->add(*operands[k], coefficients[k]); break;
      case kDifference: result->add(*operands[1], -coefficients[1]); break;
      case kProduct:    result->multiplyBy(*operands[1], coefficients[1]); break;
      case kRatio:      result->divideBy(*operands[1], coefficients[1]); break;
      case kScale:      break;
      }
    }
  catch (const std::exception & e)
    {
    error = String(e.what());
    result.reset();
    return result;
    }
  std::vector<String> named = labels;
  while (named.size() < n) named.push_back(operands[named.size()]->name());
  std::string resultName = std::string(name(op));
  resultName[0] = char(std::tolower((unsigned char) resultName[0]));
  for (std::size_t k=0; k<n; k++) resultName += "_" + nameOf(named[k]);
  if (resultName.size() > 60) resultName = resultName.substr(0, 60);
  result->setName(String(resultName));
  result->setTitle(expression(op, named, coefficients));
  return result;
  }

  unsigned int HistogramOperations::histogramsIn(CanvasPane & pane, int graph)
  {
  if (graph < 0 || graph >= pane.canvas().nGraphs()) return 0;
  unsigned int n = 0;
  for (const GraphItem & item : pane.canvas().graph(graph).items())
    if (item.kind() == GraphItem::kData && dynamic_cast<const MATH::Histogram *>(item.data())) n++;
  return n;
  }

  // ---------------------------------------------------------------------------------------------- the window

  void HistogramOperations::open(Operation op, CanvasPane & pane, int graph)
  {
  _operation = op;
  _sourcePane = pane.windowId();
  _graph = graph;
  _histograms.clear();
  _labels.clear();
  _coefficients.clear();
  _chosen.clear();
  _message = String();
  if (graph >= 0 && graph < pane.canvas().nGraphs())
    for (const GraphItem & item : pane.canvas().graph(graph).items())
      {
      const MATH::Histogram * h = item.kind() == GraphItem::kData ? dynamic_cast<const MATH::Histogram *>(item.data()) : nullptr;
      if (!h) continue;
      _histograms.push_back(h);
      _labels.push_back(item.label().isNull() ? h->name() : item.label());
      _coefficients.push_back(1.0);
      }
  if (op == kScale && _histograms.size() == 1) _chosen.push_back(0);   // the only one: chosen
  if (op != kScale && op != kSum && _histograms.size() == 2) { _chosen.push_back(0); _chosen.push_back(1); }
  _open = true;
  _focus = true;
  }

  void HistogramOperations::toggle(unsigned int k)
  {
  if (k >= _histograms.size()) return;
  _message = String();
  auto at = std::find(_chosen.begin(), _chosen.end(), k);
  if (at != _chosen.end()) { _chosen.erase(at); return; }
  unsigned int most = maximumOperands(_operation);
  if (_chosen.size() < most) { _chosen.push_back(k); return; }
  if (most == 1) { _chosen[0] = k; return; }                   // Scale: the one clicked replaces the other
  _message = "Two histograms are chosen: click one of them to unselect it first";
  }

  std::vector<const MATH::Histogram *> HistogramOperations::listed()
  {
  std::vector<const MATH::Histogram *> shown(_histograms.size(), nullptr);
  CanvasPane * pane = _panes.find ? _panes.find(_sourcePane) : nullptr;
  if (!pane || _graph < 0 || _graph >= pane->canvas().nGraphs()) return shown;
  const Graph & g = pane->canvas().graph(_graph);
  for (std::size_t k=0; k<_histograms.size(); k++)
    for (const GraphItem & item : g.items()) if (item.data() == _histograms[k]) shown[k] = _histograms[k];   // pointers compared only
  return shown;
  }

  bool HistogramOperations::confirm()
  {
  std::vector<const MATH::Histogram *> shown = listed();
  std::vector<const MATH::Histogram *> operands;
  std::vector<double> coefficients;
  std::vector<String> labels;
  for (unsigned int k : _chosen)
    {
    operands.push_back(shown[k]);
    coefficients.push_back(_coefficients[k]);
    labels.push_back(_labels[k]);
    }
  String error;
  std::unique_ptr<MATH::Histogram> h = compute(_operation, operands, coefficients, labels, error);
  if (!h) { _message = error; return false; }
  if (!_panes.addBelow) { _message = "No canvas to show the result"; return false; }
  _results.emplace_back(new Result());
  Result & r = *_results.back();
  r.id = _nextId++;
  r.expression = expression(_operation, labels, coefficients);
  r.histogram = std::move(h);
  CanvasPane & target = _panes.addBelow(_sourcePane);
  target.divide(1, 1);
  target.setSelectedGraph(0);
  target.setTitle(String(name(_operation)) + String(": ") + r.expression);
  r.targetPane = target.windowId();
  target.canvas().graph(0).show(GraphItem(r.histogram.get(), r.expression));
  _open = false;
  _message = String();
  if (onStatus) onStatus(String(name(_operation)) + String(": ") + r.expression
                         + String(" shown below (right-click it to save it)"));
  return true;
  }

  void HistogramOperations::update()
  {
  for (std::size_t k=_results.size(); k-- > 0; )
    {
    Result & r = *_results[k];
    CanvasPane * target = _panes.find ? _panes.find(r.targetPane) : nullptr;
    bool shown = false;
    if (target && target->canvas().nGraphs() > 0)
      for (const GraphItem & item : target->canvas().graph(0).items()) if (item.data() == r.histogram.get()) shown = true;
    if (shown) continue;
    if (_saveId == r.id) _saveId = 0;
    _retired.push_back(std::move(r.histogram));
    _results.erase(_results.begin() + long(k));
    }
  }

  void HistogramOperations::contextMenu(CanvasPane & pane, int /*graph*/)
  {
  for (auto & x : _results)
    {
    Result & r = *x;
    if (r.targetPane != pane.windowId()) continue;
    ImGui::Separator();
    ImGui::TextDisabled("%s", r.expression.data());
    if (ImGui::MenuItem("Save Result...")) openSave(r.id);
    ImGui::SetItemTooltip("Add the result, under a name, to the folder selected in the browser (a new data file if that "
                          "folder cannot be changed)");
    if (ImGui::MenuItem("Close Result") && _panes.close) _panes.close(r.targetPane);
    return;
    }
  }

  void HistogramOperations::openSave(int id)
  {
  for (auto & r : _results)
    if (r->id == id)
      {
      _saveId = id;
      _saveRequested = true;
      _saveName = r->histogram->name().str();
      _saveMessage = String();
      }
  }

  bool HistogramOperations::save(int id, const String & name, String & message)
  {
  Result * r = nullptr;
  for (auto & x : _results) if (x->id == id) r = x.get();
  if (!r || !r->histogram) { message = "The result is no longer shown"; return false; }
  std::string n = name.str();
  std::size_t a = n.find_first_not_of(" \t"), b = n.find_last_not_of(" \t");
  n = a == std::string::npos ? std::string() : n.substr(a, b - a + 1);
  if (n.empty()) { message = "Give the result a name"; return false; }
  if (n.find('/') != std::string::npos) { message = "A name cannot contain '/'"; return false; }
  if (!onSave) { message = "Nowhere to save it"; return false; }
  std::unique_ptr<MATH::Histogram> h(r->histogram->clone());   // a HistogramSyst saved as one
  h->setName(String(n));
  return onSave(h.get(), String(n), message);
  }

  void HistogramOperations::render()
  {
  if (_open) renderWindow();
  renderSave();
  }

  void HistogramOperations::renderWindow()
  {
  ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
  if (_focus) { ImGui::SetNextWindowFocus(); _focus = false; }
  bool open = true;
  std::string title = std::string(name(_operation)) + "###HistogramOperation";
  if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {
    static const char * what[] = { "c1 A + c2 B + ...: click two histograms or more",
                                   "c1 A - c2 B: click two histograms, A first",
                                   "(c1 A) x (c2 B), bin by bin: click two histograms",
                                   "(c1 A) / (c2 B), bin by bin: click two histograms, A (the numerator) first",
                                   "c A: click a histogram" };
    ImGui::TextUnformatted(what[_operation]);
    ImGui::TextDisabled("The coefficients are 1 unless changed. The histograms need the same binning.");
    ImGui::Separator();
    std::vector<const MATH::Histogram *> shown = listed();
    unsigned int nShown = 0;
    for (const MATH::Histogram * h : shown) if (h) nShown++;
    if (nShown < minimumOperands(_operation))
      ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.25f, 1.0f), "%s", _operation == kScale
        ? "The selected graph shows no histogram: show one (click it in the browser), then choose Scale again."
        : "The selected graph shows fewer than two histograms: show them in one graph (drag them from the browser), "
          "then choose the operation again.");
    for (unsigned int k=0; k<_histograms.size(); k++)
      {
      if (!shown[k]) continue;
      ImGui::PushID(int(k));
      auto at = std::find(_chosen.begin(), _chosen.end(), k);
      bool chosen = at != _chosen.end();
      std::string label = chosen ? std::to_string(at - _chosen.begin() + 1) + ".  " + _labels[k].str() : "     " + _labels[k].str();
      if (ImGui::Selectable(label.c_str(), chosen, ImGuiSelectableFlags_None, ImVec2(300, 0))) toggle(k);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s: %u bins%s", _histograms[k]->name().data(), _histograms[k]->nBinsTotal(),
                          _histograms[k]->hasSystUncertainties() ? ", with systematic uncertainties" : "");
      if (chosen)
        {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::InputDouble("coefficient", &_coefficients[k], 0.0, 0.0, "%g");
        }
      ImGui::PopID();
      }
    ImGui::Separator();
    std::vector<String> labels;
    std::vector<double> coefficients;
    for (unsigned int k : _chosen) { labels.push_back(_labels[k]); coefficients.push_back(_coefficients[k]); }
    bool enough = _chosen.size() >= minimumOperands(_operation) && _chosen.size() <= maximumOperands(_operation);
    if (!_chosen.empty()) ImGui::Text("Result: %s", expression(_operation, labels, coefficients).data());
    if (!_message.isNull()) ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.0f), "%s", _message.data());
    if (!enough) ImGui::BeginDisabled();
    if (ImGui::Button("OK", ImVec2(110, 0))) confirm();
    if (!enough) ImGui::EndDisabled();
    ImGui::SetItemTooltip("Compute the result and show it in a canvas below");
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) cancel();
    }
  ImGui::End();
  if (!open) cancel();
  }

  void HistogramOperations::renderSave()
  {
  if (_saveRequested) { ImGui::OpenPopup("Save Result"); _saveRequested = false; }
  if (!ImGui::BeginPopupModal("Save Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  Result * r = nullptr;
  for (auto & x : _results) if (x->id == _saveId) r = x.get();
  if (!r)
    {
    ImGui::TextDisabled("The result is no longer shown.");
    if (ImGui::Button("Close", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
    }
  ImGui::TextUnformatted(r->expression.data());
  ImGui::Spacing();
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(360);
  bool enter = ImGui::InputText("name", &_saveName, ImGuiInputTextFlags_EnterReturnsTrue);
  String where;
  if (onSave) onSave(nullptr, String(_saveName), where);
  if (!where.isNull()) ImGui::TextDisabled("%s", where.data());
  if (!_saveMessage.isNull()) ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.0f), "%s", _saveMessage.data());
  if (ImGui::Button("OK", ImVec2(110, 0)) || enter)
    {
    String message;
    if (save(_saveId, String(_saveName), message))
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
