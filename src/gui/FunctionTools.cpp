/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file FunctionTools.cpp
//! @brief Implementation of FunctionTools.

#include "FunctionTools.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "DatumFile.hpp"
#include "FileSystem.hpp"
#include "FunctionRegistry.hpp"
#include "Graph.hpp"
#include "Table2D.hpp"
#include "TableWriter.hpp"

namespace Datum
{
  namespace GUI
  {
  using GRAPHICS::Graph;
  using GRAPHICS::GraphItem;
  using MATH::ParameterizedFunction;
  using MATH::ParameterizedComposite;

  namespace
  {
  //! Label of operand k: A, B, C, ...
  String letter(std::size_t k)
  {
  std::string s;
  s += char('A' + int(k % 26));
  if (k >= 26) s += std::to_string(k/26);
  return String(s.c_str());
  }

  //! A coefficient as text: "" for 1, "-" for -1, otherwise the number and a space.
  std::string coefficientText(double c)
  {
  if (c == 1.0) return "";
  char text[32];
  std::snprintf(text, sizeof(text), "%g ", c);
  return text;
  }

  //! Whether a name ends with an extension (any case).
  bool hasExtension(const String & path, const char * extension)
  {
  std::string p = path.str(), e = extension;
  if (p.size() < e.size()) return false;
  for (std::size_t k=0; k<e.size(); k++) if (std::tolower((unsigned char)p[p.size() - e.size() + k]) != e[k]) return false;
  return true;
  }
  } // namespace

  const char * FunctionTools::name(Operation op)
  {
  switch (op)
    {
    case kSum:        return "Sum";
    case kDifference: return "Difference";
    case kProduct:    return "Product";
    case kRatio:      return "Ratio";
    case kScale:      return "Scale";
    }
  return "";
  }

  std::vector<FunctionTools::Shown> FunctionTools::functionsIn(const Graph & graph)
  {
  std::vector<Shown> list;
  for (std::size_t k=0; k<graph.items().size(); k++)
    {
    const GraphItem & item = graph.items()[k];
    if (item.kind() != GraphItem::kFunction) continue;
    const ParameterizedFunction * f = dynamic_cast<const ParameterizedFunction *>(item.function());
    if (!f) continue;
    Shown s;
    s.item = int(k);
    s.function = f;
    s.label = item.label().isNull() || item.label().str().empty() ? String(f->name()) : item.label();
    s.xMin = item.xMinimum(); s.xMax = item.xMaximum();
    s.yMin = item.yMinimum(); s.yMax = item.yMaximum();
    list.push_back(s);
    }
  return list;
  }

  String FunctionTools::expression(Operation op, const std::vector<String> & labels, const std::vector<double> & coefficients)
  {
  std::string text;
  auto term = [&](std::size_t k) { return coefficientText(k < coefficients.size() ? coefficients[k] : 1.0) + labels[k].str(); };
  for (std::size_t k=0; k<labels.size(); k++)
    {
    switch (op)
      {
      case kSum:        text += (k ? " + " : "") + term(k); break;
      case kDifference: text += (k ? " - " : "") + term(k); break;
      case kProduct:    text += "(" + term(k) + ")" + (k + 1 < labels.size() ? " x " : ""); break;
      case kRatio:      text += (k ? " / " : "") + std::string(labels.size() > 1 ? "(" : "") + term(k) + (labels.size() > 1 ? ")" : ""); break;
      case kScale:      text += term(k); break;
      }
    }
  return String(text.c_str());
  }

  std::unique_ptr<ParameterizedComposite> FunctionTools::combine(Operation op, const std::vector<const ParameterizedFunction *> & operands,
                                                                 const std::vector<double> & coefficients,
                                                                 const std::vector<String> & labels, String & error)
  {
  if (operands.size() < minimumOperands(op) || operands.size() > maximumOperands(op))
    {
    error = String(name(op)) + String(op == kScale ? ": choose one function" : op == kSum ? ": choose two functions or more" : ": choose two functions");
    return nullptr;
    }
  for (const ParameterizedFunction * f : operands)
    if (!f || f->rank() != operands.front()->rank()) { error = "The functions must have the same number of variables"; return nullptr; }
  ParameterizedComposite::Operation kind = op == kProduct ? ParameterizedComposite::kPointwiseProduct
                                         : op == kRatio ? ParameterizedComposite::kRatio : ParameterizedComposite::kSum;
  std::unique_ptr<ParameterizedComposite> result(new ParameterizedComposite(kind));
  try
    {
    for (std::size_t k=0; k<operands.size(); k++)
      {
      double c = k < coefficients.size() ? coefficients[k] : 1.0;
      if (op == kDifference && k == 1) c = -c;
      result->add(letter(k), *operands[k], c);
      }
    }
  catch (const std::exception & e) { error = String(e.what()); return nullptr; }
  result->setName(name(op));
  result->setTitle(expression(op, labels, coefficients));
  return result;
  }

  bool FunctionTools::write(const ParameterizedFunction & f, const String & path, const Shown & shown, String & error)
  {
  try
    {
    String name = f.name().isEmpty() ? String("function") : String(f.name());
    if (hasExtension(path, ".datum"))
      {
      DatumFile file(path);
      if (FileSystem::isFile(path)) file.read();                // added to the file (an object of that name replaced)
      if (file.contains(name)) file.remove(name);
      file.add(f.clone(), name);
      file.write();
      return true;
      }
    // a table of the values
    MATH::Table2D table(name);
    if (f.rank() == 1)
      {
      const int n = 201;
      for (int k=0; k<n; k++)
        {
        double x = shown.xMin + (shown.xMax - shown.xMin)*k/double(n - 1);
        table.addRow({ x, f.evaluateAt(x) });
        }
      table.setColumnNames({ "x", name });
      }
    else if (f.rank() == 2)
      {
      const int n = 51;
      for (int i=0; i<n; i++)
        for (int j=0; j<n; j++)
          {
          double x = shown.xMin + (shown.xMax - shown.xMin)*i/double(n - 1);
          double y = shown.yMin + (shown.yMax - shown.yMin)*j/double(n - 1);
          table.addRow({ x, y, f.evaluateAt(std::vector<double>{ x, y }) });
          }
      table.setColumnNames({ "x", "y", name });
      }
    else { error = "Only functions of one or two variables are written as tables"; return false; }
    TableWriter().write(path, table);
    return true;
    }
  catch (const std::exception & e) { error = String(e.what()); return false; }
  }

  std::vector<std::unique_ptr<ParameterizedFunction>> FunctionTools::read(const String & path, std::vector<String> & names, String & error)
  {
  std::vector<std::unique_ptr<ParameterizedFunction>> functions;
  names.clear();
  try
    {
    DatumFile file(path, File::kReadOnly);
    file.read();
    for (const String & p : file.folder().paths())
      if (const ParameterizedFunction * f = file.get<ParameterizedFunction>(p))
        {
        functions.emplace_back(f->clone());
        names.push_back(p);
        }
    }
  catch (const std::exception & e) { error = String(e.what()); }
  return functions;
  }

  // ------------------------------------------------------------------------------------------------------- windows

  void FunctionTools::makePrototype(const String & className, unsigned int nParameters)
  {
  ParameterizedFunction * f = nullptr;
  if (MATH::FunctionRegistry::isComposite(className)) f = MATH::FunctionRegistry::composite(className)->clone();
  else if (nParameters > 0) f = MATH::FunctionRegistry::create(className, nParameters);
  else f = MATH::FunctionRegistry::create(className);
  if (!f) { _editorMessage = className + String(": cannot be made with ") + String(std::to_string(nParameters).c_str()) + String(" parameters"); return; }
  _prototype.reset(f);
  _className = className.str();
  _nParameters = int(f->nParameters());
  _editorMessage = String();
  }

  void FunctionTools::openNew(double xMin, double xMax)
  {
  _editorOpen = true;
  _editorFocus = true;
  _xMin = xMin; _xMax = xMax; _yMin = xMin; _yMax = xMax;
  _name.clear();
  if (!_prototype || _className.empty()) makePrototype("Normal", 0);
  }

  void FunctionTools::openDuplicate(const Shown & shown)
  {
  if (!shown.function) return;
  _prototype.reset(shown.function->clone());
  _prototype->clearFits();
  _className = MATH::FunctionRegistry::isRegistered(*_prototype) && !dynamic_cast<const ParameterizedComposite *>(_prototype.get())
               ? MATH::FunctionRegistry::typeName(*_prototype).str() : std::string();
  _nParameters = int(_prototype->nParameters());
  _name = shown.label.str() + " copy";
  _xMin = shown.xMin; _xMax = shown.xMax; _yMin = shown.yMin; _yMax = shown.yMax;
  _editorMessage = String();
  _editorOpen = true;
  _editorFocus = true;
  }

  void FunctionTools::openFile(const String & path, double xMin, double xMax)
  {
  String error;
  _fileFunctions = read(path, _fileNames, error);
  if (!error.isNull() && _fileFunctions.empty())
    { if (onStatus) onStatus(String("Cannot read ") + path + String(": ") + error); return; }
  if (_fileFunctions.empty()) { if (onStatus) onStatus(String("No function in ") + path); return; }
  _filePath = path.str();
  _fileChecked.assign(_fileFunctions.size(), char(_fileFunctions.size() == 1));
  _fileXMin = xMin; _fileXMax = xMax; _fileYMin = xMin; _fileYMax = xMax;
  _fileOpen = true;
  }

  void FunctionTools::openOperation(Operation op, const Graph & graph)
  {
  _operation = op;
  _operands.clear();
  _labels.clear();
  _coefficients.clear();
  _chosen.clear();
  _operationMessage = String();
  bool first = true;
  for (const Shown & s : functionsIn(graph))
    {
    _operands.emplace_back(s.function->clone());
    _operands.back()->clearFits();
    _labels.push_back(s.label);
    _coefficients.push_back(1.0);
    if (first) { _opXMin = s.xMin; _opXMax = s.xMax; _opYMin = s.yMin; _opYMax = s.yMax; first = false; }
    else { _opXMin = std::min(_opXMin, s.xMin); _opXMax = std::max(_opXMax, s.xMax); }
    }
  _operationOpen = true;
  _operationFocus = true;
  }

  void FunctionTools::render()
  {
  renderEditor();
  renderFile();
  renderOperation();
  }

  void FunctionTools::renderEditor()
  {
  if (!_editorOpen) return;
  if (_editorFocus) { ImGui::SetNextWindowFocus(); _editorFocus = false; }
  ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Function", &_editorOpen, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
  // the class (a copy of a composite keeps its components: its parameters only are edited)
  std::vector<String> classes = MATH::FunctionRegistry::names();
  for (const String & c : MATH::FunctionRegistry::compositeNames()) classes.push_back(c);
  const char * shownClass = _className.empty() ? (_prototype ? "(copy of a composite)" : "") : _className.c_str();
  ImGui::SetNextItemWidth(260);
  if (ImGui::BeginCombo("class", shownClass))
    {
    for (const String & c : classes)
      if (ImGui::Selectable(c.data(), c.str() == _className)) makePrototype(c, 0);
    ImGui::EndCombo();
    }
  ImGui::SetItemTooltip("A function of FunctionRegistry, or a composite (registered in the Fit window)");
  if (!_className.empty() && !MATH::FunctionRegistry::isComposite(String(_className.c_str())))
    {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    int n = _nParameters;
    if (ImGui::InputInt("parameters", &n) && n > 0) makePrototype(String(_className.c_str()), (unsigned int)n);
    ImGui::SetItemTooltip("Number of parameters (classes such as Polynomial accept several)");
    }
  ImGui::SetNextItemWidth(260);
  ImGui::InputTextWithHint("name", "shown in the legend", &_name);
  if (_prototype)
    {
    ImGui::SeparatorText("Parameters");
    if (ImGui::BeginTable("##parameters", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::min(240.0f, 26.0f*float(_prototype->nParameters()) + 8.0f))))
      {
      ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 160);
      ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
      for (unsigned int k=0; k<_prototype->nParameters(); k++)
        {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(_prototype->parameter(k).name().data());
        ImGui::TableNextColumn();
        double v = _prototype->parameter(k).value();
        ImGui::PushID(int(k));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputDouble("##value", &v, 0, 0, "%.6g"))
          {
          try { MATH::Parameter p = _prototype->parameter(k); p.setValue(v); _prototype->setParameter(k, p); }
          catch (const std::exception &) { }
          }
        ImGui::PopID();
        }
      ImGui::EndTable();
      }
    ImGui::SeparatorText("Range");
    ImGui::SetNextItemWidth(120); ImGui::InputDouble("x min", &_xMin, 0, 0, "%.6g");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120); ImGui::InputDouble("x max", &_xMax, 0, 0, "%.6g");
    if (_prototype->rank() == 2)
      {
      ImGui::SetNextItemWidth(120); ImGui::InputDouble("y min", &_yMin, 0, 0, "%.6g");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(120); ImGui::InputDouble("y max", &_yMax, 0, 0, "%.6g");
      }
    if (_prototype->rank() > 2) ImGui::TextDisabled("Functions of more than two variables cannot be drawn");
    }
  ImGui::Separator();
  bool valid = _prototype && _prototype->rank() <= 2 && _xMax > _xMin && (_prototype->rank() < 2 || _yMax > _yMin);
  ImGui::BeginDisabled(!valid);
  if (ImGui::Button("Add"))
    {
    std::unique_ptr<ParameterizedFunction> f(_prototype->clone());
    String fname = _name.empty() ? String((_className.empty() ? std::string("function") : _className) + " " + std::to_string(++_added)) : String(_name.c_str());
    f->setName(fname);
    if (onShow) onShow(std::move(f), _xMin, _xMax, _yMin, _yMax);
    if (onStatus) onStatus(String("Added ") + fname + String(" (Functions > New adds another; Functions > Save writes it)"));
    _name.clear();
    }
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("Show the function in the selected graph of the current canvas (a canvas is made if there is none); "
                        "the window stays open to add more");
  ImGui::SameLine();
  if (ImGui::Button("Close")) _editorOpen = false;
  if (!_editorMessage.isNull()) ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1), "%s", _editorMessage.data());
  ImGui::End();
  }

  void FunctionTools::renderFile()
  {
  if (!_fileOpen) return;
  ImGui::SetNextWindowSize(ImVec2(460, 380), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Open Functions", &_fileOpen, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
  ImGui::TextWrapped("Functions of %s: check those to show in the selected graph of the current canvas.", _filePath.c_str());
  if (ImGui::BeginChild("##list", ImVec2(0, -4.5f*ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders))
    {
    for (std::size_t k=0; k<_fileFunctions.size(); k++)
      {
      bool checked = _fileChecked[k] != 0;
      std::string label = _fileNames[k].str() + "  (" + std::to_string(_fileFunctions[k]->nParameters()) + " parameters, "
                          + std::to_string(_fileFunctions[k]->rank()) + (_fileFunctions[k]->rank() == 1 ? " variable)" : " variables)");
      ImGui::PushID(int(k));
      if (ImGui::Checkbox(label.c_str(), &checked)) _fileChecked[k] = char(checked);
      ImGui::PopID();
      }
    }
  ImGui::EndChild();
  ImGui::SetNextItemWidth(120); ImGui::InputDouble("x min", &_fileXMin, 0, 0, "%.6g");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120); ImGui::InputDouble("x max", &_fileXMax, 0, 0, "%.6g");
  ImGui::SetNextItemWidth(120); ImGui::InputDouble("y min", &_fileYMin, 0, 0, "%.6g");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120); ImGui::InputDouble("y max", &_fileYMax, 0, 0, "%.6g");
  ImGui::SameLine();
  ImGui::TextDisabled("(two variables)");
  bool any = std::find(_fileChecked.begin(), _fileChecked.end(), char(1)) != _fileChecked.end();
  ImGui::BeginDisabled(!any || _fileXMax <= _fileXMin);
  if (ImGui::Button("Show"))
    {
    int n = 0;
    for (std::size_t k=0; k<_fileFunctions.size(); k++)
      if (_fileChecked[k] && _fileFunctions[k]->rank() <= 2 && onShow)
        { onShow(std::unique_ptr<ParameterizedFunction>(_fileFunctions[k]->clone()), _fileXMin, _fileXMax, _fileYMin, _fileYMax); n++; }
    if (onStatus) onStatus(String(std::to_string(n).c_str()) + String(" function(s) shown from ") + String(_filePath.c_str()));
    _fileOpen = false;
    }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) _fileOpen = false;
  ImGui::End();
  }

  void FunctionTools::renderOperation()
  {
  if (!_operationOpen) return;
  if (_operationFocus) { ImGui::SetNextWindowFocus(); _operationFocus = false; }
  ImGui::SetNextWindowSize(ImVec2(460, 360), ImGuiCond_FirstUseEver);
  std::string title = std::string(name(_operation)) + " of Functions###functionOperation";
  if (!ImGui::Begin(title.c_str(), &_operationOpen, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
  const char * help = _operation == kSum ? "c1 A + c2 B + ...: click two functions or more"
                    : _operation == kDifference ? "c1 A - c2 B: click two functions, A first"
                    : _operation == kProduct ? "(c1 A)(c2 B): click two functions"
                    : _operation == kRatio ? "(c1 A) / (c2 B): click two functions, A (the numerator) first"
                    : "c A: click a function";
  ImGui::TextUnformatted(help);
  ImGui::TextDisabled("The coefficients are 1 unless changed. The result is shown in the graph.");
  if (_operands.empty()) ImGui::TextDisabled("No function in the selected graph (Functions > New adds one).");
  for (unsigned int k=0; k<_operands.size(); k++)
    {
    ImGui::PushID(int(k));
    auto found = std::find(_chosen.begin(), _chosen.end(), k);
    bool chosen = found != _chosen.end();
    std::string label = chosen ? std::to_string(found - _chosen.begin() + 1) + ".  " + _labels[k].str() : "     " + _labels[k].str();
    if (ImGui::Selectable(label.c_str(), chosen, 0, ImVec2(260, 0)))
      {
      if (chosen) _chosen.erase(found);
      else if (_chosen.size() < maximumOperands(_operation)) _chosen.push_back(k);
      }
    ImGui::SameLine(280);
    ImGui::SetNextItemWidth(100);
    ImGui::InputDouble("coefficient", &_coefficients[k], 0, 0, "%.6g");
    ImGui::PopID();
    }
  std::vector<String> labels;
  std::vector<double> coefficients;
  std::vector<const ParameterizedFunction *> operands;
  for (unsigned int k : _chosen) { labels.push_back(_labels[k]); coefficients.push_back(_coefficients[k]); operands.push_back(_operands[k].get()); }
  ImGui::Separator();
  if (!labels.empty()) ImGui::Text("Result: %s", expression(_operation, labels, coefficients).data());
  bool enough = operands.size() >= minimumOperands(_operation);
  ImGui::BeginDisabled(!enough);
  if (ImGui::Button("OK"))
    {
    String error;
    std::unique_ptr<ParameterizedComposite> result = combine(_operation, operands, coefficients, labels, error);
    if (!result) _operationMessage = error;
    else
      {
      String text = String(result->title());
      if (onShow) onShow(std::unique_ptr<ParameterizedFunction>(result.release()), _opXMin, _opXMax, _opYMin, _opYMax);
      if (onStatus) onStatus(String(name(_operation)) + String(": ") + text);
      _operationOpen = false;
      }
    }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) _operationOpen = false;
  if (!_operationMessage.isNull()) ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1), "%s", _operationMessage.data());
  ImGui::End();
  }

  } // namespace GUI
} // namespace Datum
