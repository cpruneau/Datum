/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include "DatumGuiApp.hpp"
#include "PlotOutputs.hpp"
#include "PlotTemplate.hpp"
#include "DatumFile.hpp"
#include "ColorTable.hpp"
#include "FileSystem.hpp"
#include "FunctionRegistry.hpp"
#include "GraphEditor.hpp"
#include "ImGuiPainter.hpp"
#include "Palettes.hpp"
#include "XmlDocument.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

namespace Datum
{
  namespace GUI
  {
  using GRAPHICS::PlotDocument;
  using GRAPHICS::PlotSpec;
  using Path = PlotDocument::Path;
  namespace GraphXml = GRAPHICS::GraphXml;

  namespace
  {
  //!
  //! Grid of grey squares (8 x 8): the squares up to the one under the mouse are highlighted, those of the current grid
  //! outlined; a click sets columns and rows and closes the menu. Returns true when a grid is chosen.
  //!
  bool gridPicker(int currentColumns, int currentRows, int & columns, int & rows)
  {
  const int n = 8;
  const float cell = 18.0f, gap = 3.0f, step = cell + gap;
  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##grid", ImVec2(n*step - gap, n*step - gap));
  const bool hovered = ImGui::IsItemHovered();
  int hc = 0, hr = 0;
  if (hovered)
    {
    ImVec2 m = ImGui::GetIO().MousePos;
    hc = std::max(1, std::min(n, int((m.x - origin.x)/step) + 1));
    hr = std::max(1, std::min(n, int((m.y - origin.y)/step) + 1));
    }
  ImDrawList * draw = ImGui::GetWindowDrawList();
  const ImU32 grey = ImGui::GetColorU32(ImVec4(0.55f, 0.55f, 0.55f, 0.45f));
  const ImU32 lit  = ImGui::GetColorU32(ImGuiCol_ButtonActive);
  const ImU32 line = ImGui::GetColorU32(ImGuiCol_Text);
  for (int r=0; r<n; r++)
    for (int c=0; c<n; c++)
      {
      ImVec2 a(origin.x + c*step, origin.y + r*step), b(a.x + cell, a.y + cell);
      draw->AddRectFilled(a, b, hovered && c < hc && r < hr ? lit : grey, 2.0f);
      if (c < currentColumns && r < currentRows) draw->AddRect(a, b, line, 2.0f);
      }
  if (hovered) ImGui::Text("%d x %d", hc, hr);
  else         ImGui::TextDisabled("now %d x %d", currentColumns, currentRows);
  if (hovered && ImGui::IsItemClicked())
    {
    columns = hc;
    rows = hr;
    ImGui::CloseCurrentPopup();
    return true;
    }
  return false;
  }

  //! Lower-case extension of a file name, with its dot ("" if none).
  std::string extensionOf(const String & fileName)
  {
  std::string name = fileName.str();
  std::size_t slash = name.rfind('/'), dot = name.rfind('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
  std::string e = name.substr(dot);
  for (char & ch : e) ch = char(std::tolower((unsigned char)ch));
  return e;
  }

  //! Give the histograms of an element the identifiers of the inputs of another plot file (by the map; the inputs
  //! without identifier are the first one).
  void renameInputs(XmlElement & e, const std::map<std::string, String> & ids, const String & first)
  {
  if (e.isNamed("Histogram") || e.isNamed("Data") || e.isNamed("Profile"))
    {
    String id = e.attribute("input", "");
    auto found = ids.find((id.isNull() ? first : id).str());
    if (found != ids.end()) e.setAttribute("input", found->second);
    }
  for (unsigned int k=0; k<e.nChildren(); k++) renameInputs(e.child(k), ids, first);
  }

  const char * kEditorWindow = "Plot Tree";
  const char * kInspectorWindow = "Inspector";   //!< ImGui name of the inspector pane
  const char * kNodePayload = "DATUM_PLOT_NODE";

  std::string pathKey(int tab, const Path & path)
  {
  std::string s = std::to_string(tab) + ":";
  for (std::size_t k=0; k<path.size(); k++) s += (k ? "." : "") + std::to_string(path[k]);
  return s;
  }

  bool parsePathKey(const std::string & key, int & tab, Path & path)
  {
  std::size_t colon = key.find(':');
  if (colon == std::string::npos) return false;
  try
    {
    tab = std::stoi(key.substr(0, colon));
    path.clear();
    std::string rest = key.substr(colon + 1);
    std::size_t start = 0;
    while (start < rest.size())
      {
      std::size_t dot = rest.find('.', start);
      if (dot == std::string::npos) dot = rest.size();
      path.push_back(std::stoi(rest.substr(start, dot - start)));
      start = dot + 1;
      }
    }
  catch (...) { return false; }
  return true;
  }

  ImVec4 kindColor(PlotDocument::Kind kind)
  {
  bool dark = ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x < 0.5f;
  switch (kind)
    {
    case PlotDocument::kWindow:    return dark ? ImVec4(0.45f, 0.70f, 1.00f, 1) : ImVec4(0.10f, 0.35f, 0.75f, 1);
    case PlotDocument::kCanvas:    return dark ? ImVec4(0.45f, 0.85f, 0.50f, 1) : ImVec4(0.10f, 0.50f, 0.15f, 1);
    case PlotDocument::kGraph:     return dark ? ImVec4(1.00f, 0.70f, 0.35f, 1) : ImVec4(0.75f, 0.40f, 0.00f, 1);
    case PlotDocument::kHistogram: return dark ? ImVec4(0.90f, 0.90f, 0.90f, 1) : ImVec4(0.10f, 0.10f, 0.10f, 1);
    case PlotDocument::kFunction:  return dark ? ImVec4(0.85f, 0.60f, 1.00f, 1) : ImVec4(0.50f, 0.15f, 0.65f, 1);
    default:                       return ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
    }
  }

  const char * kindTag(PlotDocument::Kind kind)
  {
  switch (kind)
    {
    case PlotDocument::kRoot:      return "Plots";
    case PlotDocument::kInput:     return "Input";
    case PlotDocument::kOutput:    return "Output";
    case PlotDocument::kWindow:    return "Window";
    case PlotDocument::kCanvas:    return "Canvas";
    case PlotDocument::kGraph:     return "Graph";
    case PlotDocument::kHistogram: return "Hist";
    case PlotDocument::kFunction:  return "Func";
    case PlotDocument::kStyle:     return "Style";
    default:                       return "";
    }
  }

  std::string number(double v)
  {
  return XmlElement::formatNumber(v).str();
  }

  //! A text attribute of an element
  bool textField(PlotDocument & d, const Path & p, const char * attribute, const char * label, float width=-1, const char * hint=nullptr)
  {
  const XmlElement * e = d.element(p);
  if (!e) return false;
  std::string value = e->attribute(attribute, "").str();
  ImGui::SetNextItemWidth(width > 0 ? width : ImGui::GetContentRegionAvail().x*0.6f);
  bool edited = hint ? ImGui::InputTextWithHint(label, hint, &value) : ImGui::InputText(label, &value);
  return edited && d.setAttribute(p, attribute, String(value));
  }

  //! A number attribute (an empty field: the default)
  bool numberField(PlotDocument & d, const Path & p, const char * attribute, const char * label, double fallback,
                   double step=0.0, const char * format="%.6g", float width=130)
  {
  const XmlElement * e = d.element(p);
  if (!e) return false;
  double value = e->attributeDouble(attribute, fallback);
  ImGui::SetNextItemWidth(width);
  if (!ImGui::InputDouble(label, &value, step, step*10, format)) return false;
  return d.setAttribute(p, attribute, String(number(value)));
  }

  bool intField(PlotDocument & d, const Path & p, const char * attribute, const char * label, int fallback, int low, int high,
                float width=110)
  {
  const XmlElement * e = d.element(p);
  if (!e) return false;
  int value = int(e->attributeInt(attribute, fallback));
  ImGui::SetNextItemWidth(width);
  if (!ImGui::InputInt(label, &value)) return false;
  value = std::max(low, std::min(high, value));
  return d.setAttribute(p, attribute, String(std::to_string(value)));
  }

  bool checkField(PlotDocument & d, const Path & p, const char * attribute, const char * label, bool fallback)
  {
  const XmlElement * e = d.element(p);
  if (!e) return false;
  bool value = e->attributeBool(attribute, fallback);
  if (!ImGui::Checkbox(label, &value)) return false;
  return d.setAttribute(p, attribute, String(value ? "true" : "false"));
  }

  //! A grid of columns x rows chosen by the mouse (in a popup). @return true if chosen
  bool gridPicker(int & columns, int & rows, int maxColumns=6, int maxRows=6)
  {
  static int hoverC = 0, hoverR = 0;
  const float s = ImGui::GetFrameHeight();
  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList * draw = ImGui::GetWindowDrawList();
  ImGui::InvisibleButton("##grid", ImVec2(maxColumns*(s + 3), maxRows*(s + 3)));
  bool chosen = false;
  if (ImGui::IsItemHovered())
    {
    ImVec2 m = ImGui::GetMousePos();
    hoverC = std::max(1, std::min(maxColumns, int((m.x - origin.x)/(s + 3)) + 1));
    hoverR = std::max(1, std::min(maxRows,    int((m.y - origin.y)/(s + 3)) + 1));
    if (ImGui::IsItemClicked()) { columns = hoverC; rows = hoverR; chosen = true; }
    }
  else { hoverC = columns; hoverR = rows; }
  for (int r=0; r<maxRows; r++)
    for (int c=0; c<maxColumns; c++)
      {
      ImVec2 a(origin.x + c*(s + 3), origin.y + r*(s + 3)), b(a.x + s, a.y + s);
      bool on = c < hoverC && r < hoverR;
      draw->AddRectFilled(a, b, ImGui::GetColorU32(on ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg), 2.0f);
      draw->AddRect(a, b, ImGui::GetColorU32(ImGuiCol_Border), 2.0f);
      }
  ImGui::Text("%d x %d", hoverC, hoverR);
  return chosen;
  }

  //! A grid pull-down: "c x r" button opening the grid picker; writes columns and rows
  bool gridControl(PlotDocument & d, const Path & p, const char * id, const char * tooltip)
  {
  const XmlElement * e = d.element(p);
  if (!e) return false;
  int columns = int(e->attributeInt("columns", 0)), rows = int(e->attributeInt("rows", 0));
  char text[48];
  if (columns > 0 || rows > 0) std::snprintf(text, sizeof(text), "%d x %d##%s", std::max(columns, 1), std::max(rows, 1), id);
  else std::snprintf(text, sizeof(text), "auto##%s", id);
  if (ImGui::SmallButton(text)) ImGui::OpenPopup(id);
  ImGui::SetItemTooltip("%s", tooltip);
  bool changed = false;
  if (ImGui::BeginPopup(id))
    {
    ImGui::TextDisabled("%s", tooltip);
    int c = std::max(columns, 1), r = std::max(rows, 1);
    if (gridPicker(c, r))
      {
      d.beginEdit();
      changed |= d.setAttribute(p, "columns", String(std::to_string(c)));
      changed |= d.setAttribute(p, "rows", String(std::to_string(r)));
      d.endEdit();
      ImGui::CloseCurrentPopup();
      }
    if (ImGui::Button("Automatic"))
      {
      d.beginEdit();
      changed |= d.setAttribute(p, "columns", "");
      changed |= d.setAttribute(p, "rows", "");
      d.endEdit();
      ImGui::CloseCurrentPopup();
      }
    ImGui::SetItemTooltip("As many cells as needed");
    ImGui::EndPopup();
    }
  return changed;
  }

  //! Index of an item among the items of its graph, and the graph's element
  int itemIndex(const PlotDocument & d, const Path & item, const XmlElement *& graph)
  {
  Path g = PlotDocument::parent(item);
  graph = d.element(g);
  int k = 0;
  if (!graph || item.empty()) return 0;
  for (int j=0; j<item.back() && j<int(graph->nChildren()); j++)
    {
    PlotDocument::Kind kind = PlotDocument::kind(graph->child(unsigned(j)));
    if (kind == PlotDocument::kHistogram || kind == PlotDocument::kFunction) k++;
    }
  return k;
  }

  //! Color of an item as drawn: its own, or that of the palette of its graph
  int itemColor(const PlotDocument & d, const Path & item, const char * child)
  {
  String own = d.style(item, child, "color");
  if (!own.isNull()) return GraphXml::parseColor(own, 1);
  const XmlElement * graph = nullptr;
  int k = itemIndex(d, item, graph);
  String palette = graph ? graph->attribute("colorPalette", "") : String("");
  return GRAPHICS::Palettes::session().colorPaletteOr(palette).color(k);
  }

  int itemMarker(const PlotDocument & d, const Path & item)
  {
  String own = d.style(item, "Marker", "style");
  if (!own.isNull()) return std::atoi(own.data());
  const XmlElement * graph = nullptr;
  int k = itemIndex(d, item, graph);
  String palette = graph ? graph->attribute("markerPalette", "") : String("");
  return GRAPHICS::Palettes::session().markerPaletteOr(palette).marker(k);
  }

  //! A marker as a small button showing it, opening the list of the markers
  bool markerButton(const char * id, int & style, int color)
  {
  float h = ImGui::GetFrameHeight();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::PushID(id);
  bool clicked = ImGui::Button("##marker", ImVec2(h, h));
  ImGui::SetItemTooltip("marker: click to choose");
  ImGuiPainter painter(ImGui::GetWindowDrawList(), nullptr, p.x, p.y, h, h);
  GRAPHICS::MarkerAttributes marker;
  marker.set(color, 1.0, style, 1.2);
  painter.drawMarker(GRAPHICS::Point2D(0.5, 0.5), marker);
  if (clicked) ImGui::OpenPopup("markers");
  bool changed = false;
  if (ImGui::BeginPopup("markers"))
    {
    changed = GraphEditor::markerEdit("marker", style, color);
    if (changed) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    }
  ImGui::PopID();
  return changed;
  }

  //! A line style as a small button, opening the list of the styles
  bool lineButton(const char * id, int & style)
  {
  static const char * shown[] = { "none", "____", "- - -", ".....", "-.-.-" };
  ImGui::PushID(id);
  bool clicked = ImGui::SmallButton(shown[(style >= 0 && style <= 4) ? style : 1]);
  ImGui::SetItemTooltip("line style: click to choose");
  if (clicked) ImGui::OpenPopup("lines");
  bool changed = false;
  if (ImGui::BeginPopup("lines"))
    {
    changed = GraphEditor::lineStyleEdit("line style", style, false);
    if (changed) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    }
  ImGui::PopID();
  return changed;
  }

  //! A color of a child of style, as a swatch with the ROOT colors
  bool colorStyle(PlotDocument & d, const Path & p, const char * child, const char * label, int shown,
                  const char * which="", const char * also=nullptr)
  {
  int color = shown;
  if (!GraphEditor::colorEdit(label, color)) return false;
  d.beginEdit();
  bool changed = d.setStyle(p, child, "color", GraphXml::colorName(color), which);
  if (also) changed |= d.setStyle(p, also, "color", GraphXml::colorName(color), which);
  d.endEdit();
  return changed;
  }

  void helpText()
  {
  ImGui::TextWrapped("Every DatumGui session is a plot file (the XML files of PlotHistos): windows of canvases, canvases of "
                     "graphs, graphs of histograms and functions. The canvases on screen are the windows of the plot file "
                     "shown in the Plot Tree; what is changed on them is written to the file, and what is changed in the "
                     "tree draws them again.");
  ImGui::Spacing();
  ImGui::BulletText("Datum: About, Preferences (theme, fonts, palettes of colors, markers and line styles, LaTeX), New\n"
                    "Window, Rename Window, Close Window, Quit. Panes (browsers, Plot Browser, inspector, canvases, tables)\n"
                    "are dragged between the windows by their tab; Datum ends when its last window is closed.");
  ImGui::BulletText("File acts on the data files (Datum) of the browser, the one selected: New (a file browser), New Data\n"
                    "File, Open, Duplicate, Close, Save (a new or changed file), Export (ROOT, or every histogram on a page\n"
                    "of a PDF, LaTeX or PowerPoint file).");
#ifdef DATUM_WITH_HEPDATA
  ImGui::BulletText("File > Import > HEPData YAML makes a data file of the tables of HEPData submissions; File > Export >\n"
                    "HEPData YAML writes histograms, points, fits and functions as a submission. Both show a preview\n"
                    "first (see Help > HEPData Import and Export).");
#endif
  ImGui::BulletText("Plots acts on the plot files (XML), a tab each in the Plot Browser: New, Open, Duplicate, Close, Save,\n"
                    "Add Graph, Add Function, Regroup, Move Up and Down, Export (PDF book, LaTeX, PowerPoint, named plots).\n"
                    "Closing a changed plot file or tree asks to save it.");
  ImGui::BulletText("Windows acts on the window shown: New (with one canvas), Open (adds the windows of a plot file),\n"
                    "Duplicate, Close, Format (its grid of canvases), Save (PDF, SVG, XML or LaTeX), Export.\n"
                    "Canvas acts on the current canvas: New, Open (a canvas of a plot file), Duplicate, Rename, Close,\n"
                    "Format (its grid of graphs), Save, Save As (PDF, SVG, PNG, LaTeX, PowerPoint, XML), Clear, Clear All.");
  ImGui::BulletText("Drag a histogram from the browser onto a graph of a canvas or onto the tree: on a graph it is\n"
                    "overlaid; on a histogram it goes after it; on a canvas it gets a new graph; on a window a new\n"
                    "canvas. A folder adds all its histograms. Clicking a histogram in the browser adds it to the\n"
                    "selected graph.");
  ImGui::BulletText("The selection is shared: click a graph on a canvas, its row is selected in the tree, and the other\n"
                    "way around. Edit > Copy, Cut, Paste, Delete and Duplicate act on it; Copy puts its XML on the clipboard.\n"
                    "Edit > Clear empties the selected graph, Clear All every graph of the current canvas.");
  ImGui::BulletText("View shows or hides the panes: the file browser, table or data pane used last (Current) or all of\n"
                    "them, the Plot Browser, the inspector, the fit results, the toolbars of the canvases, the legends.");
  ImGui::BulletText("Windows > All Windows shows every window of the plot file; otherwise the window of the selection.");
  ImGui::BulletText("Right-click a map (a 2D histogram) > Project: onto x or y, over the whole range of the other axis or\n"
                    "over a range dragged across the map. The projection is shown in a canvas below, and the range is\n"
                    "marked on the map by two lines: drag a line (or its handle) and the projection follows when the mouse\n"
                    "rests or is released. Right-click the projection > Save Projection... adds it, under a name, to the\n"
                    "folder selected in the browser (or to a new data file if that folder is in a file read from disk).");
  ImGui::BulletText("Right-click a graph of histograms > Extract: two vertical lines (and, for a map, two horizontal ones);\n"
                    "the segment of the histograms (the region of the maps) between them is shown in a canvas below and\n"
                    "follows the lines when the mouse rests or is released. Right-click the extract > Save Extraction...\n"
                    "(a name per histogram) or Cancel Extraction.");
  ImGui::BulletText("Histograms: Sum, Difference, Product, Ratio, Scale of the histograms of the selected graph. Click\n"
                    "their names in the window (in order: A first), change the coefficients (1 by default), OK: the result\n"
                    "is shown in a canvas below; right-click it > Save Result... or Close Result. The same operations are in the\n"
                    "context menu of a graph (right-click), and the Histograms menu has Compare, Rebin, Fit and Show Data too.");
  ImGui::BulletText("Functions: New defines a function (class, parameters, range, name) and shows it in the selected graph\n"
                    "(a canvas is made if there is none; Add again for more). Open shows functions of a Datum file. Save and\n"
                    "Save As write a function to a Datum file, or its values as a table (.txt, .csv, .tsv, .dtb, .xlsx).\n"
                    "Duplicate opens a copy in the window of New, to edit. Sum, Difference, Product, Ratio, Scale combine\n"
                    "the functions of the selected graph, with coefficients: the result is a function that can be saved and fitted.\n"
                    "On a canvas of a plot file, functions of a registered class go into the plot file, others in a canvas below.");
  ImGui::BulletText("Shapes drawn on the canvases and fits are shown, not written to the plot file.");
  ImGui::BulletText("PlotHistos plots.xml opens DatumGui on a plot file; PlotHistos --all plots.xml writes its outputs.");
  }
  } // namespace

  // ------------------------------------------------------------------------------------------------ application

  DatumGuiApp::DatumGuiApp(Mode mode, const std::vector<String> & plotFiles, int argc, char ** argv)
  :
  DatumApp(argc, argv),
  _mode(mode),
  _plotFiles(plotFiles)
  {
#ifdef DATUM_WITH_HEPDATA
  _hepDataDialog.onImport = [this](const String & name, const HEPDataImportPlan & plan, String & message)
    { return importHEPData(name, plan, message); };
  _hepDataDialog.onStatus = [this](const String & message) { _gui.setStatus(message); };
#endif
  }

  DatumGuiApp::~DatumGuiApp()
  {
  // the canvases refer to the functions of the specification: emptied first
  for (int k=0; k<_gui.nPanes(); k++) _gui.pane(k).canvas() = GRAPHICS::Canvas();
  }

  void DatumGuiApp::buildMenus()
  {
  MenuBar & bar = _gui.menuBar();
  // Datum: the application and its windows
  Menu & datum = bar.addMenu("Datum");
  datum.addItem("About Datum",     "help.about",       "",             "Version and components");
  datum.addItem("Preferences...",  "edit.preferences", "Ctrl+,",       "Settings of DatumGui: theme, window, fonts, graphs, palettes, LaTeX");
  datum.addSeparator();
  datum.addItem("New Window",      "file.newWindow",   "Ctrl+Alt+N",   "Another DatumGui window: menus, a file browser of its own; "
                                                                       "panes (browsers, canvases, tables, tree, inspector) can be "
                                                                       "dragged between the windows");
  datum.addItem("Rename Window...", "gui.renameWindow", "",            "A new title for the window used last");
  datum.addItem("Close Window",    "gui.closeWindow",  "Ctrl+Shift+W", "Close the window used last, with its panes (Datum ends with the last window)");
  datum.addSeparator();
  datum.addItem("Quit Datum",      "file.exit",        "Ctrl+Q",       "Quit (asks to save the changes of the plot files and data files)");

  // File: the Datum data files and trees of the browser (the one selected in the browser used last)
  Menu & file = bar.addMenu("File");
  file.addItem("New",           "file.newBrowser", "",             "A new file browser, with files and folders of its own");
  file.addItem("New Data File", "data.new",        "Ctrl+Shift+N", "A new data file in the browser used last: an entry with an empty "
                                                                   "top folder; drag objects and folders onto it, save it as a Datum file");
  file.addItem("Open...",      "file.open",      "Ctrl+Shift+O", "Open a Datum file (or the histograms of a ROOT file) in the browser, or a table of data in a table pane");
  file.addItem("Duplicate",    "data.duplicate", "",             "A new data file holding a copy of the folders and objects of the file selected");
  file.addItem("Rename...",    "data.rename",    "",             "A new name for the folder or histogram selected in a data file made in DatumGui (or its top folder); "
                                                                  "files read from disk keep their names");
  file.addSeparator();
  file.addItem("Close",        "file.closeTree", "",             "Close the file selected in the browser, with its folders (asks to save its changes)");
  file.addItem("Close All",    "data.closeAll",  "",             "Close all the data files of the browsers (asks to save their changes)");
  file.addSeparator();
  file.addItem("Save",         "data.save",      "",             "Save the file selected, new or changed, to its Datum file");
  file.addItem("Save As...",   "data.saveAs",    "",             "Save the file selected to a new Datum file");
  file.addSeparator();
#ifdef DATUM_WITH_HEPDATA
  Menu & dataImport = file.addMenu("Import", "A new data file made from files of other formats");
  dataImport.addItem("HEPData YAML...", "data.importHEPData", "", "Tables of HEPData submissions (a folder, an archive or YAML "
                                                                  "files) as histograms, points and functions: a preview "
                                                                  "shows what will be made before OK");
#endif
  Menu & dataExport = file.addMenu("Export", "The file selected in other formats");
#ifdef DATUM_WITH_ROOT
  dataExport.addItem("ROOT...",       "data.exportRoot",  "", "All the objects of the file selected, converted, to a ROOT file");
#endif
  dataExport.addItem("PDF...",        "data.exportPdf",   "", "Every histogram of the file selected drawn on a page of its own, in one PDF file");
  dataExport.addItem("LaTeX...",      "data.exportLatex", "", "Every histogram of the file selected drawn in a figure of its own, in a LaTeX document");
  dataExport.addItem("PowerPoint...", "data.exportPptx",  "", "Every histogram of the file selected drawn on a slide of its own");
#ifdef DATUM_WITH_HEPDATA
  dataExport.addSeparator();
  dataExport.addItem("HEPData YAML...", "data.exportHEPData", "", "Histograms, points, fits and functions of the open data files "
                                                                  "as a HEPData submission, with the publication and the keywords: "
                                                                  "a preview shows the files before OK writes them");
#endif

  // Plots: the plot files (XML), the tabs of the Plot Tree
  Menu & plots = bar.addMenu("Plots");
  plots.addItem("New",                 "plot.new",       "Ctrl+N",       "A new, untitled plot file (a new tab of the Plot Tree)");
  plots.addItem("New From Data File...", "plot.fromData", "",            "A new plot file with windows, canvases and graphs of the histograms and functions of a Datum file (or of a RunDatum workflow)");
  plots.addItem("Open...",             "plot.open",      "Ctrl+O",       "Open a plot file (XML) in a new tab of the Plot Tree");
  plots.addItem("Duplicate",           "plot.duplicate", "",             "A copy of the plot file shown, in a new, untitled tab");
  plots.addItem("Rename...",           "plot.rename",    "",             "A new name for the plot file shown (a saved file is renamed in its folder)");
  plots.addSeparator();
  plots.addItem("Close",               "plot.close",     "Ctrl+W",       "Close the plot file shown, asking to save its changes");
  plots.addItem("Close All",           "plot.closeAll",  "",             "Close all the plot files, asking to save their changes");
  plots.addSeparator();
  plots.addItem("Save",                "plot.save",      "Ctrl+S",       "Save the plot file shown");
  plots.addItem("Save As...",          "plot.saveAs",    "Ctrl+Shift+S", "Save the plot file shown under a new name");
  plots.addSeparator();
  plots.addItem("Add Graph",           "plot.addGraph",    "",         "A new graph in the canvas of the selection");
  plots.addItem("Add Function",        "plot.addFunction", "",         "A function in the graph of the selection");
  plots.addItem("Regroup...",          "plot.regroup",     "",         "Gather the histograms and functions whose names match a pattern "
                                                                       "(e.g. *_pt) into common graphs, after a preview");
  plots.addItem("Move Up",             "plot.up",          "",         "Move the selection before the previous element");
  plots.addItem("Move Down",           "plot.down",        "",         "Move the selection after the next element");
  plots.addSeparator();
  Menu & plotExport = plots.addMenu("Export", "Figures and documents of all the canvases of the plot file shown, in its output folder");
  plotExport.addItem("PDF Book",       "plot.pdf",    "", "All the canvases of the plot file, one per page, to the PDF book of the output folder");
  plotExport.addItem("LaTeX",          "plot.latex",  "", "A Beamer presentation or an article of all the canvases of the plot file");
  plotExport.addItem("PowerPoint",     "plot.pptx",   "", "All the canvases of the plot file, one per slide (native shapes)");
  plotExport.addItem("Named Plots",    "plot.print",  "", "Each canvas and graph of the plot file that has a file name, to that file in the output folder");
  plotExport.addItem("All Outputs",    "plot.all",    "", "Named plots, PDF book, PowerPoint and LaTeX");
  plotExport.addSeparator();
  plotExport.addItem("Output Folder...", "plot.folder", "", "Choose the folder where the outputs of the plot file go");

  // Windows: the windows of the plot file shown (the one in the main window)
  Menu & windows = bar.addMenu("Windows");
  windows.addItem("New",        "win.new",       "", "A new window of the plot file, with one empty canvas");
  windows.addItem("Open...",    "win.open",      "", "Add the windows of a plot file (XML) to the plot file shown");
  windows.addItem("Duplicate",  "win.duplicate", "", "A copy of the window shown, with all its canvases, in a new window");
  windows.addItem("Rename...",  "win.rename",    "", "A new title for the window shown");
  windows.addSeparator();
  windows.addItem("Close",      "win.close",     "", "Remove the window shown from the plot file, with its canvases (Undo restores it)");
  windows.addItem("Close All",  "win.closeAll",  "", "Remove all the windows of the plot file (Undo restores them)");
  windows.addSeparator();
  Menu & windowFormat = windows.addMenu("Format", "Canvases of the window shown: columns x rows");
  windowFormat.addWidget([this]() { gridMenu(false); });
  windows.addSeparator();
  windows.addItem("Save",       "win.save",      "", "Save the window shown again to the file it was saved to (PDF, SVG, XML or LaTeX)");
  windows.addItem("Save As...", "win.saveAs",    "", "Save the window shown to a PDF or SVG file (as on screen), an XML plot file or a LaTeX document");
  windows.addSeparator();
  Menu & windowExport = windows.addMenu("Export", "The canvases of the window shown");
  windowExport.addItem("PDF Files...",  "win.exportPdfs",  "", "Each canvas of the window to a PDF file of its own, in a folder");
  windowExport.addItem("Book...",       "win.exportBook",  "", "All the canvases of the window, one per page, in one PDF file");
  windowExport.addItem("LaTeX...",      "win.exportLatex", "", "All the canvases of the window in a LaTeX document");
  windowExport.addItem("PowerPoint...", "win.exportPptx",  "", "All the canvases of the window, one per slide");

  // Windows of the plot file: their display (moved from View), the list of the canvases
  windows.addSeparator();
  windows.addItem("All Windows",      "plot.allWindows",      "", "Every window of the plot file: the first one in its DatumGui window, "
                                                                  "the others in windows of their own (otherwise the window of the selection)").setCheckable(true);
  windows.addItem("Detached Windows", "plot.detachedWindows", "", "Every window of the plot file in a window of its own").setCheckable(true);
  windows.addItem("Canvases List",    "view.canvases",        "", "The canvases of the session by window, with their graphs").setCheckable(true);

  // Canvas: the current canvas
  Menu & canvas = bar.addMenu("Canvas");
  canvas.addItem("New",        "canvas.new",       "", "A new canvas: in the window of the plot file shown, otherwise in the window used last");
  canvas.addItem("Open...",    "canvas.open",      "", "Add a canvas of a saved plot file (XML) to the window of the plot file shown");
  canvas.addItem("Duplicate",  "canvas.duplicate", "", "A copy of the current canvas, after it in the plot file");
  canvas.addItem("Rename...",  "canvas.rename",    "", "A new name (and title) for the current canvas");
  canvas.addItem("Close",      "window.close",     "", "Remove the current canvas (from the plot file, with its graphs: Undo restores it)");
  canvas.addSeparator();
  Menu & canvasFormat = canvas.addMenu("Format", "Graphs of the current canvas: columns x rows");
  canvasFormat.addWidget([this]() { gridMenu(true); });
  canvas.addSeparator();
  canvas.addItem("Save",       "canvas.save",      "", "Save the current canvas again to the file it was saved to");
  Menu & canvasSaveAs = canvas.addMenu("Save As", "Save the current canvas to a file of a format");
  canvasSaveAs.addItem("PDF...",           "canvas.saveAs.pdf",  "", "The canvas as on screen, in a PDF file");
  canvasSaveAs.addItem("SVG...",           "canvas.saveAs.svg",  "", "The canvas as on screen, in an SVG file");
  canvasSaveAs.addItem("PNG...",           "canvas.saveAs.png",  "", "The canvas as on screen, in a PNG image (150 dpi)");
  canvasSaveAs.addItem("LaTeX...",         "canvas.saveAs.tex",  "", "The canvas in a LaTeX document");
  canvasSaveAs.addItem("PowerPoint...",    "canvas.saveAs.pptx", "", "The canvas on a PowerPoint slide (native shapes)");
  canvasSaveAs.addItem("XML Plot File...", "canvas.saveAs.xml",  "", "The canvas in a plot file of its own, with its inputs");
  canvas.addSeparator();
  canvas.addItem("Clear",      "canvas.clear",     "", "Remove the histograms and functions of all the graphs of the current canvas");
  canvas.addItem("Clear All",  "canvas.clearAll",  "", "Remove the histograms and functions of all the canvases shown");

  // Edit: the selection (Plot Tree or canvas)
  Menu & edit = bar.addMenu("Edit");
  edit.addItem("Undo",   "plot.undo",  "Ctrl+Z",       "Undo the last change of the plot file (made on the canvases or in the tree)");
  edit.addItem("Redo",   "plot.redo",  "Ctrl+Shift+Z", "Redo the change undone last");
  edit.addSeparator();
  edit.addItem("Cut",    "plot.cut",   "Ctrl+X", "Copy the selection (graph, canvas, histogram, ...), then remove it");
  edit.addItem("Copy",   "plot.copy",  "Ctrl+C", "Copy the selection: its XML also goes to the clipboard");
  edit.addItem("Paste",  "plot.paste", "Ctrl+V", "Paste what was copied where it belongs, at the selection");
  edit.addItem("Duplicate", "file.duplicate", "Ctrl+D", "A copy of the selection, after it: the canvas, graph, histogram or function "
                                                        "selected in the Plot Tree, or the entry selected in the browser if it was used last");
  edit.addItem("Delete", "plot.delete", "Delete", "Remove the shape selected on a canvas, otherwise the selection");
  edit.addSeparator();
  edit.addItem("Clear",     "edit.clearGraph",  "", "Remove the histograms, functions and shapes of the selected graph");
  edit.addItem("Clear All", "edit.clearCanvas", "", "Remove the histograms, functions and shapes of all the graphs of the current canvas");

  // View: panes shown or hidden
  Menu & view = bar.addMenu("View");
  Menu & viewBrowsers = view.addMenu("File Browsers", "Show or hide the file browser used last, or all of them");
  viewBrowsers.addItem("Current", "view.panes.browsers.current", "Ctrl+B", "Show or hide the file browser used last").setCheckable(true);
  viewBrowsers.addItem("All",     "view.panes.browsers.all",     "",       "Show or hide all the file browsers").setCheckable(true);
  view.addItem("Plot Browser",   "plot.tree",      "Ctrl+T", "Show or hide the Plot Tree: the plot files, their windows, canvases and graphs").setCheckable(true);
  view.addItem("Inspector",      "plot.inspector", "",       "Show or hide the inspector: all the settings of the element selected in the tree").setCheckable(true);
  Menu & viewTables = view.addMenu("Spreadsheets", "Show or hide the table pane used last, or all of them");
  viewTables.addItem("Current", "view.panes.tables.current", "", "Show or hide the table pane used last").setCheckable(true);
  viewTables.addItem("All",     "view.panes.tables.all",     "", "Show or hide all the table panes").setCheckable(true);
  Menu & viewData = view.addMenu("Data Panes", "Show or hide the data pane used last, or all of them");
  viewData.addItem("Current", "view.panes.data.current", "", "Show or hide the data pane used last").setCheckable(true);
  viewData.addItem("All",     "view.panes.data.all",     "", "Show or hide all the data panes").setCheckable(true);
  view.addSeparator();
  view.addItem("Style Elements", "plot.showStyle", "", "Show the elements of style (Margins, Axis, Marker, ...) in the tree").setCheckable(true);
  view.addItem("Fit Results",    "view.fits",      "", "The results of the fits of the selected graph, below the canvases").setCheckable(true);
  view.addItem("Canvas Toolbars", "view.toolbars", "", "Show the buttons above each canvas").setCheckable(true);
  view.addItem("Legends",        "view.legends",   "", "Show the legend of every graph").setCheckable(true);
  if (MenuItem * item = bar.findItem("view.toolbars")) item->setChecked(_configuration.gui.toolbars);
  if (MenuItem * item = bar.findItem("view.legends"))  item->setChecked(_configuration.legend);

  // Maps: detector maps (efficiencies and smearers)
  Menu & maps = bar.addMenu("Maps");
  maps.addItem("Detector Maps...", "edit.maps", "", "The particle efficiencies and smearers of a maps file (RunDatum: Import)");

  addOperationsMenu(bar);
  addFunctionsMenu(bar);
  Menu & help = bar.addMenu("Help");
  help.addItem("DatumGui Help",        "plot.help",      "F1", "How the canvases, the plot files and the browser work together");
  help.addItem("Keyboard Shortcuts", "help.shortcuts", "",   "List of the keyboard shortcuts");
#ifdef DATUM_WITH_HEPDATA
  help.addItem("HEPData Import and Export", "help.hepdata", "", "How HEPData YAML files become histograms, points and functions, and back");
#endif
  help.addSeparator();
  help.addItem("ImGui Demo (developers)", "help.imguiDemo", "", "Dear ImGui's demonstration window").setCheckable(true);
  }

  void DatumGuiApp::showFunction(std::unique_ptr<MATH::ParameterizedFunction> f, double xMin, double xMax, double yMin, double yMax)
  {
  Tab * t = tab();
  Path canvas, graph;
  bool registered = f && MATH::FunctionRegistry::isRegistered(*f) && !dynamic_cast<const MATH::ParameterizedComposite *>(f.get());
  // the window of the plot file used, without a canvas of the plot file (a new plot file): a window, a canvas and a
  // graph made in the file for a function it can describe
  if (t && f && registered && !currentCanvas(canvas, graph) && _gui.nPanes() > 0
      && _gui.windowOfPane(_gui.currentPaneIndex()) >= 0 && _gui.currentPane().host() >= 0)
    {
    PlotDocument & d = *t->document;
    d.beginEdit();
    Path w;
    if (!currentWindow(w)) w = d.addWindow();
    canvas = d.addCanvas(w);
    graph = canvas.empty() ? Path() : d.addGraph(canvas);
    d.endEdit();
    }
  else if (!t || !f || !currentCanvas(canvas, graph))
    {
    DatumApp::showFunction(std::move(f), xMin, xMax, yMin, yMax);
    return;
    }
  if (graph.empty()) { DatumApp::showFunction(std::move(f), xMin, xMax, yMin, yMax); return; }
  PlotDocument & d = *t->document;
  if (registered)
    {
    // a function of the plot file: its class, parameters and range in the selected graph
    std::string values;
    for (unsigned int k=0; k<f->nParameters(); k++)
      values += (k ? " " : "") + XmlElement::formatNumber(f->parameter(k).value()).str();
    d.beginEdit();
    Path p = d.addFunction(graph, MATH::FunctionRegistry::typeName(*f), String(values.c_str()), xMin, xMax);
    if (!p.empty())
      {
      if (f->rank() == 2)
        {
        d.setAttribute(p, "yMin", XmlElement::formatNumber(yMin));
        d.setAttribute(p, "yMax", XmlElement::formatNumber(yMax));
        }
      String name(f->name());
      if (!name.isEmpty()) { d.setAttribute(p, "name", name); d.setAttribute(p, "label", name); }
      t->selected = p;
      t->hasSelection = true;
      }
    d.endEdit();
    return;
    }
  // a composite (not described by plot files): a canvas of its own, below the canvas of the plot file
  CanvasPane & below = _gui.addPaneBelow(_gui.currentPane().windowId());
  below.setTitle(String(f->name()));
  DatumApp::showFunctionIn(below, std::move(f), xMin, xMax, yMin, yMax);
  _gui.setStatus("The function is shown in a canvas of its own: plot files describe functions of registered classes only");
  }

  bool DatumGuiApp::updateFunction(int item, std::unique_ptr<MATH::ParameterizedFunction> f, double xMin, double xMax,
                                   double yMin, double yMax, const String & name, String & error)
  {
  Tab * t = tab();
  Path canvas, graph;
  if (!t || !f || !currentCanvas(canvas, graph) || graph.empty())   // not a graph of the plot file
    return DatumApp::updateFunction(item, std::move(f), xMin, xMax, yMin, yMax, name, error);
  PlotDocument & d = *t->document;
  const XmlElement * g = d.element(graph);
  if (!g) { error = "The graph is no longer in the plot file"; return false; }
  // the element of the item: the items of a graph are its Histogram and Function elements, in order
  Path element;
  int k = 0;
  for (unsigned int j=0; j<g->nChildren(); j++)
    {
    PlotDocument::Kind kind = PlotDocument::kind(g->child(j));
    if (kind != PlotDocument::kHistogram && kind != PlotDocument::kFunction) continue;
    if (k++ != item) continue;
    if (kind == PlotDocument::kFunction) { element = graph; element.push_back(int(j)); }
    break;
    }
  if (element.empty()) { error = "That function is no longer in the selected graph"; return false; }
  if (!MATH::FunctionRegistry::isRegistered(*f) || dynamic_cast<const MATH::ParameterizedComposite *>(f.get()))
    { error = "Plot files describe functions of registered classes only (use Add: the function is shown in a canvas of its own)"; return false; }
  std::string values;
  for (unsigned int p=0; p<f->nParameters(); p++)
    values += (p ? " " : "") + XmlElement::formatNumber(f->parameter(p).value()).str();
  d.beginEdit();
  d.setAttribute(element, "class", MATH::FunctionRegistry::typeName(*f));
  d.setAttribute(element, "parameters", String(values.c_str()));
  d.setAttribute(element, "xMin", XmlElement::formatNumber(xMin));
  d.setAttribute(element, "xMax", XmlElement::formatNumber(xMax));
  if (f->rank() == 2)
    {
    d.setAttribute(element, "yMin", XmlElement::formatNumber(yMin));
    d.setAttribute(element, "yMax", XmlElement::formatNumber(yMax));
    }
  if (!name.isEmpty()) { d.setAttribute(element, "name", name); d.setAttribute(element, "label", name); }
  d.endEdit();
  t->selected = element;
  t->hasSelection = true;
  return true;
  }

  void DatumGuiApp::updateMenuStates()
  {
  DatumApp::updateMenuStates();
  MenuBar & bar = _gui.menuBar();
  Tab * t = tab();
  bool any = t != nullptr, selected = t && t->hasSelection && !t->selected.empty();
  auto enable = [&](const char * c, bool v) { if (MenuItem * item = bar.findItem(c)) item->setEnabled(v); };
  enable("plot.close", any);
  enable("plot.duplicate", any);
  enable("plot.save", any);
  enable("plot.saveAs", any);
  enable("file.duplicate", any);
  enable("plot.closeAll", any);
  enable("plot.newWindow", any);
  enable("plot.newCanvas", any);
  enable("plot.newCanvasWindow", any);
  enable("plot.regroup", any);
  enable("plot.closeWindow", any && !t->document->children(PlotDocument::Path(), PlotDocument::kWindow).empty());
  {
  bool windows = any && !t->document->children(PlotDocument::Path(), PlotDocument::kWindow).empty();
  Path canvas, graph;
  bool canvasOfPlot = currentCanvas(canvas, graph);
  for (const char * c : {"win.duplicate", "win.close", "win.closeAll", "win.save", "win.saveAs", "win.exportPdfs",
                         "win.exportBook", "win.exportLatex", "win.exportPptx"}) enable(c, windows);
  enable("win.new", any);
  enable("win.open", any);
  enable("canvas.save", canvasOfPlot);
  enable("canvas.saveAs", canvasOfPlot);
  enable("canvas.duplicate", canvasOfPlot);
  for (const char * c : {"canvas.saveAs.tex", "canvas.saveAs.pptx", "canvas.saveAs.xml"}) enable(c, canvasOfPlot);
  for (const char * c : {"canvas.saveAs.pdf", "canvas.saveAs.svg", "canvas.saveAs.png"}) enable(c, canvasOfPlot || _gui.nPanes() > 0);
  for (const char * c : {"canvas.clear", "canvas.clearAll", "edit.clearGraph", "edit.clearCanvas"}) enable(c, _gui.nPanes() > 0);
  if (Menu * w = bar.findMenu("Windows")) if (Menu * f = w->findMenu("Format")) f->setEnabled(windows);
  if (Menu * c = bar.findMenu("Canvas")) if (Menu * f = c->findMenu("Format")) f->setEnabled(canvasOfPlot);
  String path;
  bool folder = false;
  BrowserFile * data = selectedEntry(path, folder);
  for (const char * c : {"data.duplicate", "data.saveAs", "data.exportRoot", "data.exportPdf", "data.exportLatex",
                         "data.exportPptx"}) enable(c, data != nullptr);
  enable("data.save", data && data->editable && (data->modified || data->path.isNull()));
  enable("data.closeAll", !_files.empty());
#ifdef DATUM_WITH_HEPDATA
  enable("data.exportHEPData", !_files.empty());
#endif
  enable("data.rename", _gui.activeBrowser().canRename());
  enable("win.rename", windows);
  enable("canvas.rename", canvasOfPlot);
  enable("plot.rename", any);
  }
  enable("window.close", _gui.nPanes() > 0);
  enable("file.closeBrowser", _gui.browserVisible() || _gui.browsers().size() > 1);
  {
  String path;
  bool folder = false;
  enable("file.closeTree", selectedEntry(path, folder) != nullptr);
  }
  enable("plot.undo", any && t->document->canUndo());
  enable("plot.redo", any && t->document->canRedo());
  enable("plot.cut", selected);
  enable("plot.copy", selected);
  enable("plot.paste", any && !_clipboard.empty());
  enable("plot.duplicateElement", selected);
  enable("plot.up", selected);
  enable("plot.down", selected);
  enable("plot.fromData", bool(_catalog));
  if (MenuItem * item = bar.findItem("plot.tree"))       item->setChecked(_showTree);
  if (MenuItem * item = bar.findItem("plot.inspector"))  item->setChecked(_showInspector);
  if (MenuItem * item = bar.findItem("plot.showStyle"))  item->setChecked(_showStyle);
  if (MenuItem * item = bar.findItem("plot.allWindows")) item->setChecked(_allWindows);
  if (MenuItem * item = bar.findItem("plot.detachedWindows")) item->setChecked(_detachedWindows);
  // View: the panes shown, the current one of each kind or all of them
  const std::pair<const char *, DatumGui::PaneKind> kinds[] = { {"browsers", DatumGui::kBrowserPanes},
                                                                {"tables", DatumGui::kTablePanes}, {"data", DatumGui::kDataPanes} };
  for (const auto & kind : kinds)
    for (bool all : {false, true})
      if (MenuItem * item = bar.findItem(String("view.panes.") + String(kind.first) + String(all ? ".all" : ".current")))
        {
        item->setEnabled(_gui.nPanesOf(kind.second) > 0);
        item->setChecked(_gui.panesShown(kind.second, all));
        }
  }

  void DatumGuiApp::connectToPlot(FileBrowser & browser)
  {
  browser.onShowObject = [this](const ObjectReference & object) { showObject(object); };
  browser.onShowFolder = [this](int fileId, const String & folderPath) { showFolder(ObjectReference{fileId, folderPath}); };
  }

  void DatumGuiApp::onStartup()
  {
  _detachedWindows = configuration().gui.detachedWindows;
  _gui.setSideWindow(kEditorWindow, 0.40f);
  _gui.setSideWindowVisible(_showTree);       // hidden until a plot file is made or opened, or View > Plot Browser
  std::function<void()> previous = _gui.onFrame;
  _gui.onFrame = [this, previous]() { if (previous) previous(); renderFrame(); };
  for (const auto & b : _gui.browsers()) connectToPlot(*b);
  std::function<void(FileBrowser &)> previousBrowser = _gui.onNewBrowser;
  _gui.onNewBrowser = [this, previousBrowser](FileBrowser & b) { if (previousBrowser) previousBrowser(b); connectToPlot(b); };
  _gui.onClosePanes = [this](const std::vector<int> & panes) { return closeCanvases(panes); };
  // the x of the DatumGui window of the plot files: the plot file shown is closed (asked to be saved first)
  _gui.onCloseWindow = [this](int window) -> bool
    {
    if (window == 0 && _current >= 0 && _current < int(_documents.size())) { requestClose(_current); return false; }
    return true;
    };
  // a DatumGui window closed: the Plot Tree and the inspector go with it
  _gui.onWindowClosing = [this](int window)
    {
    if (_showTree && _gui.windowOf(kEditorWindow) == window) { _showTree = false; _gui.setSideWindowVisible(false); }
    if (_showInspector && _gui.windowOf(kInspectorWindow) == window) _showInspector = false;
    };
  // "Rename Canvas..." of the context menu of a graph: the canvas of the plot file (its name and title)
  auto hookRename = [this](CanvasPane & pane)
    {
    std::function<void()> previous = pane.onRename;
    pane.onRename = [this, &pane, previous]()
      {
      for (const auto & link : _links) if (link.pane == &pane && !link.canvas.empty()) { requestRename(3, link.canvas); return; }
      if (previous) previous();
      };
    };
  for (int k=0; k<_gui.nPanes(); k++) hookRename(_gui.pane(k));
  std::function<void(CanvasPane &)> previousPane = _gui.onNewPane;
  _gui.onNewPane = [previousPane, hookRename](CanvasPane & pane) { if (previousPane) previousPane(pane); hookRename(pane); };
  _gui.onCloseRequest = [this]() -> bool
    {
    // the trees changed first (Cancel leaves the plot files as they are), then the plot files changed
    if (!confirmTreesBeforeExit()) return false;
    _closing.clear();
    for (const auto & t : _documents) if (t.document->isDirty()) _closing.push_back(t.id);
    if (_closing.empty()) return true;
    _exitAfterClosing = true;
    _closeDialog = true;
    return false;
    };
  for (const String & f : _plotFiles) openDocument(f);
  if (!_plotFiles.empty()) showTree();        // plot files given: asked for
  if (!_inputFiles.empty() && !_documents.empty())
    {
    // files replacing the inputs of the first plot file (as shown, not a change of the file)
    XmlElement & root = _documents.front().document->document().root();
    std::size_t k = 0;
    for (XmlElement * e : root.children())
      if (e->isNamed("Input") && k < _inputFiles.size()) e->setAttribute("file", FileSystem::normalize(_inputFiles[k++]));
    for (; k < _inputFiles.size(); k++)
      {
      XmlElement in("Input");
      in.setAttribute("id", String("input") + String(std::to_string(k)));
      in.setAttribute("file", FileSystem::normalize(_inputFiles[k]));
      root.insertChild(unsigned(k), in);
      }
    }
  if (_documents.empty())
    {
    // BASIC: the window starts with the file browser only (a canvas is made when an object is shown; Plots > New makes
    // a plot file); EDIT: a plot file to edit
    if (_mode == kEdit) newDocument();
    if (_mode == kEdit && !_templateOpen) actionPerformed(ActionEvent{"plot.open", nullptr});
    else if (_mode == kBasic)
      {
      bool folders = false;
      for (const auto & b : _gui.browsers()) if (!b->folders().empty()) folders = true;
      if (!folders) openFolder(dataFolder());   // the Data I/O default folder (Preferences > Files), or the current one
      }
    }
  _gui.setStatus(_mode == kBasic ? "Click or drag histograms or folders from the browser onto the canvas (View > Plot Browser shows the plot file)"
                                 : "The canvases are the plot file: change them, or the tree, and save the file");
  }

  int DatumGuiApp::newDocument()
  {
  Tab t;
  t.document.reset(new PlotDocument());
  t.document->setUntitledName(String("Untitled ") + String(std::to_string(++_untitled)));
  t.id = _nextId++;
  _documents.push_back(std::move(t));
  _current = int(_documents.size()) - 1;
  _selectTab = _current;
  return _current;
  }

  bool DatumGuiApp::openDocument(const String & fileName)
  {
  String path = FileSystem::normalize(fileName);
  for (std::size_t k=0; k<_documents.size(); k++)
    if (_documents[k].document->fileName() == path) { _current = int(k); _selectTab = _current; return true; }
  std::unique_ptr<PlotDocument> d(new PlotDocument());
  String error;
  if (!d->read(path, error)) { _gui.showMessage("Open", path + String(":\n") + error); return false; }
  // an untitled, unchanged, empty tab is replaced
  if (Tab * t = tab())
    if (t->document->fileName().isNull() && !t->document->isDirty() && !t->document->canUndo())
      {
      t->document = std::move(d);
      t->selected.clear();
      t->hasSelection = false;
      _selectTab = _current;
      _gui.setStatus(String("Opened ") + path);
      return true;
      }
  Tab t;
  t.document = std::move(d);
  t.id = _nextId++;
  _documents.push_back(std::move(t));
  _current = int(_documents.size()) - 1;
  _selectTab = _current;
  _gui.setStatus(String("Opened ") + path);
  return true;
  }

  void DatumGuiApp::select(const Path & path)
  {
  if (Tab * t = tab()) { t->selected = path; t->hasSelection = true; }
  }

  void DatumGuiApp::requestClose(int k)
  {
  if (k < 0 || k >= int(_documents.size())) return;
  if (!_documents[std::size_t(k)].document->isDirty()) { closeTab(k); return; }
  _closing = { _documents[std::size_t(k)].id };
  _exitAfterClosing = false;
  _closeDialog = true;
  }

  void DatumGuiApp::closeTab(int k)
  {
  if (k < 0 || k >= int(_documents.size())) return;
  _documents.erase(_documents.begin() + k);
  if (_current >= int(_documents.size())) _current = int(_documents.size()) - 1;
  _selectTab = _current;
  _drawnTab = -1;
  if (_documents.empty() && !_exitAfterClosing && _mode == kEdit) newDocument();   // EDIT: always a plot file (BASIC: its window closes)
  }

  void DatumGuiApp::save(int k, bool ask, const std::function<void()> & after)
  {
  if (k < 0 || k >= int(_documents.size())) return;
  int id = _documents[std::size_t(k)].id;
  auto write = [this, id, after](const String & fileName)
    {
    for (auto & t : _documents)
      {
      if (t.id != id) continue;
      String name = FileSystem::hasExtension(fileName, ".xml") ? fileName : fileName + String(".xml");
      String error;
      if (!t.document->write(name, error)) { _gui.showMessage("Save", name + String(":\n") + error); return; }
      _gui.setStatus(String("Saved ") + name);
      if (after) after();
      return;
      }
    };
  PlotDocument & d = *_documents[std::size_t(k)].document;
  if (!ask && !d.fileName().isNull()) { write(d.fileName()); return; }
  String start = d.fileName().isNull() ? _configuration.directory : d.directory();
  String name = d.fileName().isNull() ? String("plots.xml") : d.displayName();
  _gui.fileDialog().open(FileDialog::kSaveFile, "Save the plot file", start, {".xml"}, name, write);
  }

  void DatumGuiApp::saveChangedDocuments(const std::function<void()> & after)
  {
  for (std::size_t k=0; k<_documents.size(); k++)
    if (_documents[k].document->isDirty())
      {
      int id = _documents[k].id;
      save(int(k), false, [this, id, after]()
        {
        for (const auto & t : _documents) if (t.id == id && t.document->isDirty()) return;   // not saved: stop
        saveChangedDocuments(after);
        });
      return;
      }
  if (after) after();
  }

  bool DatumGuiApp::currentWindow(Path & window)
  {
  Tab * t = tab();
  if (!t) return false;
  PlotDocument & d = *t->document;
  std::vector<Path> windows = d.children(Path(), PlotDocument::kWindow);
  if (windows.empty()) return false;
  bool ok = false;
  if (t->hasSelection)
    {
    Path w = d.enclosing(t->selected, PlotDocument::kWindow, ok);
    if (ok) { window = w; return true; }
    }
  int k = (_drawnTab == t->id && _drawnWindow >= 0 && _drawnWindow < int(windows.size())) ? _drawnWindow : 0;
  window = windows[std::size_t(k)];
  return true;
  }

  bool DatumGuiApp::browserIsCurrent()
  {
  if (_gui.lastFocus() != DatumGui::kFocusBrowser) return false;
  String path;
  bool folder = false;
  return selectedEntry(path, folder) != nullptr;
  }

  void DatumGuiApp::duplicateCurrent()
  {
  if (browserIsCurrent())
    {
    actionPerformed(ActionEvent{"file.duplicateEntry", nullptr});
    return;
    }
  Tab * t = tab();
  if (!t) return;
  PlotDocument & d = *t->document;
  if (t->hasSelection && !t->selected.empty() && d.element(t->selected))
    {
    PlotDocument::Kind kind = PlotDocument::kind(*d.element(t->selected));
    Path copy = d.duplicate(t->selected);
    if (copy.empty()) return;
    t->selected = copy;
    const char * what = kind == PlotDocument::kWindow ? "window" : kind == PlotDocument::kCanvas ? "canvas"
                      : kind == PlotDocument::kGraph ? "graph" : kind == PlotDocument::kHistogram ? "histogram"
                      : kind == PlotDocument::kFunction ? "function" : "element";
    _gui.setStatus(String("Duplicated the ") + String(what) + String(" (Undo to remove the copy)"));
    return;
    }
  actionPerformed(ActionEvent{"plot.duplicate", nullptr});
  }

  std::vector<String> DatumGuiApp::objectsOf(const ObjectReference & object, bool folder, String & fileName)
  {
  std::vector<String> objects;
  BrowserFile * f = file(object.file);
  if (!f || !f->file) return objects;
  fileName = dataPath(*f);                             // a tree not saved yet: a path of its own
  if (!folder) { objects.push_back(object.path); return objects; }
  Folder * dir = object.path.isNull() ? &f->file->folder() : f->file->folder().folder(object.path);
  if (!dir) return objects;
  std::function<void(Folder &, const String &, int)> collect = [&](Folder & d, const String & prefix, int depth)
    {
    for (const String & name : d.names())
      {
      String path = prefix.isNull() ? name : prefix + String("/") + name;
      if (Folder * sub = d.folder(name)) { if (depth > 0) collect(*sub, path, depth - 1); continue; }
      String kind, details;
      if (FileBrowser::describe(d, name, kind, details)) objects.push_back(path);
      }
    };
  collect(*dir, object.path, 0);
  if (objects.empty()) collect(*dir, object.path, 3);   // no histogram here: those of the sub-folders
  return objects;
  }

  bool DatumGuiApp::drop(const Path & target, const ObjectReference & object, bool folder)
  {
  Tab * t = tab();
  if (!t) return false;
  String fileName;
  std::vector<String> objects = objectsOf(object, folder, fileName);
  if (objects.empty() || fileName.isNull()) { _gui.setStatus("Nothing to plot there"); return false; }
  if (objects.size() > 48) objects.resize(48);
  Path added = t->document->addData(target, fileName, objects);
  if (added.empty()) return false;
  t->selected = added;
  t->hasSelection = true;
  _gui.setStatus(String(std::to_string(objects.size())) + String(objects.size() > 1 ? " histograms added" : " histogram added"));
  return true;
  }

  // ------------------------------------------------------------------------------------------------ commands

  void DatumGuiApp::actionPerformed(const ActionEvent & event)
  {
  const String & c = event.command;
  Tab * t = tab();
  PlotDocument * d = t ? t->document.get() : nullptr;
  bool selected = t && t->hasSelection && !t->selected.empty() && d->element(t->selected);
  int columns = 0, rows = 0;
  if (c == "plot.new") { newDocument(); showTree(); }
  else if (c == "gui.renameWindow" && _gui.currentWindow() >= 0) actionPerformed(ActionEvent{"win.rename", nullptr});   // its title: that of the window of the plot file
  else if (c == "plot.open")
    _gui.fileDialog().open(FileDialog::kOpenFile, "Open a plot file", _configuration.directory, {".xml"}, "",
                           [this](const String & path) { if (openDocument(path)) showTree(); });
  else if (c == "plot.fromData")
    _gui.fileDialog().open(FileDialog::kOpenFile, "Datum file or RunDatum workflow (.xml) whose histograms to plot",
                           _configuration.directory, {DatumFile::extension(), DatumFile::formerExtension(), ".xml"}, "",
      [this](const String & path)
      {
      GRAPHICS::PlotTemplate catalog;
      String problems;
      if (!_catalog || !_catalog({ path }, String(""), catalog, problems))
        { _gui.showMessage("New from Data", String("No histogram found in ") + path + String("\n") + problems); return; }
      if (!problems.isNull()) _gui.setStatus(String("New from Data: ") + problems);
      startTemplate(catalog, GRAPHICS::PlotTemplate::Options(), String(""));
      showTree();
      });
  else if (c == "plot.close") requestClose(_current);
  else if (c == "plot.duplicate" && d)
    {
    int from = _current;
    int k = newDocument();
    _documents[std::size_t(k)].document->document() = _documents[std::size_t(from)].document->document();
    _documents[std::size_t(k)].document->setUntitledName(String("Copy of ") + _documents[std::size_t(from)].document->displayName());
    _documents[std::size_t(k)].document->setDirty(true);
    }
  else if (c == "plot.save" && d) save(_current, false);
  else if (c == "plot.saveAs" && d) save(_current, true);
  else if (c == "plot.saveAll") saveChangedDocuments();
  else if (c == "file.saveItem" || c == "file.saveItemAs")
    {
    bool ask = c == "file.saveItemAs";
    if (browserIsCurrent())
      {
      String path;
      bool folder = false;
      BrowserFile * entry = selectedEntry(path, folder);
      if (entry && entry->editable) { saveTree(entry->id, ask); return; }
      }
    if (d) save(_current, ask);
    }
  else if (c == "file.saveEverything") saveChangedDocuments([this]() { saveChangedTrees([this]() { _gui.setStatus("All saved"); }); });
  else if (c == "file.duplicate") duplicateCurrent();
  else if (c == "plot.newWindow" && d)
    {
    t->selected = d->addWindow();
    t->hasSelection = true;
    _gui.setStatus("New window: Canvas > New adds a canvas to it, or drop histograms on it in the Plot Tree");
    }
  else if ((c == "plot.newCanvas" || c == "plot.newCanvasWindow") && d)
    {
    Path w;
    d->beginEdit();
    if (c == "plot.newCanvasWindow" || !currentWindow(w)) w = d->addWindow();
    t->selected = d->addCanvas(w);
    d->endEdit();
    t->hasSelection = true;
    }
  else if (c == "plot.closeWindow" && d)
    {
    Path w;
    if (!currentWindow(w)) { _gui.setStatus("No window in the plot file"); return; }
    std::vector<Path> windows = d->children(Path(), PlotDocument::kWindow);
    std::size_t k = std::size_t(std::find(windows.begin(), windows.end(), w) - windows.begin());
    if (!d->remove(w)) return;
    windows = d->children(Path(), PlotDocument::kWindow);
    t->hasSelection = !windows.empty();
    t->selected = windows.empty() ? Path() : windows[std::min(k, windows.size() - 1)];   // the next one shown
    _drawnWindow = -1;
    _gui.setStatus("Window removed from the plot file (Undo to restore it)");
    }
  else if (c == "plot.undo" && d) d->undo();
  else if (c == "plot.redo" && d) d->redo();
  else if ((c == "plot.copy" || c == "plot.cut") && selected)
    {
    _clipboard = d->copy(t->selected).str();
    ImGui::SetClipboardText(_clipboard.c_str());
    if (c == "plot.cut") { d->remove(t->selected); t->selected = PlotDocument::parent(t->selected); }
    _gui.setStatus(c == "plot.cut" ? "Cut" : "Copied (its XML is on the clipboard)");
    }
  else if (c == "plot.paste" && d)
    {
    // the clipboard of the system, if it holds an element (copied from a text editor), otherwise the last copy
    const char * system = ImGui::GetClipboardText();
    std::string text = system && std::strchr(system, '<') ? std::string(system) : _clipboard;
    bool ok = false;
    Path p = d->paste(String(text), t->hasSelection ? t->selected : Path(), ok);
    if (!ok && text != _clipboard && !_clipboard.empty()) p = d->paste(String(_clipboard), t->hasSelection ? t->selected : Path(), ok);
    if (ok) { t->selected = p; t->hasSelection = true; }
    else _gui.setStatus("Nothing to paste here");
    }
  else if (c == "plot.duplicateElement" && selected) t->selected = d->duplicate(t->selected);
  else if (c == "plot.delete")
    {
    // a shape selected on the current canvas (the tree not in use) goes first
    if (!_treeFocused && _gui.nPanes() > 0 && _gui.currentPane().hasSelectedShape()) DatumApp::actionPerformed(ActionEvent{"edit.delete", nullptr});
    else if (selected)
      {
      Path p = t->selected;
      if (d->remove(p)) t->selected = PlotDocument::parent(p);
      }
    }
  else if (c == "plot.up" && selected)   t->selected = d->shift(t->selected, -1);
  else if (c == "plot.down" && selected) t->selected = d->shift(t->selected, +1);
  else if (c == "plot.addWindow" && d) { t->selected = d->addWindow(); t->hasSelection = true; }
  else if (c == "plot.addCanvas" && d)
    {
    bool ok = false;
    Path w = d->enclosing(t->selected, PlotDocument::kWindow, ok);
    if (!ok) { std::vector<Path> ws = d->children(Path(), PlotDocument::kWindow); w = ws.empty() ? d->addWindow() : ws.back(); }
    t->selected = d->addCanvas(w);
    t->hasSelection = true;
    }
  else if (c == "plot.addGraph" && d)
    {
    bool ok = false;
    Path cv = d->enclosing(t->selected, PlotDocument::kCanvas, ok);
    if (!ok) { _gui.setStatus("Select a canvas first"); return; }
    t->selected = d->addGraph(cv);
    }
  else if (c == "plot.regroup" && d)
    {
    // the window, canvas or graph of the selection may limit the search (whole file by default)
    Path scope;
    if (selected)
      for (PlotDocument::Kind k : { PlotDocument::kGraph, PlotDocument::kCanvas, PlotDocument::kWindow })
        {
        bool ok = false;
        Path p = d->enclosing(t->selected, k, ok);
        if (ok) { scope = p; break; }
        }
    requestRegroup(scope, false);
    }
  else if (c == "plot.addFunction" && d)
    {
    bool ok = false;
    Path g = d->enclosing(t->selected, PlotDocument::kGraph, ok);
    if (!ok) { _gui.setStatus("Select a graph first"); return; }
    t->selected = d->addFunction(g);
    }
  else if (c == "plot.print" || c == "plot.pdf" || c == "plot.pptx" || c == "plot.latex" || c == "plot.all")
    {
    GRAPHICS::PlotSpec spec;
    std::vector<GRAPHICS::PlotSpec::WindowState> windows;
    if (!makeOutputs(spec, windows)) return;
    String done, error;
    bool all = c == "plot.all";
    if (all || c == "plot.print")
      {
      std::vector<String> files = PlotOutputs::print(spec, windows);
      done += String(std::to_string(files.size())) + String(" plots printed in ") + spec.outputPath("") + String("\n");
      }
    if (all || c == "plot.pdf")
      {
      String path = PlotOutputs::pdf(spec, windows);
      done += path.isNull() ? String("cannot write the PDF book\n") : String("PDF book: ") + path + String("\n");
      }
    if (all || c == "plot.pptx")
      {
      String path = PlotOutputs::pptx(spec, windows);
      done += path.isNull() ? String("cannot write the PowerPoint file\n") : String("PowerPoint: ") + path + String("\n");
      }
    if (all || c == "plot.latex")
      {
      String path = PlotOutputs::latex(spec, windows, error);
      if (!error.isNull()) done += error + String("\n");
      if (!path.isNull()) done += String("LaTeX: ") + path + String("\n");
      }
    _gui.showMessage("Export", done);
    }
  else if (c == "plot.folder" && d)
    {
    int id = t->id;
    _gui.fileDialog().open(FileDialog::kOpenFolder, "Folder of the outputs", d->directory(), {}, "",
      [this, id](const String & folder)
      {
      for (auto & tt : _documents)
        if (tt.id == id)
          for (const Path & p : tt.document->children(Path(), PlotDocument::kOutput))
            tt.document->setAttribute(p, "directory", PlotDocument::relativePath(folder, tt.document->directory()));
      });
    }
  else if (c == "plot.tree") { if (_showTree) { _showTree = false; _gui.setSideWindowVisible(false); } else showTree(); }
  else if (c == "plot.inspector") { _showInspector = !_showInspector; _placeInspector = true; }
  else if (c == "plot.showStyle") _showStyle = !_showStyle;
  else if (c == "plot.allWindows") { _allWindows = !_allWindows; _drawnTab = -1; }
  else if (c == "plot.detachedWindows") { setDetachedWindows(!_detachedWindows); _drawnTab = -1; }
  else if (c == "plot.help") _showHelp = true;
  else if (std::sscanf(c.data(), "window.layout.%dx%d", &columns, &rows) == 2) setGrid(false, columns, rows);
  else if (std::sscanf(c.data(), "window.graphs.%dx%d", &columns, &rows) == 2) setGrid(true, columns, rows);
  else if (c == "window.layout.custom" || c == "window.graphs.custom") { _gridOfGraphs = c == "window.graphs.custom"; _gridDialog = true; }
  // ----- File: the data files
  else if (c == "data.new") newTree(_gui.activeBrowser());
  else if (c == "data.duplicate")
    {
    BrowserFile * f = selectedDataFile();
    if (!f) { _gui.setStatus("Select a data file (or one of its objects) in the browser first"); return; }
    duplicateDataFile(f->id);
    }
  else if (c == "data.closeAll")
    {
    std::vector<int> ids;
    for (const auto & f : _files) ids.push_back(f.id);
    if (!ids.empty()) closeFilesAsking(ids, nullptr);
    }
  else if (c == "data.save" || c == "data.saveAs")
    {
    BrowserFile * f = selectedDataFile();
    if (!f) { _gui.setStatus("Select a data file (or one of its objects) in the browser first"); return; }
    if (f->editable) saveTree(f->id, c == "data.saveAs");
    else if (c == "data.saveAs") saveCopy(f->id, "");
    else _gui.setStatus("Unchanged: a file read from disk is saved as it is (Save As writes a copy)");
    }
  else if (c == "data.exportRoot")
    {
    BrowserFile * f = selectedDataFile();
    if (f) exportRoot(f->id, "");
    }
  else if (c == "data.exportPdf" || c == "data.exportLatex" || c == "data.exportPptx")
    {
    BrowserFile * f = selectedDataFile();
    if (!f) { _gui.setStatus("Select a data file (or one of its objects) in the browser first"); return; }
    String kind = c == "data.exportPdf" ? "pdf" : (c == "data.exportLatex" ? "latex" : "pptx");
    String extension = c == "data.exportPdf" ? ".pdf" : (c == "data.exportLatex" ? ".tex" : ".pptx");
    String name = (f->editable ? f->name : FileSystem::stem(f->path)) + extension;
    int id = f->id;
    _gui.fileDialog().open(FileDialog::kSaveFile, "Export the histograms of the file", _configuration.directory,
                           { extension }, name, [this, id, kind, extension](const String & chosen)
      {
      exportDataFile(id, kind, FileSystem::hasExtension(chosen, extension) ? chosen : chosen + extension);
      });
    }
#ifdef DATUM_WITH_HEPDATA
  else if (c == "data.importHEPData") _hepDataDialog.openImport(_configuration.directory);
  else if (c == "data.exportHEPData") openHEPDataExport();
  else if (c == "help.hepdata") _hepDataDialog.openHelp();
#endif
  else if (c == "data.rename")
    {
    if (!_gui.activeBrowser().requestRename())
      _gui.setStatus("Select a folder or a histogram of a data file made in DatumGui (files read from disk keep their names)");
    }
  // ----- Plots
  else if (c == "plot.rename" && d) requestRename(1, Path());
  else if (c == "win.rename" && d)
    {
    Path w;
    if (currentWindow(w)) requestRename(2, w); else _gui.setStatus("No window in the plot file");
    }
  else if (c == "canvas.rename" && d)
    {
    Path cv, graph;
    if (currentCanvas(cv, graph)) requestRename(3, cv); else _gui.setStatus("The current canvas is not one of the plot file");
    }
  else if (c == "plot.closeAll")
    {
    std::vector<int> dirty;
    for (const auto & tt : _documents) if (tt.document->isDirty()) dirty.push_back(tt.id);
    for (int k=int(_documents.size())-1; k>=0; k--)
      if (k < int(_documents.size()) && !_documents[std::size_t(k)].document->isDirty()) closeTab(k);
    if (!dirty.empty()) { _closing = dirty; _exitAfterClosing = false; _closeDialog = true; }
    }
  // ----- Windows
  else if (c == "win.new") actionPerformed(ActionEvent{"plot.newCanvasWindow", nullptr});
  else if (c == "win.duplicate" && d)
    {
    Path w;
    if (!currentWindow(w)) { _gui.setStatus("No window in the plot file"); return; }
    Path copy = d->duplicate(w);
    if (copy.empty()) return;
    t->selected = copy;
    t->hasSelection = true;
    _gui.setStatus("Window duplicated with its canvases (Undo to remove the copy)");
    }
  else if (c == "win.close") actionPerformed(ActionEvent{"plot.closeWindow", nullptr});
  else if (c == "win.closeAll" && d)
    {
    std::vector<Path> windows = d->children(Path(), PlotDocument::kWindow);
    if (windows.empty()) return;
    d->beginEdit();
    for (auto w = windows.rbegin(); w != windows.rend(); ++w) d->remove(*w);
    d->endEdit();
    t->hasSelection = false;
    t->selected = Path();
    _drawnWindow = -1;
    _gui.setStatus("All the windows removed from the plot file (Undo to restore them)");
    }
  else if (c == "win.open" && d)
    _gui.fileDialog().open(FileDialog::kOpenFile, "Plot file whose windows to add", _configuration.directory, {".xml"}, "",
                           [this](const String & path) { openWindows(path); });
  else if (c == "win.save" || c == "win.saveAs") saveViewAs(false, c == "win.saveAs");
  else if (c == "canvas.save" || c == "canvas.saveAs") saveViewAs(true, c == "canvas.saveAs");
  else if (c.str().compare(0, 14, "canvas.saveAs.") == 0) saveViewAs(true, true, "." + c.str().substr(14));   // canvas.saveAs.pdf, ...
  else if (c == "canvas.new")
    {
    if (d) actionPerformed(ActionEvent{"plot.newCanvas", nullptr});   // a canvas of the plot file shown
    else { recordUndo(); _gui.addPane(); }                              // no plot file: a canvas of the window
    }
  else if (c == "canvas.open")
    _gui.fileDialog().open(FileDialog::kOpenFile, "Plot file with the canvas to open", _configuration.directory, {".xml"}, "",
                           [this](const String & path) { openCanvas(path); });
  else if (c == "canvas.duplicate")
    {
    Path canvas, graph;
    if (!d || !currentCanvas(canvas, graph)) { _gui.setStatus("The current canvas is not one of a plot file"); return; }
    Path copy = d->duplicate(canvas);
    if (copy.empty()) return;
    t->selected = copy;
    t->hasSelection = true;
    _gui.setStatus("Duplicated the canvas (Undo to remove the copy)");
    }
  else if (c == "win.exportPdfs")
    _gui.fileDialog().open(FileDialog::kOpenFolder, "Folder of the PDF files of the canvases", _configuration.directory, {}, "",
                           [this](const String & folder) { exportWindow("pdfs", folder); });
  else if (c == "win.exportBook" || c == "win.exportLatex" || c == "win.exportPptx")
    {
    String kind = c == "win.exportBook" ? "book" : (c == "win.exportLatex" ? "latex" : "pptx");
    String extension = c == "win.exportBook" ? ".pdf" : (c == "win.exportLatex" ? ".tex" : ".pptx");
    _gui.fileDialog().open(FileDialog::kSaveFile, "Export the canvases of the window", _configuration.directory,
                           { extension }, String("window") + extension, [this, kind, extension](const String & chosen)
      {
      exportWindow(kind, FileSystem::hasExtension(chosen, extension) ? chosen : chosen + extension);
      });
    }
  else if (c.str().compare(0, 5, "plot.") == 0) { }
  else DatumApp::actionPerformed(event);
  }

  void DatumGuiApp::setGrid(bool graphs, int columns, int rows)
  {
  Tab * t = tab();
  if (!t) return;
  PlotDocument & d = *t->document;
  Path target;
  if (graphs)
    {
    int k = _gui.currentPaneIndex();
    for (const auto & link : _links) if (paneIndexOf(link.pane) == k) target = link.canvas;
    if (target.empty()) { _gui.setStatus("The current canvas is not one of the plot file"); return; }
    }
  else
    {
    std::vector<Path> windows = d.children(Path(), PlotDocument::kWindow);
    if (_drawnWindow < 0 || _drawnWindow >= int(windows.size())) { _gui.setStatus("No window in the plot file"); return; }
    target = windows[std::size_t(_drawnWindow)];
    }
  d.beginEdit();
  d.setAttribute(target, "columns", String(std::to_string(columns)));
  d.setAttribute(target, "rows", String(std::to_string(rows)));
  d.endEdit();
  }

  void DatumGuiApp::gridMenu(bool graphs)
  {
  Tab * t = tab();
  int columns = 1, rows = 1;
  const XmlElement * e = nullptr;
  if (t)
    {
    PlotDocument & d = *t->document;
    if (graphs)
      {
      Path canvas, graph;
      if (currentCanvas(canvas, graph)) e = d.element(canvas);
      }
    else
      {
      Path w;
      if (currentWindow(w)) e = d.element(w);
      }
    }
  if (!e) { ImGui::TextDisabled(graphs ? "No canvas of the plot file" : "No window in the plot file"); return; }
  int currentColumns = int(e->attributeInt("columns", 1)), currentRows = int(e->attributeInt("rows", 1));
  if (gridPicker(std::max(1, currentColumns), std::max(1, currentRows), columns, rows)) setGrid(graphs, columns, rows);
  }

  PlotDocument DatumGuiApp::reducedDocument(const Path & window, const Path & canvas)
  {
  PlotDocument reduced = *tab()->document;
  XmlElement & root = reduced.document().root();
  for (int k=int(root.nChildren())-1; k>=0; k--)
    if (root.child(unsigned(k)).isNamed("Window") && (window.empty() || k != window[0])) root.removeChild(unsigned(k));
  if (!canvas.empty() && canvas.size() >= 2)
    for (unsigned int k=0; k<root.nChildren(); k++)
      {
      XmlElement & w = root.child(k);
      if (!w.isNamed("Window")) continue;
      for (int j=int(w.nChildren())-1; j>=0; j--)
        if (w.child(unsigned(j)).isNamed("Canvas") && j != canvas[1]) w.removeChild(unsigned(j));
      w.setAttribute("columns", 1);
      w.setAttribute("rows", 1);
      }
  return reduced;
  }

  std::string DatumGuiApp::viewKey(bool canvas)
  {
  Tab * t = tab();
  if (!t) return "";
  std::string key = std::to_string(t->id);
  if (canvas)
    {
    Path c, graph;
    if (!currentCanvas(c, graph)) return "";
    const XmlElement * e = t->document->element(c);
    return key + "|canvas|" + (e ? e->attribute("name", "").str() : std::string());
    }
  Path w;
  if (!currentWindow(w) || w.empty()) return "";
  return key + "|window|" + std::to_string(w[0]);
  }

  bool DatumGuiApp::saveView(bool canvas, const String & fileName)
  {
  Tab * t = tab();
  if (!t) return false;
  Path w, c, graph;
  if (canvas ? !currentCanvas(c, graph) : !currentWindow(w))
    { _gui.showMessage("Save", canvas ? "The current canvas is not one of the plot file" : "No window in the plot file"); return false; }
  if (canvas) w = PlotDocument::parent(c);
  std::string extension = extensionOf(fileName);
  String path = extension.empty() ? fileName + String(".pdf") : fileName;
  if (extension.empty()) extension = ".pdf";
  String error;
  if (extension == ".pdf" || extension == ".svg")
    {
    DatumApp::save(path, canvas ? _gui.currentPaneIndex() : -1);         // as on screen
    return true;
    }
  if (extension == ".png")
    {
    int pane = canvas ? _gui.currentPaneIndex() : -1;
    if (!writePng(path, [this, pane](const String & pdf) { DatumApp::save(pdf, pane); return true; })) return false;
    _gui.setStatus(String("Saved ") + path);
    return true;
    }
  if (extension == ".pptx")
    {
    PlotSpec spec;
    std::vector<PlotSpec::WindowState> windows;
    PlotDocument reduced = reducedDocument(w, c);
    if (!buildOutputs(reduced, spec, windows)) return false;
    String written = PlotOutputs::pptx(spec, windows, path);
    if (written.isNull()) { _gui.showMessage("Save", String("Cannot write ") + path); return false; }
    _gui.setStatus(String("Saved ") + written);
    return true;
    }
  if (extension == ".xml")
    {
    PlotDocument reduced = reducedDocument(w, c);
    if (!reduced.write(path, error)) { _gui.showMessage("Save", path + String(":\n") + error); return false; }
    _gui.setStatus(String("Saved ") + path);
    return true;
    }
  if (extension == ".tex")
    {
    PlotSpec spec;
    std::vector<PlotSpec::WindowState> windows;
    PlotDocument reduced = reducedDocument(w, c);
    if (!buildOutputs(reduced, spec, windows)) return false;
    String written = PlotOutputs::latex(spec, windows, error, path);
    if (written.isNull()) { _gui.showMessage("Save", String("Cannot write ") + path + String("\n") + error); return false; }
    _gui.setStatus(String("Saved ") + written);
    return true;
    }
  _gui.showMessage("Save", String("Unknown type ") + String(extension.c_str()) + String(": use .pdf, .svg, .png, .tex, .pptx or .xml"));
  return false;
  }

  bool DatumGuiApp::writePng(const String & png, const std::function<bool(const String & pdf)> & writePdf)
  {
  // the canvas as a PDF (as on screen), converted to PNG at 150 dpi by a tool of the system
  String pdf = png + String(".tmp.pdf");
  if (!writePdf(pdf) || !FileSystem::isFile(pdf)) { _gui.showMessage("Save", String("Cannot write ") + pdf); return false; }
  std::string in = "'" + pdf.str() + "'", out = "'" + png.str() + "'";
  std::string stem = png.str().size() > 4 ? png.str().substr(0, png.str().size() - 4) : png.str();
  std::vector<std::string> commands;
#ifdef __APPLE__
  commands.push_back("sips -s format png -s dpiWidth 150 -s dpiHeight 150 " + in + " --out " + out + " >/dev/null 2>&1");
#endif
  commands.push_back("pdftoppm -png -r 150 -singlefile " + in + " '" + stem + "' >/dev/null 2>&1");
  commands.push_back("convert -density 150 " + in + " " + out + " >/dev/null 2>&1");
  bool done = false;
  for (const std::string & command : commands)
    if (std::system(command.c_str()) == 0 && FileSystem::isFile(png)) { done = true; break; }
  std::remove(pdf.data());
  if (!done) _gui.showMessage("Save", String("Cannot convert the canvas to PNG (no sips, pdftoppm or convert found): ") + png);
  return done;
  }

  void DatumGuiApp::openCanvas(const String & fileName, int which)
  {
  PlotDocument other;
  String error;
  if (!other.read(fileName, error)) { _gui.showMessage("Open Canvas", fileName + String(":\n") + error); return; }
  // the canvases of the file, window by window
  std::vector<const XmlElement *> canvases;
  std::vector<std::string> labels;
  for (const XmlElement * w : other.root().children())
    {
    if (!w->isNamed("Window")) continue;
    for (const XmlElement * e : w->children())
      {
      if (!e->isNamed("Canvas")) continue;
      String title = e->attribute("title", ""), name = e->attribute("name", "");
      String label = title.isNull() || title.str().empty() ? name : title;
      labels.push_back(w->attribute("title", "window").str() + " / " + (label.isNull() ? std::string("canvas") : label.str()));
      canvases.push_back(e);
      }
    }
  if (canvases.empty()) { _gui.setStatus(String("No canvas in ") + fileName); return; }
  if (which < 0 && canvases.size() > 1)
    {
    _canvasChoiceFile = fileName.str();
    _canvasChoices = labels;
    _canvasChoiceRequested = true;
    return;
    }
  if (which < 0) which = 0;
  if (which >= int(canvases.size())) return;
  // the plot file shown (a new one if none), and its window shown (a new one if it has none)
  if (!tab()) newDocument();
  Tab * t = tab();
  if (!t) return;
  PlotDocument & d = *t->document;
  d.beginEdit();
  std::map<std::string, String> ids;
  String first;
  for (const XmlElement * e : other.root().children())
    {
    if (!e->isNamed("Input")) continue;
    String id = e->attribute("id", ""), file = e->attribute("file", "");
    if (first.isNull()) first = id;
    if (file.isNull()) continue;
    String full = (file.str()[0] == '/' || file.str()[0] == '$' || file.str()[0] == '~') ? file
                  : FileSystem::normalize(FileSystem::join(other.directory(), file));
    ids[id.str()] = d.input(full);
    }
  Path w;
  if (!currentWindow(w)) w = d.addWindow();
  XmlElement canvas = *canvases[std::size_t(which)];
  renameInputs(canvas, ids, first);
  bool ok = false;
  Path p = d.paste(canvas.toString(), w, ok);
  d.endEdit();
  if (!ok) { _gui.setStatus(String("Cannot add the canvas of ") + fileName); return; }
  t->selected = p;
  t->hasSelection = true;
  _configuration.directory = FileSystem::parent(fileName);
  _gui.setStatus(String("Canvas ") + String(labels[std::size_t(which)].c_str()) + String(" added from ") + fileName + String(" (Undo removes it)"));
  }

  void DatumGuiApp::renderCanvasChoice()
  {
  if (_canvasChoiceRequested) { ImGui::OpenPopup("Open Canvas##choice"); _canvasChoiceRequested = false; }
  if (!ImGui::BeginPopupModal("Open Canvas##choice", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  ImGui::TextUnformatted("The canvas of the plot file to add to the window shown:");
  int chosen = -1;
  if (ImGui::BeginListBox("##canvases", ImVec2(420, std::min(300.0f, 22.0f*float(_canvasChoices.size()) + 8.0f))))
    {
    for (std::size_t k=0; k<_canvasChoices.size(); k++)
      {
      ImGui::PushID(int(k));
      if (ImGui::Selectable(_canvasChoices[k].c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) chosen = int(k);
      ImGui::PopID();
      }
    ImGui::EndListBox();
    }
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { _canvasChoiceFile.clear(); ImGui::CloseCurrentPopup(); }
  if (chosen >= 0)
    {
    String file(_canvasChoiceFile.c_str());
    _canvasChoiceFile.clear();
    ImGui::CloseCurrentPopup();
    openCanvas(file, chosen);
    }
  ImGui::EndPopup();
  }

  void DatumGuiApp::saveViewAs(bool canvas, bool ask, const std::string & extension)
  {
  std::string key = viewKey(canvas);
  if (key.empty() && canvas && _gui.nPanes() > 0 && (extension == ".pdf" || extension == ".svg" || extension == ".png"))
    {
    // a canvas that is not one of a plot file: saved as shown
    String title = _gui.currentPane().title();
    String name = (title.isNull() || title.str().empty() ? String("canvas") : title) + String(extension.c_str());
    int pane = _gui.currentPaneIndex();
    _gui.fileDialog().open(FileDialog::kSaveFile, "Save the canvas", _configuration.directory, { extension }, name,
      [this, pane, extension](const String & chosen)
      {
      String path = extensionOf(chosen).empty() ? chosen + String(extension.c_str()) : chosen;
      if (extension == ".png") { if (!writePng(path, [this, pane](const String & pdf) { DatumApp::save(pdf, pane); return true; })) return; }
      else DatumApp::save(path, pane);
      _configuration.directory = FileSystem::parent(path);
      });
    return;
    }
  if (key.empty())
    { _gui.setStatus(canvas ? "The current canvas is not one of the plot file" : "No window in the plot file"); return; }
  auto found = _viewFiles.find(key);
  if (!ask && found != _viewFiles.end()) { saveView(canvas, found->second); return; }
  std::string type = extension.empty() ? std::string(".pdf") : extension;
  String name = canvas ? String("canvas") + String(type.c_str()) : String("window") + String(type.c_str());
  if (found != _viewFiles.end() && (extension.empty() || extensionOf(found->second) == extension)) name = FileSystem::name(found->second);
  else if (canvas)
    {
    Path c, graph;
    if (currentCanvas(c, graph))
      if (const XmlElement * e = tab()->document->element(c)) if (!e->attribute("name", "").isNull()) name = e->attribute("name") + String(type.c_str());
    }
  std::vector<String> types = extension.empty() ? std::vector<String>{".pdf", ".svg", ".xml", ".tex"} : std::vector<String>{ extension };
  _gui.fileDialog().open(FileDialog::kSaveFile, canvas ? "Save the canvas" : "Save the window",
                         _configuration.directory, types, name,
    [this, canvas, key, type](const String & chosen)
    {
    String path = extensionOf(chosen).empty() ? chosen + String(type.c_str()) : chosen;
    if (saveView(canvas, path))
      {
      _viewFiles[key] = path;
      _configuration.directory = FileSystem::parent(chosen);
      }
    });
  }

  void DatumGuiApp::exportWindow(const String & kind, const String & path)
  {
  Path w;
  if (!currentWindow(w)) { _gui.setStatus("No window in the plot file"); return; }
  PlotSpec spec;
  std::vector<PlotSpec::WindowState> windows;
  PlotDocument reduced = reducedDocument(w, Path());
  if (!buildOutputs(reduced, spec, windows)) return;
  String done, error;
  if (kind == "pdfs")
    {
    int n = 0;
    for (const GRAPHICS::Canvas * canvas : PlotOutputs::canvases(windows))
      {
      String name = canvas->name().isNull() ? String("canvas") + String(std::to_string(n).c_str()) : canvas->name();
      String file = FileSystem::join(path, name + String(".pdf"));
      if (PlotSpec::writePdf(file, { canvas })) n++;
      else done += String("cannot write ") + file + String("\n");
      }
    done += String(std::to_string(n).c_str()) + String(" PDF files in ") + path;
    }
  else if (kind == "book")
    {
    String written = PlotOutputs::pdf(spec, windows, path);
    done = written.isNull() ? String("Cannot write ") + path : String("Book: ") + written;
    }
  else if (kind == "pptx")
    {
    String written = PlotOutputs::pptx(spec, windows, path);
    done = written.isNull() ? String("Cannot write ") + path : String("PowerPoint: ") + written;
    }
  else
    {
    String written = PlotOutputs::latex(spec, windows, error, path);
    done = written.isNull() ? String("Cannot write ") + path + String("\n") + error : String("LaTeX: ") + written;
    }
  _configuration.directory = kind == "pdfs" ? path : FileSystem::parent(path);
  _gui.showMessage("Export", done);
  }

  void DatumGuiApp::openWindows(const String & fileName)
  {
  Tab * t = tab();
  if (!t) return;
  PlotDocument other;
  String error;
  if (!other.read(fileName, error)) { _gui.showMessage("Open Windows", fileName + String(":\n") + error); return; }
  PlotDocument & d = *t->document;
  d.beginEdit();
  // the inputs of the other file, added to those of this one
  std::map<std::string, String> ids;
  String first;
  for (const XmlElement * e : other.root().children())
    {
    if (!e->isNamed("Input")) continue;
    String id = e->attribute("id", ""), file = e->attribute("file", "");
    if (first.isNull()) first = id;
    if (file.isNull()) continue;
    String full = (file.str()[0] == '/' || file.str()[0] == '$' || file.str()[0] == '~') ? file
                  : FileSystem::normalize(FileSystem::join(other.directory(), file));
    ids[id.str()] = d.input(full);
    }
  int added = 0;
  Path last;
  for (const XmlElement * e : other.root().children())
    {
    if (!e->isNamed("Window")) continue;
    XmlElement window = *e;
    renameInputs(window, ids, first);
    bool ok = false;
    Path p = d.paste(window.toString(), Path(), ok);
    if (ok) { added++; last = p; }
    }
  d.endEdit();
  if (added == 0) { _gui.setStatus(String("No window in ") + fileName); return; }
  t->selected = last;
  t->hasSelection = true;
  _configuration.directory = FileSystem::parent(fileName);
  _gui.setStatus(String(std::to_string(added).c_str()) + String(" windows added from ") + fileName + String(" (Undo removes them)"));
  }

  void DatumGuiApp::treeSaved(int id, const String & previousPath)
  {
  BrowserFile * tree = file(id);
  if (!tree) return;
  int changed = 0;
  for (auto & t : _documents)
    {
    PlotDocument & d = *t.document;
    String dir = d.directory();
    std::vector<Path> inputs;
    const XmlElement & root = d.root();
    for (unsigned int k=0; k<root.nChildren(); k++) if (root.child(k).isNamed("Input")) inputs.push_back(Path{int(k)});
    for (const Path & p : inputs)
      {
      const XmlElement * e = d.element(p);
      String f = e ? e->attribute("file", "") : String("");
      if (f.isNull()) continue;
      String resolved = f.str()[0] == '/' ? FileSystem::normalize(f) : FileSystem::normalize(FileSystem::join(dir, f));
      if (resolved != FileSystem::normalize(previousPath) && f != previousPath) continue;
      d.setAttribute(p, "file", d.fileName().isNull() ? tree->path : PlotDocument::relativePath(tree->path, dir));
      changed++;
      }
    }
  if (changed) _drawnTab = -1;                          // drawn again from the file saved
  }

  void DatumGuiApp::entryRenamed(int id, const String & oldPath, const String & newPath)
  {
  BrowserFile * tree = file(id);
  if (!tree) return;
  std::string from = oldPath.str(), to = newPath.str();
  int changed = 0;
  for (auto & t : _documents)
    {
    PlotDocument & d = *t.document;
    String dir = d.directory();
    // the inputs of the plot file that are this tree (the first one: histograms without input)
    std::set<std::string> ids;
    String first;
    bool firstIsTree = false;
    for (const XmlElement * e : d.root().children("Input"))
      {
      String f = e->attribute("file", "");
      String resolved = f.isNull() ? f : (f.str()[0] == '/' ? FileSystem::normalize(f) : FileSystem::normalize(FileSystem::join(dir, f)));
      bool isTree = !resolved.isNull() && dataFile(resolved) == tree;
      if (first.isNull()) { first = e->attribute("id", ""); firstIsTree = isTree; }
      if (isTree) ids.insert(e->attribute("id", "").str());
      }
    if (ids.empty()) continue;
    std::vector<Path> items;
    std::function<void(const XmlElement &, const Path &)> walk = [&](const XmlElement & e, const Path & p)
      {
      if (e.isNamed("Histogram") || e.isNamed("Data") || e.isNamed("Profile"))
        {
        String input = e.attribute("input", "");
        bool mine = input.isNull() ? firstIsTree : ids.count(input.str()) > 0;
        std::string path = e.attribute("path", "").str();
        if (mine && (path == from || path.compare(0, from.size() + 1, from + "/") == 0)) items.push_back(p);
        }
      for (unsigned int k=0; k<e.nChildren(); k++) { Path q = p; q.push_back(int(k)); walk(e.child(k), q); }
      };
    walk(d.root(), Path());
    if (items.empty()) continue;
    d.beginEdit();
    for (const Path & p : items)
      {
      std::string path = d.element(p)->attribute("path", "").str();
      d.setAttribute(p, "path", String((to + path.substr(from.size())).c_str()));
      changed++;
      }
    d.endEdit();
    }
  if (changed) _drawnTab = -1;                          // drawn again: the histograms under their new names
  }

  void DatumGuiApp::requestRename(int kind, const Path & path)
  {
  Tab * t = tab();
  if (!t) return;
  PlotDocument & d = *t->document;
  std::string text;
  if (kind == 1)
    {
    text = d.fileName().isNull() ? d.displayName().str() : FileSystem::stem(d.fileName()).str();
    }
  else
    {
    const XmlElement * e = d.element(path);
    if (!e) return;
    text = kind == 2 ? e->attribute("title", "").str()
                     : (e->attribute("title", "").isNull() ? e->attribute("name", "") : e->attribute("title", "")).str();
    }
  _rename = RenameRequest{kind, t->id, path, text, false};
  }

  void DatumGuiApp::renderRenameDialog()
  {
  if (_rename.kind == 0) return;
  const char * title = "Rename##plotRename";
  if (!_rename.opened) { ImGui::OpenPopup(title); _rename.opened = true; }
  if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { _rename = RenameRequest(); return; }
  ImGui::Text("%s", _rename.kind == 1 ? "New name of the plot file:" : (_rename.kind == 2 ? "New title of the window:"
                                                                                          : "New name of the canvas:"));
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  bool done = ImGui::InputText("##name", &_rename.text, ImGuiInputTextFlags_EnterReturnsTrue);
  if (_rename.kind == 1) ImGui::TextDisabled("A saved plot file is renamed in its folder.");
  done = ImGui::Button("OK") || done;
  ImGui::SameLine();
  bool cancel = ImGui::Button("Cancel");
  if (done && !_rename.text.empty())
    {
    for (auto & t : _documents)
      {
      if (t.id != _rename.tab) continue;
      PlotDocument & d = *t.document;
      String name(_rename.text.c_str());
      if (_rename.kind == 1)
        {
        if (d.fileName().isNull()) d.setUntitledName(name);
        else
          {
          String target = FileSystem::join(FileSystem::parent(d.fileName()),
                                            FileSystem::hasExtension(name, ".xml") ? name : name + String(".xml"));
          if (FileSystem::isFile(target)) _gui.showMessage("Rename", target + String(" exists already"));
          else if (std::rename(d.fileName().data(), target.data()) != 0) _gui.showMessage("Rename", String("Cannot rename ") + d.fileName());
          else { d.setFileName(target); _gui.setStatus(String("Renamed to ") + target); }
          }
        }
      else if (_rename.kind == 2) d.setAttribute(_rename.path, "title", name);
      else
        {
        // the title shown, and the name (an identifier, the name of its files): unique, without spaces
        std::string id = _rename.text;
        for (char & ch : id) if (ch == ' ' || ch == '/') ch = '_';
        d.beginEdit();
        d.setAttribute(_rename.path, "title", name);
        const XmlElement * e = d.element(_rename.path);
        if (e && e->attribute("name", "").str() != id) d.setAttribute(_rename.path, "name", d.uniqueCanvasName(String(id.c_str())));
        d.endEdit();
        }
      }
    }
  if (done || cancel) { _rename = RenameRequest(); ImGui::CloseCurrentPopup(); }
  ImGui::EndPopup();
  }

  BrowserFile * DatumGuiApp::selectedDataFile()
  {
  String path;
  bool folder = false;
  return selectedEntry(path, folder);
  }

  void DatumGuiApp::duplicateDataFile(int id)
  {
  int copy = newTree(_gui.activeBrowser());
  BrowserFile * from = file(id);                       // after newTree (the list of files grew)
  BrowserFile * to = file(copy);
  if (!from || !to || !from->file) return;
  String name = String("Copy of ") + (from->editable ? from->name : FileSystem::stem(from->path));
  to->name = name;
  to->file->setFolderName(name);
  try
    {
    Folder & whole = *from->file;
    Folder & top = *to->file;
    for (const auto & entry : whole.names()) top.copyEntry(whole, entry);
    to->modified = true;
    _gui.setStatus(name + String(": a new data file (Save to write it)"));
    }
  catch (const std::exception & e) { _gui.showMessage("Duplicate", String("Cannot copy:\n") + String(e.what())); }
  }

#ifdef DATUM_WITH_HEPDATA
  void DatumGuiApp::openHEPDataExport()
  {
  std::vector<HEPDataDialog::ExportFile> files;
  BrowserFile * selected = selectedDataFile();
  for (const BrowserFile & f : _files)
    {
    if (!f.file) continue;
    HEPDataDialog::ExportFile e;
    e.label = f.editable ? f.name : FileSystem::stem(f.path);
    e.folder = std::shared_ptr<const Folder>(f.file, &f.file->folder());   // keeps the file alive
    e.selected = selected && selected->id == f.id;
    files.push_back(e);
    }
  _hepDataDialog.openExport(files, _configuration.directory);
  }

  bool DatumGuiApp::importHEPData(const String & name, const HEPDataImportPlan & plan, String & message)
  {
  int id = newTree(_gui.activeBrowser());
  BrowserFile * f = file(id);
  if (!f || !f->file) { message = "Cannot make a new data file"; return false; }
  f->name = name;
  f->file->setFolderName(name);
  try
    {
    unsigned int n = plan.addTo(f->file->folder());
    f->modified = true;
    message = name + String(": ") + String(std::to_string(n).c_str()) + String(n == 1 ? " object" : " objects")
              + String(" imported from HEPData (Save to write it as a Datum file)");
    return true;
    }
  catch (const std::exception & e)
    {
    message = String("Cannot import: ") + String(e.what());
    return false;
    }
  }
#endif

  void DatumGuiApp::exportDataFile(int id, const String & kind, const String & path)
  {
  BrowserFile * f = file(id);
  if (!f || !f->file) return;
  String name = f->editable ? f->name : FileSystem::stem(f->path);
  GRAPHICS::PlotTemplate catalog;
  int input = catalog.addInput("data", f->path.isNull() ? name + String(DatumFile::extension()) : f->path);
  if (catalog.addFolder(input, *f->file) == 0) { _gui.showMessage("Export", name + String(": no histogram to draw")); return; }
  GRAPHICS::PlotTemplate::Options options;
  options.grouping = GRAPHICS::PlotTemplate::Options::kByHistogram;
  options.overlay = false;
  options.maxGraphs = 1;                               // one histogram per page
  options.maxCanvases = 1000000;
  options.printFiles = false;
  options.title = name;
  options.outputDirectory = FileSystem::parent(path);
  PlotSpec spec;
  String error, problems;
  if (!spec.read(catalog.makeSpec(options), FileSystem::parent(path), error)) { _gui.showMessage("Export", error); return; }
  spec.setInputFile(0, f->file);
  if (!spec.openInputs(error)) { _gui.showMessage("Export", error); return; }
  std::vector<PlotSpec::WindowState> windows = spec.buildAll(problems);
  String written;
  if (kind == "pdf")       written = PlotOutputs::pdf(spec, windows, path);
  else if (kind == "pptx") written = PlotOutputs::pptx(spec, windows, path);
  else                     written = PlotOutputs::latex(spec, windows, error, path);
  _configuration.directory = FileSystem::parent(path);
  if (written.isNull()) _gui.showMessage("Export", String("Cannot write ") + path + String("\n") + error);
  else _gui.showMessage("Export", String(std::to_string(PlotOutputs::canvases(windows).size()).c_str()) + String(" histograms: ")
                        + written + (problems.isNull() ? String("") : String("\n") + problems));
  }

  bool DatumGuiApp::makeOutputs(GRAPHICS::PlotSpec & spec, std::vector<GRAPHICS::PlotSpec::WindowState> & windows)
  {
  Tab * t = tab();
  if (!t) return false;
  return buildOutputs(*t->document, spec, windows);
  }

  bool DatumGuiApp::buildOutputs(PlotDocument & document, GRAPHICS::PlotSpec & spec,
                               std::vector<GRAPHICS::PlotSpec::WindowState> & windows)
  {
  String error, problems;
  if (!spec.read(document.document(), document.directory(), error)) { _gui.showMessage("Export", error); return false; }
  for (std::size_t k=0; k<spec.inputs().size(); k++)
    {
    String path = spec.resolve(spec.inputs()[k].path);
    BrowserFile * f = dataFile(path);
    if (f) spec.setInputFile(int(k), f->file);
    }
  if (!spec.openInputs(error)) { _gui.showMessage("Export", error); return false; }
  windows = spec.buildAll(problems);
  if (!problems.isNull()) _gui.setStatus(String("Export: not everything could be drawn: ") + problems);
  return true;
  }

  // ------------------------------------------------------------------------------------------------ editor

  void DatumGuiApp::renderFrame()
  {
  // changes made while a widget is active (typing, dragging) are one step of undo
  bool active = ImGui::IsAnyItemActive();
  if (active && !_grouping && tab()) { tab()->document->beginEdit(); _grouping = true; _groupTab = tab()->id; }
  if (!active && _grouping)
    {
    for (auto & t : _documents) if (t.id == _groupTab) t.document->endEdit();
    _grouping = false;
    }

  ImGui::SetNextWindowSize(ImVec2(620, 700), ImGuiCond_FirstUseEver);
  _treeFocused = false;
  // Begin() and End() are paired on the visibility at the start of the frame: closing the window (its close button,
  // which Begin() reports by clearing _showTree) must not skip End(), which ImGui reports as missing EndChild/End.
  const bool treeShown = _showTree;
  if (treeShown && ImGui::Begin(kEditorWindow, &_showTree, ImGuiWindowFlags_NoCollapse))
    {
    _treeFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (_treeFocused) _gui.setLastFocus(DatumGui::kFocusSide);
    if (ImGui::BeginTabBar("##documents", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs
                                          | ImGuiTabBarFlags_FittingPolicyScroll))
      {
      if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) newDocument();
      ImGui::SetItemTooltip("New plot file");
      int closeRequest = -1;
      for (std::size_t k=0; k<_documents.size(); k++)
        {
        Tab & t = _documents[k];
        std::string label = t.document->displayName().str() + "###tab" + std::to_string(t.id);
        ImGuiTabItemFlags flags = t.document->isDirty() ? ImGuiTabItemFlags_UnsavedDocument : 0;
        if (_selectTab == int(k)) flags |= ImGuiTabItemFlags_SetSelected;
        bool open = true;
        if (ImGui::BeginTabItem(label.c_str(), &open, flags))
          {
          if (_selectTab < 0 || _selectTab == int(k)) _current = int(k);
          if (!t.document->fileName().isNull()) ImGui::SetItemTooltip("%s", t.document->fileName().data());
          renderTree(t);
          ImGui::EndTabItem();
          }
        if (!open) closeRequest = int(k);
        }
      _selectTab = -1;
      ImGui::EndTabBar();
      if (closeRequest >= 0) requestClose(closeRequest);
      }
    if (_documents.empty())
      {
      ImGui::TextDisabled("No plot file open.");
      if (ImGui::Button("New plot file")) newDocument();
      ImGui::SameLine();
      if (ImGui::Button("Open...")) actionPerformed(ActionEvent{"plot.open", nullptr});
      }
    }
  if (treeShown) ImGui::End();
  if (!_showTree && _gui.sideWindowVisible()) _gui.setSideWindowVisible(false);   // closed with its close button
  renderInspectorPane();
  renderDialogs();
  renderCanvasChoice();
  renderTemplate();
  layoutCanvases();
  recordCanvases();
  syncSelection();
  // the canvas of the plot file current last: where objects are shown when a canvas of a projection is current
  if (_gui.nPanes() > 0)
    for (const auto & link : _links)
      if (link.pane == &_gui.currentPane() && !link.canvas.empty()) { _plotPane = _gui.currentPane().windowId(); break; }
  }

  void DatumGuiApp::renderTree(Tab & t)
  {
  PlotDocument & d = *t.document;
  // toolbar
  if (ImGui::SmallButton("+ Window")) actionPerformed(ActionEvent{"plot.addWindow", nullptr});
  ImGui::SameLine();
  if (ImGui::SmallButton("+ Canvas")) actionPerformed(ActionEvent{"plot.addCanvas", nullptr});
  ImGui::SameLine();
  if (ImGui::SmallButton("+ Graph")) actionPerformed(ActionEvent{"plot.addGraph", nullptr});
  ImGui::SameLine();
  if (ImGui::SmallButton("+ Function")) actionPerformed(ActionEvent{"plot.addFunction", nullptr});
  ImGui::SameLine();
  if (ImGui::SmallButton("Regroup...")) actionPerformed(ActionEvent{"plot.regroup", nullptr});
  ImGui::SetItemTooltip("Gather histograms whose names match a pattern (e.g. *_pt) into common graphs");
  ImGui::SameLine(0, 18);
  ImGui::BeginDisabled(!d.canUndo());
  if (ImGui::SmallButton("Undo")) d.undo();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!d.canRedo());
  if (ImGui::SmallButton("Redo")) d.redo();
  ImGui::EndDisabled();
  ImGui::SameLine(0, 18);
  ImGui::TextDisabled("%s", _mode == kBasic ? "BASIC" : "EDIT");
  ImGui::SetItemTooltip("BASIC: started from a new file; EDIT: from an existing one. The tools are the same.");

  if (ImGui::BeginChild("##tree", ImVec2(0, 0), ImGuiChildFlags_Borders))
    {
    renderNode(t, Path());
    // below the rows: dropping there adds to the top of the document
    ImGui::InvisibleButton("##below", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(40.0f, ImGui::GetContentRegionAvail().y)));
    rowDragAndDrop(t, Path());
    if (ImGui::IsItemClicked()) { t.selected.clear(); t.hasSelection = false; }
    if (d.children(Path(), PlotDocument::kWindow).empty())
      {
      ImVec2 p = ImGui::GetItemRectMin();
      ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 10, p.y + 10), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                          "Drag histograms or folders from the browser here");
      }
    }
  ImGui::EndChild();
  }

  void DatumGuiApp::renderInspectorPane()
  {
  if (!_showInspector) { _placeInspector = true; return; }
  // the first time it is shown (or once its window was closed): below the Plot Tree, or in the current window
  if (_placeInspector && _gui.needsPlace(kInspectorWindow))
    {
    ImGuiWindow * tree = _showTree ? ImGui::FindWindowByName(kEditorWindow) : nullptr;
    if (tree && tree->DockNode && !_gui.needsPlace(kEditorWindow))
      {
      ImGuiID below = 0, above = 0;
      ImGui::DockBuilderSplitNode(tree->DockNode->ID, ImGuiDir_Down, 0.42f, &below, &above);
      ImGui::DockBuilderDockWindow(kInspectorWindow, below);
      ImGui::DockBuilderFinish(ImGui::DockNodeGetRootNode(tree->DockNode)->ID);
      }
    else if (!_showTree || (tree && !_gui.needsPlace(kEditorWindow))) _gui.dockInCurrentWindow(kInspectorWindow);
    else return;   // the Plot Tree is placed first
    }
  _placeInspector = false;
  ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
  const bool shown = _showInspector;
  if (ImGui::Begin(kInspectorWindow, &_showInspector, ImGuiWindowFlags_NoCollapse))
    {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) { _treeFocused = true; _gui.setLastFocus(DatumGui::kFocusSide); }
    Tab * t = tab();
    if (t) renderInspector(*t);
    else ImGui::TextDisabled("No plot file open: the settings of the element selected in the Plot Tree are shown here.");
    }
  if (shown) ImGui::End();
  }

  void DatumGuiApp::renderNode(Tab & t, const Path & path)
  {
  PlotDocument & d = *t.document;
  const XmlElement * e = d.element(path);
  if (!e) return;
  PlotDocument::Kind kind = PlotDocument::kind(*e);
  if (kind == PlotDocument::kStyle && !_showStyle) return;
  bool hasChildren = false;
  for (unsigned k=0; k<e->nChildren() && !hasChildren; k++)
    if (_showStyle || PlotDocument::kind(e->child(k)) != PlotDocument::kStyle) hasChildren = true;
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
                             | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
  if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
  if (t.hasSelection && t.selected == path) flags |= ImGuiTreeNodeFlags_Selected;
  if (kind == PlotDocument::kRoot || kind == PlotDocument::kWindow || kind == PlotDocument::kCanvas
      || kind == PlotDocument::kGraph)
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  // the selection is made visible
  if (t.hasSelection && t.selected.size() > path.size() && std::equal(path.begin(), path.end(), t.selected.begin()))
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  ImGui::PushID(path.empty() ? -1 : path.back());
  bool open = ImGui::TreeNodeEx("##node", flags);
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { t.selected = path; t.hasSelection = true; }
  rowDragAndDrop(t, path);
  rowMenu(t, path);
  ImGui::SameLine();
  ImGui::TextColored(kindColor(kind), "%s", kindTag(kind));
  ImGui::SameLine();
  String text = PlotDocument::label(*e);
  if (kind == PlotDocument::kRoot && !d.fileName().isNull()) text = d.fileName();
  ImGui::TextUnformatted(text.data());
  rowControls(t, path);
  if (open)
    {
    for (unsigned k=0; k<e->nChildren(); k++)
      {
      Path child = path;
      child.push_back(int(k));
      renderNode(t, child);
      e = d.element(path);   // an edit of a row may have changed the tree
      if (!e) break;
      }
    ImGui::TreePop();
    }
  ImGui::PopID();
  }

  void DatumGuiApp::rowDragAndDrop(Tab & t, const Path & path)
  {
  PlotDocument & d = *t.document;
  if (!path.empty() && ImGui::BeginDragDropSource())
    {
    std::string key = pathKey(t.id, path);
    ImGui::SetDragDropPayload(kNodePayload, key.c_str(), key.size() + 1);
    const XmlElement * e = d.element(path);
    ImGui::Text("%s %s", kindTag(e ? PlotDocument::kind(*e) : PlotDocument::kOther), e ? PlotDocument::label(*e).data() : "");
    ImGui::TextDisabled("drop it on the element that is to hold it");
    ImGui::EndDragDropSource();
    }
  if (!ImGui::BeginDragDropTarget()) return;
  if (const ImGuiPayload * payload = ImGui::AcceptDragDropPayload(kNodePayload))
    {
    int from = -1;
    Path source;
    if (parsePathKey(static_cast<const char *>(payload->Data), from, source))
      {
      if (from == t.id)
        {
        bool ok = false;
        Path moved = d.move(source, path, ok);
        if (ok) { t.selected = moved; t.hasSelection = true; }
        else _gui.setStatus("It cannot go there");
        }
      else
        {
        // from another tab: copied
        for (auto & other : _documents)
          if (other.id == from)
            {
            bool ok = false;
            Path p = d.paste(other.document->copy(source), path, ok);
            if (ok) { t.selected = p; t.hasSelection = true; }
            }
        }
      }
    }
  for (const char * type : { FileBrowser::payloadType(), FileBrowser::folderPayloadType() })
    if (const ImGuiPayload * payload = ImGui::AcceptDragDropPayload(type))
      {
      ObjectReference object;
      if (FileBrowser::decodePayload(payload->Data, payload->DataSize, object))
        drop(path, object, std::strcmp(type, FileBrowser::folderPayloadType()) == 0);
      }
  ImGui::EndDragDropTarget();
  }

  void DatumGuiApp::rowMenu(Tab & t, const Path & path)
  {
  PlotDocument & d = *t.document;
  if (!ImGui::BeginPopupContextItem("##rowmenu")) return;
  t.selected = path;
  t.hasSelection = true;
  PlotDocument::Kind kind = d.kind(path);
  auto item = [&](const char * label, const char * command, bool enabled=true)
    {
    if (ImGui::MenuItem(label, nullptr, false, enabled)) actionPerformed(ActionEvent{command, nullptr});
    };
  if (kind == PlotDocument::kRoot || kind == PlotDocument::kWindow) item("Add Canvas", "plot.addCanvas");
  if (kind == PlotDocument::kCanvas || kind == PlotDocument::kGraph) item("Add Graph", "plot.addGraph");
  if (kind == PlotDocument::kGraph || kind == PlotDocument::kHistogram || kind == PlotDocument::kFunction)
    item("Add Function", "plot.addFunction");
  if (kind == PlotDocument::kRoot) item("Add Window", "plot.addWindow");
  if (kind == PlotDocument::kRoot || kind == PlotDocument::kWindow || kind == PlotDocument::kCanvas || kind == PlotDocument::kGraph)
    if (ImGui::MenuItem("Regroup...")) requestRegroup(path, !path.empty());
  if (kind == PlotDocument::kRoot || kind == PlotDocument::kWindow || kind == PlotDocument::kCanvas)
    if (ImGui::MenuItem("Rename..."))
      requestRename(kind == PlotDocument::kRoot ? 1 : (kind == PlotDocument::kWindow ? 2 : 3), path);
  ImGui::Separator();
  bool element = !path.empty();
  item("Cut", "plot.cut", element);
  item("Copy", "plot.copy", element);
  item("Paste", "plot.paste", !_clipboard.empty());
  item("Duplicate", "plot.duplicateElement", element);
  item("Delete", "plot.delete", element);
  ImGui::Separator();
  item("Move Up", "plot.up", element && path.back() > 0);
  item("Move Down", "plot.down", element);
  ImGui::EndPopup();
  }

  void DatumGuiApp::rowControls(Tab & t, const Path & path)
  {
  PlotDocument & d = *t.document;
  const XmlElement * e = d.element(path);
  if (!e) return;
  PlotDocument::Kind kind = PlotDocument::kind(*e);
  // the controls in a column on the right, or after the label if it is long
  float x = std::max(ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX() + 10.0f,
                     ImGui::GetWindowWidth() - 250.0f);
  auto next = [&](float spacing=4.0f) { ImGui::SameLine(0, spacing); };
  ImGui::SameLine(x);
  switch (kind)
    {
    case PlotDocument::kWindow:
      gridControl(d, path, "windowgrid", "canvases of the window: columns x rows");
      break;
    case PlotDocument::kCanvas:
      {
      gridControl(d, path, "canvasgrid", "graphs of the canvas: columns x rows");
      next();
      String bg = d.style(path, "Background", "color");
      int color = bg.isNull() ? 0 : GraphXml::parseColor(bg, 0);
      colorStyle(d, path, "Background", "##bg", color);
      ImGui::SetItemTooltip("background of the canvas");
      break;
      }
    case PlotDocument::kGraph:
      {
      bool logX = d.style(path, "Axis", "log", "x") == "true", logY = d.style(path, "Axis", "log", "y") == "true";
      if (ImGui::Checkbox("log x", &logX)) d.setStyle(path, "Axis", "log", logX ? "true" : "", "x");
      next();
      if (ImGui::Checkbox("log y", &logY)) d.setStyle(path, "Axis", "log", logY ? "true" : "", "y");
      next();
      if (ImGui::SmallButton("Palettes")) ImGui::OpenPopup("palettes");
      ImGui::SetItemTooltip("color, marker and line-style palettes of the items of the graph");
      if (ImGui::BeginPopup("palettes"))
        {
        GRAPHICS::Palettes & palettes = GRAPHICS::Palettes::session();
        String cp = e->attribute("colorPalette", ""), mp = e->attribute("markerPalette", ""), lp = e->attribute("linePalette", "");
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("colors", cp.isNull() ? "default" : cp.data()))
          {
          for (const String & name : palettes.colorPaletteNames())
            {
            const GRAPHICS::ColorPalette * palette = palettes.colorPalette(name);
            if (ImGui::Selectable(name.data(), name == cp)) d.setAttribute(path, "colorPalette", name);
            if (palette)
              for (int k=0; k<std::min(8, palette->size()); k++)
                {
                ImGui::SameLine(0, k ? 1 : 8);
                ImGui::PushID(k);
                int c = palette->color(k);
                GRAPHICS::Color rgb = GRAPHICS::ColorTable::instance().color(c);
                ImGui::ColorButton("##p", ImVec4(rgb.red, rgb.green, rgb.blue, 1), ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
                ImGui::PopID();
                }
            }
          ImGui::EndCombo();
          }
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("markers", mp.isNull() ? "default" : mp.data()))
          {
          for (const String & name : palettes.markerPaletteNames())
            if (ImGui::Selectable(name.data(), name == mp)) d.setAttribute(path, "markerPalette", name);
          ImGui::EndCombo();
          }
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("line styles", lp.isNull() ? "default" : lp.data()))
          {
          for (const String & name : palettes.linePaletteNames())
            if (ImGui::Selectable(name.data(), name == lp)) d.setAttribute(path, "linePalette", name);
          ImGui::EndCombo();
          }
        if (ImGui::Button("Default palettes"))
          {
          d.beginEdit();
          d.setAttribute(path, "colorPalette", "");
          d.setAttribute(path, "markerPalette", "");
          d.setAttribute(path, "linePalette", "");
          d.endEdit();
          }
        ImGui::EndPopup();
        }
      break;
      }
    case PlotDocument::kHistogram:
      {
      int color = itemColor(d, path, "Marker");
      if (colorStyle(d, path, "Marker", "##color", color, "", "Line")) { }
      ImGui::SetItemTooltip("color of the markers and line");
      next();
      int marker = itemMarker(d, path);
      if (markerButton("marker", marker, color)) d.setStyle(path, "Marker", "style", String(std::to_string(marker)));
      break;
      }
    case PlotDocument::kFunction:
      {
      int color = itemColor(d, path, "Line");
      colorStyle(d, path, "Line", "##color", color);
      ImGui::SetItemTooltip("color of the line");
      next();
      String s = d.style(path, "Line", "style");
      int style = s.isNull() ? 1 : std::atoi(s.data());
      if (lineButton("line", style)) d.setStyle(path, "Line", "style", String(std::to_string(style)));
      break;
      }
    default:
      break;
    }
  if (kind != PlotDocument::kStyle && kind != PlotDocument::kOther)
    {
    next(6);
    if (ImGui::SmallButton("...")) ImGui::OpenPopup("attributes");
    ImGui::SetItemTooltip("all the settings");
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 0), ImVec2(700, 640));
    if (ImGui::BeginPopup("attributes"))
      {
      editElement(t, path);
      ImGui::EndPopup();
      }
    }
  }

  void DatumGuiApp::renderInspector(Tab & t)
  {
  PlotDocument & d = *t.document;
  const XmlElement * e = t.hasSelection ? d.element(t.selected) : nullptr;
  if (!e)
    {
    ImGui::TextDisabled("Select an element of the tree to see its settings.");
    if (!t.hasSelection) e = nullptr;
    return;
    }
  PlotDocument::Kind kind = PlotDocument::kind(*e);
  ImGui::TextColored(kindColor(kind), "%s", PlotDocument::kindName(kind));
  ImGui::SameLine();
  ImGui::TextUnformatted(PlotDocument::label(*e).data());
  ImGui::Separator();
  editElement(t, t.selected);
  e = d.element(t.selected);
  if (e && ImGui::CollapsingHeader("XML"))
    {
    // shown as plain text in a child of its own, with a Copy button (not a read-only text field, whose state ImGui keeps
    // from frame to frame while the element shown changes)
    std::string text = e->toString().str();
    if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(text.c_str());
    ImGui::SetItemTooltip("Copy the XML of the element to the clipboard");
    float lines = float(std::count(text.begin(), text.end(), '\n') + 1);
    float height = std::min(300.0f, ImGui::GetStyle().WindowPadding.y*2.0f + lines*ImGui::GetTextLineHeightWithSpacing());
    if (ImGui::BeginChild("##xml", ImVec2(-1, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
      ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
    ImGui::EndChild();
    }
  }

  void DatumGuiApp::editElement(Tab & t, const Path & p)
  {
  PlotDocument & d = *t.document;
  const XmlElement * e = d.element(p);
  if (!e) return;
  ImGui::PushID(pathKey(t.id, p).c_str());
  switch (PlotDocument::kind(*e))
    {
    case PlotDocument::kRoot:
      ImGui::TextWrapped("The plot file: inputs (Datum files), outputs, and windows of canvases. Drop histograms here to "
                         "add a canvas to the last window.");
      break;
    case PlotDocument::kInput:
      {
      textField(d, p, "id", "identifier", 200);
      textField(d, p, "file", "file", -1, "path of the Datum file (relative to the plot file)");
      ImGui::SameLine();
      if (ImGui::SmallButton("Browse..."))
        {
        int id = t.id;
        Path path = p;
        _gui.fileDialog().open(FileDialog::kOpenFile, "Datum file of the input", d.directory(), {DatumFile::extension(), DatumFile::formerExtension()}, "",
          [this, id, path](const String & f)
          {
          for (auto & tt : _documents)
            if (tt.id == id) tt.document->setAttribute(path, "file", PlotDocument::relativePath(f, tt.document->directory()));
          });
        }
      String f = e->attribute("file", "");
      String resolved = f.isNull() ? f : (f.str()[0] == '/' ? f : FileSystem::join(d.directory(), f));
      if (!f.isNull() && !FileSystem::isFile(resolved)) ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.1f, 1), "file not found: %s", resolved.data());
      break;
      }
    case PlotDocument::kOutput:
      {
      textField(d, p, "directory", "directory", -1, "folder of the outputs (relative to the plot file)");
      textField(d, p, "pdf", "PDF book", 260);
      textField(d, p, "pptx", "PowerPoint", 260);
      textField(d, p, "latex", "LaTeX", 260);
      int style = e->attribute("latexStyle", "beamer") == "article" ? 1 : 0;
      const char * styles[] = { "beamer (one frame per canvas)", "article (one figure per canvas)" };
      ImGui::SetNextItemWidth(260);
      if (ImGui::Combo("LaTeX style", &style, styles, 2)) d.setAttribute(p, "latexStyle", style ? "article" : "beamer");
      checkField(d, p, "latexCompile", "compile with pdflatex", false);
      textField(d, p, "title", "title");
      textField(d, p, "author", "author");
      textField(d, p, "affiliation", "affiliation");
      break;
      }
    case PlotDocument::kWindow:
      {
      textField(d, p, "title", "title");
      intField(d, p, "width", "width", 1200, 200, 6000);
      ImGui::SameLine();
      intField(d, p, "height", "height", 800, 200, 6000);
      ImGui::TextUnformatted("canvases:");
      ImGui::SameLine();
      gridControl(d, p, "inspgrid", "canvases of the window: columns x rows");
      break;
      }
    case PlotDocument::kCanvas:
      {
      textField(d, p, "name", "name", 220);
      textField(d, p, "title", "title");
      textField(d, p, "file", "printed to", 260, "e.g. spectra.pdf (or .svg); empty: not printed");
      intField(d, p, "width", "width", 800, 100, 6000);
      ImGui::SameLine();
      intField(d, p, "height", "height", 600, 100, 6000);
      ImGui::TextUnformatted("graphs:");
      ImGui::SameLine();
      gridControl(d, p, "inspcanvasgrid", "graphs of the canvas: columns x rows");
      String bg = d.style(p, "Background", "color");
      int color = bg.isNull() ? 0 : GraphXml::parseColor(bg, 0);
      colorStyle(d, p, "Background", "background", color);
      break;
      }
    case PlotDocument::kGraph:
      {
      textField(d, p, "title", "title");
      textField(d, p, "file", "printed to", 260, "e.g. graph.pdf; empty: not printed");
      intField(d, p, "cell", "cell", -1, -1, 99);
      ImGui::SetItemTooltip("cell of the canvas (0, 1, ... row by row); -1: the next one");
      checkField(d, p, "legend", "legend", true);
      ImGui::SameLine();
      for (const char * axis : { "x", "y", "z" })
        {
        bool log = d.style(p, "Axis", "log", axis) == "true";
        std::string label = std::string("log ") + axis;
        if (ImGui::Checkbox(label.c_str(), &log)) d.setStyle(p, "Axis", "log", log ? "true" : "", axis);
        ImGui::SameLine();
        }
      ImGui::NewLine();
      if (ImGui::TreeNode("Axes"))
        {
        for (const char * axis : { "x", "y", "z" })
          {
          ImGui::PushID(axis);
          std::string title = d.style(p, "Axis", "title", axis).str();
          ImGui::SetNextItemWidth(220);
          if (ImGui::InputText((std::string(axis) + " title").c_str(), &title)) d.setStyle(p, "Axis", "title", String(title), axis);
          ImGui::SameLine();
          std::string mn = d.style(p, "Axis", "min", axis).str(), mx = d.style(p, "Axis", "max", axis).str();
          ImGui::SetNextItemWidth(70);
          if (ImGui::InputTextWithHint("##min", "min", &mn)) d.setStyle(p, "Axis", "min", String(mn), axis);
          ImGui::SameLine();
          ImGui::SetNextItemWidth(70);
          if (ImGui::InputTextWithHint("##max", "max", &mx)) d.setStyle(p, "Axis", "max", String(mx), axis);
          ImGui::PopID();
          }
        ImGui::TreePop();
        }
      if (ImGui::TreeNode("Margins"))
        {
        const char * sides[] = { "left", "bottom", "right", "top" };
        const float defaults[] = { 0.15f, 0.15f, 0.05f, 0.10f };
        for (int k=0; k<4; k++)
          {
          String v = d.style(p, "Margins", sides[k]);
          float value = v.isNull() ? defaults[k] : float(std::atof(v.data()));
          ImGui::SetNextItemWidth(160);
          if (ImGui::SliderFloat(sides[k], &value, 0.0f, 0.45f, "%.3f")) d.setStyle(p, "Margins", sides[k], String(number(value)));
          if (k % 2 == 0) ImGui::SameLine();
          }
        if (ImGui::SmallButton("Default margins")) { d.beginEdit(); for (const char * s : sides) d.setStyle(p, "Margins", s, ""); d.endEdit(); }
        ImGui::TreePop();
        }
      // everything else, with the editors of DatumGui: written back as the differences to the defaults
      GRAPHICS::Graph graph;
      GraphXml::readGraph(*e, graph);
      bool changed = false;
      if (ImGui::TreeNode("Title: color, font, size"))   { changed |= GraphEditor::editTitle(graph); ImGui::TreePop(); }
      for (int k=0; k<3; k++)
        {
        const char * names[] = { "x axis: ticks, labels, title style", "y axis: ticks, labels, title style", "z axis: ticks, labels, title style" };
        if (ImGui::TreeNode(names[k])) { changed |= GraphEditor::editAxis(graph, k); changed |= GraphEditor::editAxisTitle(graph, k); ImGui::TreePop(); }
        }
      if (ImGui::TreeNode("Legend: position, text, box")) { changed |= GraphEditor::editLegend(graph); ImGui::TreePop(); }
      if (changed)
        {
        XmlElement full("Graph"), defaults("Graph");
        GraphXml::writeGraph(full, graph);
        GraphXml::writeGraph(defaults, GRAPHICS::Graph());
        XmlElement merged = *d.element(p);
        PlotDocument::mergeDifferences(merged, full, defaults);
        d.replace(p, merged);
        }
      break;
      }
    case PlotDocument::kHistogram:
      {
      std::vector<String> ids;
      for (const XmlElement * in : d.root().children("Input")) ids.push_back(in->attribute("id", ""));
      String input = e->attribute("input", "");
      ImGui::SetNextItemWidth(200);
      if (ImGui::BeginCombo("input", input.isNull() ? "(first)" : input.data()))
        {
        for (const String & id : ids) if (ImGui::Selectable(id.data(), id == input)) d.setAttribute(p, "input", id);
        ImGui::EndCombo();
        }
      textField(d, p, "path", "path", -1, "path of the histogram in the file");
      textField(d, p, "label", "label", -1, "text of the legend (ROOT or LaTeX syntax)");
      checkField(d, p, "showMarkers", "markers", true);
      ImGui::SameLine();
      checkField(d, p, "showLine", "line", false);
      ImGui::SameLine();
      checkField(d, p, "showErrors", "error bars", true);
      int color = itemColor(d, p, "Marker");
      colorStyle(d, p, "Marker", "marker color", color);
      ImGui::SameLine(0, 20);
      int marker = itemMarker(d, p);
      if (GraphEditor::markerEdit("marker", marker, color)) d.setStyle(p, "Marker", "style", String(std::to_string(marker)));
      String size = d.style(p, "Marker", "size");
      float s = size.isNull() ? 1.0f : float(std::atof(size.data()));
      ImGui::SetNextItemWidth(160);
      if (ImGui::SliderFloat("marker size", &s, 0.2f, 4.0f, "%.2f")) d.setStyle(p, "Marker", "size", String(number(s)));
      int lineColor = itemColor(d, p, "Line");
      colorStyle(d, p, "Line", "line color", lineColor);
      ImGui::SameLine(0, 20);
      String ls = d.style(p, "Line", "style");
      int lineStyle = ls.isNull() ? 1 : std::atoi(ls.data());
      if (GraphEditor::lineStyleEdit("line style", lineStyle)) d.setStyle(p, "Line", "style", String(std::to_string(lineStyle)));
      String lw = d.style(p, "Line", "width");
      float w = lw.isNull() ? 1.0f : float(std::atof(lw.data()));
      ImGui::SetNextItemWidth(160);
      if (ImGui::SliderFloat("line width", &w, 0.5f, 6.0f, "%.1f")) d.setStyle(p, "Line", "width", String(number(w)));
      if (ImGui::SmallButton("Default style"))
        {
        d.beginEdit();
        for (const char * child : { "Marker", "Line" })
          for (const char * a : { "color", "style", "size", "width", "alpha" }) d.setStyle(p, child, a, "");
        d.endEdit();
        }
      break;
      }
    case PlotDocument::kFunction:
      {
      std::vector<String> names = MATH::FunctionRegistry::names();
      for (const String & n : MATH::FunctionRegistry::compositeNames()) names.push_back(n);
      String cls = e->attribute("class", "");
      ImGui::SetNextItemWidth(220);
      if (ImGui::BeginCombo("class", cls.data()))
        {
        for (const String & n : names) if (ImGui::Selectable(n.data(), n == cls)) d.setAttribute(p, "class", n);
        ImGui::EndCombo();
        }
      textField(d, p, "parameters", "parameters", -1, "values separated by commas");
      textField(d, p, "label", "label");
      numberField(d, p, "xMin", "x min", -1.0);
      ImGui::SameLine();
      numberField(d, p, "xMax", "x max", 1.0);
      numberField(d, p, "yMin", "y min (rank 2)", 0.0);
      ImGui::SameLine();
      numberField(d, p, "yMax", "y max (rank 2)", 1.0);
      intField(d, p, "nSamples", "points drawn", 200, 2, 100000);
      int color = itemColor(d, p, "Line");
      colorStyle(d, p, "Line", "line color", color);
      ImGui::SameLine(0, 20);
      String ls = d.style(p, "Line", "style");
      int lineStyle = ls.isNull() ? 1 : std::atoi(ls.data());
      if (GraphEditor::lineStyleEdit("line style", lineStyle, false)) d.setStyle(p, "Line", "style", String(std::to_string(lineStyle)));
      String lw = d.style(p, "Line", "width");
      float w = lw.isNull() ? 2.0f : float(std::atof(lw.data()));
      ImGui::SetNextItemWidth(160);
      if (ImGui::SliderFloat("line width", &w, 0.5f, 6.0f, "%.1f")) d.setStyle(p, "Line", "width", String(number(w)));
      break;
      }
    default:
      {
      // any other element: its attributes as text
      std::vector<std::pair<String,String>> attributes = e->attributes();
      for (const auto & a : attributes)
        {
        std::string value = a.second.str();
        ImGui::SetNextItemWidth(260);
        if (ImGui::InputText(a.first.data(), &value)) d.setAttribute(p, a.first, String(value));
        ImGui::SameLine();
        ImGui::PushID(a.first.data());
        if (ImGui::SmallButton("x")) d.setAttribute(p, a.first, "");
        ImGui::PopID();
        }
      static std::string newName, newValue;
      ImGui::SetNextItemWidth(120);
      ImGui::InputTextWithHint("##newname", "attribute", &newName);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(140);
      ImGui::InputTextWithHint("##newvalue", "value", &newValue);
      ImGui::SameLine();
      if (ImGui::SmallButton("Add") && XmlElement::isValidName(String(newName)) && !newValue.empty())
        {
        d.setAttribute(p, String(newName), String(newValue));
        newName.clear();
        newValue.clear();
        }
      break;
      }
    }
  ImGui::PopID();
  }

  void DatumGuiApp::requestRegroup(const Path & scope, bool onlyScope)
  {
  Tab * t = tab();
  if (!t) return;
  _regroup.requested = true;
  _regroup.tab = t->id;
  _regroup.scope = scope;
  _regroup.onlyScope = onlyScope && !scope.empty();
  _regroup.previewed = false;
  _regroup.message.clear();
  }

  void DatumGuiApp::renderRegroupDialog()
  {
  const char * title = "Regroup Histograms##plotRegroup";
  if (_regroup.requested) { ImGui::OpenPopup(title); _regroup.requested = false; }
  ImGui::SetNextWindowSize(ImVec2(700, 560), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(title, nullptr)) return;
  Tab * t = nullptr;
  for (auto & tab : _documents) if (tab.id == _regroup.tab) t = &tab;
  if (!t) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
  PlotDocument & d = *t->document;
  RegroupDialog & r = _regroup;

  ImGui::TextWrapped("The histograms and functions whose names match the pattern are moved into common graphs of new "
                     "canvases, placed after the canvas of the first one. Preview shows what would be done; OK does it "
                     "(Edit > Undo undoes it).");
  ImGui::Spacing();
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(-160);
  bool preview = ImGui::InputTextWithHint("pattern", "e.g. *_pt   or   *PiP*_pt; *PiM*_pt   or   MB_*/n1_pt", &r.pattern,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SetItemTooltip("Wildcards: * any text, ? one character; several patterns separated by ';'.\n"
                        "Without '/': matched against the name of a histogram (the last part of its path), then its whole path.\n"
                        "With '/': matched against its whole path in its file. Functions: their label, or class.\n"
                        "Press Enter for a preview.");
  ImGui::Checkbox("regular expression", &r.regularExpression);
  ImGui::SetItemTooltip("The pattern is an ECMAScript regular expression, searched in the whole path (e.g. Pi[PM]/n1_pt$)");
  ImGui::SameLine(0, 24);
  ImGui::Checkbox("case sensitive", &r.caseSensitive);
  if (!r.scope.empty() && d.element(r.scope))
    {
    std::string where = std::string(PlotDocument::kindName(d.kind(r.scope))) + " " + PlotDocument::label(*d.element(r.scope)).str();
    int only = r.onlyScope ? 1 : 0;
    ImGui::RadioButton("search the whole plot file", &only, 0);
    ImGui::SameLine(0, 24);
    ImGui::RadioButton((std::string("search only in the ") + where).c_str(), &only, 1);
    r.onlyScope = only == 1;
    }
  ImGui::SetNextItemWidth(120);
  if (ImGui::InputInt("histograms per graph", &r.itemsPerGraph)) r.itemsPerGraph = std::max(0, r.itemsPerGraph);
  ImGui::SetItemTooltip("At most this many histograms and functions in a graph; more graphs are made as needed (0: no limit)");
  ImGui::SameLine(0, 24);
  ImGui::SetNextItemWidth(120);
  if (ImGui::InputInt("graphs per canvas", &r.graphsPerCanvas)) r.graphsPerCanvas = std::max(0, r.graphsPerCanvas);
  ImGui::SetItemTooltip("At most this many graphs in a canvas; more canvases are made as needed (0: no limit)");
  ImGui::SameLine(0, 24);
  ImGui::SetNextItemWidth(120);
  if (ImGui::InputInt("canvases per window", &r.canvasesPerWindow)) r.canvasesPerWindow = std::max(0, r.canvasesPerWindow);
  ImGui::SetItemTooltip("0: the new canvases go into the window of the first histogram; otherwise into new windows of at most\n"
                        "this many canvases each, after it");
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("canvas name", "from the pattern", &r.canvasName);
  ImGui::SameLine(0, 24);
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("graph title", "none", &r.graphTitle);
  ImGui::Checkbox("label the histograms by what tells them apart", &r.labelItems);
  ImGui::SetItemTooltip("Histograms without a label get one for the legend: the text matched by the wildcards, or the parts of\n"
                        "their paths (and inputs) that differ");
  ImGui::SameLine(0, 24);
  ImGui::Checkbox("remove the graphs and canvases left empty", &r.removeEmpty);

  PlotDocument::RegroupOptions o;
  o.pattern = String(r.pattern.c_str());
  o.regularExpression = r.regularExpression;
  o.caseSensitive = r.caseSensitive;
  if (r.onlyScope) o.scope = r.scope;
  o.itemsPerGraph = r.itemsPerGraph;
  o.graphsPerCanvas = r.graphsPerCanvas;
  o.canvasesPerWindow = r.canvasesPerWindow;
  o.canvasName = String(r.canvasName.c_str());
  o.graphTitle = String(r.graphTitle.c_str());
  o.labelItems = r.labelItems;
  o.removeEmpty = r.removeEmpty;
  std::string key = r.pattern + "|" + std::to_string(r.regularExpression) + std::to_string(r.caseSensitive)
                  + std::to_string(r.onlyScope) + std::to_string(r.labelItems) + "|" + std::to_string(r.itemsPerGraph) + "|"
                  + std::to_string(r.graphsPerCanvas) + "|" + std::to_string(r.canvasesPerWindow) + "|" + r.canvasName + "|" + std::to_string(d.version());

  ImGui::Spacing();
  preview = ImGui::Button("Preview") || preview;
  ImGui::SameLine();
  bool ok = ImGui::Button("OK");
  ImGui::SameLine();
  bool cancel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);
  if (preview)
    {
    r.plan = d.planRegroup(o);
    r.previewed = true;
    r.previewKey = key;
    r.message.clear();
    }
  if (r.previewed && r.previewKey != key)
    {
    ImGui::SameLine(0, 18);
    ImGui::TextDisabled("(changed since the preview)");
    }
  if (!r.message.empty()) { ImGui::SameLine(0, 18); ImGui::TextColored(ImVec4(0.85f, 0.2f, 0.2f, 1.0f), "%s", r.message.c_str()); }

  // the preview: the new canvases, their graphs and the histograms going there
  ImGui::Separator();
  if (ImGui::BeginChild("##regroupPreview", ImVec2(0, 0), ImGuiChildFlags_Borders))
    {
    const PlotDocument::RegroupPlan & p = r.plan;
    if (!r.previewed) ImGui::TextDisabled("Type a pattern, then Preview (or Enter)");
    else if (!p.error.isNull()) ImGui::TextColored(ImVec4(0.85f, 0.2f, 0.2f, 1.0f), "%s", p.error.data());
    else
      {
      if (p.windows.empty())
        ImGui::Text("%d histograms and functions into %d graphs in %d canvases", int(p.entries.size()), int(p.graphs.size()),
                    int(p.canvases.size()));
      else
        ImGui::Text("%d histograms and functions into %d graphs in %d canvases in %d new windows", int(p.entries.size()),
                    int(p.graphs.size()), int(p.canvases.size()), int(p.windows.size()));
      std::vector<int> windowOf(p.canvases.size(), -1);
      for (std::size_t w=0; w<p.windows.size(); w++) for (int c : p.windows[w]) windowOf[std::size_t(c)] = int(w);
      for (std::size_t c=0; c<p.canvases.size(); c++)
        {
        if (windowOf[c] >= 0 && p.windows[std::size_t(windowOf[c])].front() == int(c))
          ImGui::TextDisabled("Window %s", p.windowTitles[std::size_t(windowOf[c])].data());
        ImGui::PushID(int(c));
        if (ImGui::TreeNodeEx("canvas", ImGuiTreeNodeFlags_DefaultOpen, "Canvas %s", p.canvasNames[c].data()))
          {
          for (int g : p.canvases[c])
            {
            ImGui::PushID(g);
            if (ImGui::TreeNodeEx("graph", ImGuiTreeNodeFlags_DefaultOpen, "Graph %d  (%d)", g + 1, int(p.graphs[std::size_t(g)].size())))
              {
              if (ImGui::BeginTable("##items", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
                {
                for (int i : p.graphs[std::size_t(g)])
                  {
                  const PlotDocument::RegroupEntry & e = p.entries[std::size_t(i)];
                  ImGui::TableNextRow();
                  ImGui::TableNextColumn();
                  ImGui::TextUnformatted(e.name.data());
                  ImGui::TableNextColumn();
                  if (!e.label.isNull()) ImGui::Text("\"%s\"", e.label.data());
                  ImGui::TableNextColumn();
                  ImGui::TextDisabled("from %s", e.location.data());
                  }
                ImGui::EndTable();
                }
              ImGui::TreePop();
              }
            ImGui::PopID();
            }
          ImGui::TreePop();
          }
        ImGui::PopID();
        }
      }
    }
  ImGui::EndChild();

  if (ok)
    {
    PlotDocument::RegroupPlan done;
    Path canvas = d.regroup(o, &done);
    if (canvas.empty())
      {
      r.message = done.error.isNull() ? std::string("Nothing to regroup") : done.error.str();
      r.plan = done;
      r.previewed = true;
      r.previewKey = key;
      }
    else
      {
      t->selected = canvas;
      t->hasSelection = true;
      char text[160];
      std::snprintf(text, sizeof(text), "Regrouped %d histograms and functions into %d graphs in %d canvases%s",
                    int(done.entries.size()), int(done.graphs.size()), int(done.canvases.size()),
                    done.windows.empty() ? "" : " in new windows (Windows > Detached Windows shows them all)");
      _gui.setStatus(String(text));
      r.previewed = false;
      ImGui::CloseCurrentPopup();
      }
    }
  if (cancel) { r.previewed = false; ImGui::CloseCurrentPopup(); }
  ImGui::EndPopup();
  }

  void DatumGuiApp::renderDialogs()
  {
  renderRenameDialog();
  renderRegroupDialog();
#ifdef DATUM_WITH_HEPDATA
  _hepDataDialog.render(_gui.fileDialog());
#endif
  if (_closeDialog) { ImGui::OpenPopup("Save changes?"); _closeDialog = false; }
  if (ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
    int k = -1;
    for (std::size_t j=0; j<_documents.size() && !_closing.empty(); j++) if (_documents[j].id == _closing.front()) k = int(j);
    if (k < 0) { _closing.clear(); ImGui::CloseCurrentPopup(); }
    else
      {
      ImGui::Text("Save the changes made to %s?", _documents[std::size_t(k)].document->displayName().data());
      auto nextOne = [this]()
        {
        _closing.erase(_closing.begin());
        if (_closing.empty() && _exitAfterClosing) _gui.requestExit();
        else if (!_closing.empty()) _closeDialog = true;
        };
      if (ImGui::Button("Save", ImVec2(110, 0)))
        {
        int id = _closing.front();
        ImGui::CloseCurrentPopup();
        save(k, false, [this, id, nextOne]()
          {
          for (std::size_t j=0; j<_documents.size(); j++) if (_documents[j].id == id) { closeTab(int(j)); break; }
          nextOne();
          });
        }
      ImGui::SameLine();
      if (ImGui::Button("Don't Save", ImVec2(110, 0)))
        {
        closeTab(k);
        ImGui::CloseCurrentPopup();
        nextOne();
        }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(110, 0)))
        {
        _closing.clear();
        _exitAfterClosing = false;
        ImGui::CloseCurrentPopup();
        }
      }
    ImGui::EndPopup();
    }
  if (_showHelp)
    {
    ImGui::SetNextWindowSize(ImVec2(760, 360), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("DatumGui Help", &_showHelp)) helpText();
    ImGui::End();
    }
  if (_gridDialog) { ImGui::OpenPopup("Grid"); _gridDialog = false; }
  if (ImGui::BeginPopup("Grid"))
    {
    ImGui::TextDisabled("%s", _gridOfGraphs ? "graphs of the current canvas" : "canvases of the window");
    if (gridPicker(_gridColumns, _gridRows)) { setGrid(_gridOfGraphs, _gridColumns, _gridRows); ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
    }
  }

  // ------------------------------------------------------------------------------------------------ canvases

  const MATH::DataPoints * DatumGuiApp::rebinnedData(CanvasPane & pane, const MATH::DataPoints * data,
                                                   const std::vector<unsigned int> & factors)
  {
  // a canvas of the plot file: the copy belongs to the specification, which writes it back (attribute rebin) and
  // makes it again when the canvas is drawn again
  bool linked = false;
  for (const auto & link : _links) if (link.pane == &pane) linked = true;
  if (linked && _spec)
    {
    std::vector<unsigned int> previous;
    DatumApp::rebinSourceOf(data, previous);
    if (previous.empty()) return _spec->rebinned(data, factors);
    }
  return DatumApp::rebinnedData(pane, data, factors);
  }

  const MATH::DataPoints * DatumGuiApp::rebinSourceOf(const MATH::DataPoints * data, std::vector<unsigned int> & factors) const
  {
  if (_spec)
    {
    const MATH::DataPoints * source = _spec->rebinSource(data, factors);
    if (!factors.empty()) return source;
    }
  return DatumApp::rebinSourceOf(data, factors);
  }

  int DatumGuiApp::paneIndexOf(const CanvasPane * pane)
  {
  for (int k=0; k<_gui.nPanes(); k++) if (&_gui.pane(k) == pane) return k;
  return -1;
  }

  std::vector<int> DatumGuiApp::closeCanvases(const std::vector<int> & panes)
  {
  std::vector<int> rest;
  std::vector<Path> canvases;
  Tab * t = tab();
  for (int k : panes)
    {
    const PaneLink * found = nullptr;
    for (const auto & link : _links)
      if (paneIndexOf(link.pane) == k && !link.canvas.empty()) { found = &link; break; }
    if (t && found && t->document->element(found->canvas)) canvases.push_back(found->canvas);
    else rest.push_back(k);
    }
  if (canvases.empty()) return rest;
  // removed last first, so that the paths of the others stay valid
  std::sort(canvases.begin(), canvases.end());
  canvases.erase(std::unique(canvases.begin(), canvases.end()), canvases.end());
  PlotDocument & d = *t->document;
  d.beginEdit();
  for (auto p = canvases.rbegin(); p != canvases.rend(); ++p)
    {
    if (t->hasSelection && t->selected.size() >= p->size() && std::equal(p->begin(), p->end(), t->selected.begin()))
      t->selected = PlotDocument::parent(*p);   // the selection was in the canvas: its window
    d.remove(*p);
    }
  d.endEdit();
  _gui.setStatus(canvases.size() == 1 ? String("Canvas removed from the plot file (Undo to restore it)")
                                      : String("Canvases removed from the plot file (Undo to restore them)"));
  return rest;
  }

  void DatumGuiApp::showTree()
  {
  if (!_showTree && !_treeShownOnce) _showInspector = true;   // the inspector comes with the tree the first time
  _treeShownOnce = true;
  _showTree = true;
  _gui.setSideWindowVisible(true);
  }

  CanvasPane * DatumGuiApp::plotPane()
  {
  if (_gui.nPanes() == 0) return nullptr;
  CanvasPane & current = _gui.currentPane();
  auto linked = [this](const CanvasPane * p) { for (const auto & link : _links) if (link.pane == p && !link.canvas.empty()) return true; return false; };
  if (linked(&current)) return &current;
  int k = _gui.paneIndexOfWindow(_plotPane);
  if (k >= 0 && linked(&_gui.pane(k))) return &_gui.pane(k);
  for (const auto & link : _links) if (!link.canvas.empty() && paneIndexOf(link.pane) >= 0) return link.pane;
  return &current;
  }

  bool DatumGuiApp::currentCanvas(Path & canvas, Path & graph)
  {
  canvas.clear();
  graph.clear();
  if (_gui.nPanes() == 0) return false;
  CanvasPane & pane = *plotPane();
  for (const auto & link : _links)
    {
    if (link.pane != &pane || link.canvas.empty()) continue;
    canvas = link.canvas;
    auto found = link.graphs.find(pane.selectedGraph());
    if (found != link.graphs.end()) graph = found->second;
    return true;
    }
  return false;
  }

  void DatumGuiApp::clearGraph(const Path & graph, CanvasPane * pane, int cell)
  {
  Tab * t = tab();
  if (!t || graph.empty()) return;
  PlotDocument & d = *t->document;
  std::vector<Path> items = d.children(graph, PlotDocument::kHistogram);
  for (const Path & p : d.children(graph, PlotDocument::kFunction)) items.push_back(p);
  std::sort(items.begin(), items.end());
  for (auto p = items.rbegin(); p != items.rend(); ++p) d.remove(*p);   // last first: the others keep their paths
  if (pane && !pane->keepShapes() && cell >= 0 && cell < pane->canvas().nGraphs()) pane->canvas().graph(cell).clearShapes();
  }

  void DatumGuiApp::showObject(const ObjectReference & object)
  {
  Tab * t = tab();
  if (!t) { DatumApp::showObject(object); return; }   // no plot file: a canvas of the current window
  String fileName;
  if (objectsOf(object, false, fileName).empty()) { _gui.setStatus("Nothing to plot there"); return; }
  Path target = dropTarget(), canvas, graph;
  CanvasPane * pane = plotPane();
  bool replace = pane && pane->mode() == CanvasPane::kReplace;
  if (currentCanvas(canvas, graph) && !graph.empty())
    {
    // the graph selected on the current canvas, unless an element of it is selected in the tree (Add: added there)
    bool inGraph = t->hasSelection && t->selected.size() >= graph.size()
                   && std::equal(graph.begin(), graph.end(), t->selected.begin());
    if (replace || !inGraph) target = graph;
    }
  else replace = false;    // no graph of the plot file shown: a new canvas
  if (pane && pane != &_gui.currentPane()) _gui.setCurrentPane(paneIndexOf(pane), false);   // shown there: it is current again
  if (replace) { replaceGraph(graph, object, pane, pane->selectedGraph()); return; }
  drop(target, object, false);
  }

  void DatumGuiApp::replaceGraph(const Path & graph, const ObjectReference & object, CanvasPane * pane, int cell)
  {
  Tab * t = tab();
  if (!t) return;
  PlotDocument & d = *t->document;
  auto lastPart = [](const String & path) { std::string s = path.str(); std::size_t k = s.rfind('/'); return String(k == std::string::npos ? s : s.substr(k + 1)); };
  // the title of the graph follows when it was the name of its first histogram
  bool follow = false;
  if (const XmlElement * g = d.element(graph))
    {
    std::vector<Path> items = d.children(graph, PlotDocument::kHistogram);
    const XmlElement * first = items.empty() ? nullptr : d.element(items.front());
    String title = g->attribute("title");
    follow = title.isNull() || (first && title == lastPart(first->attribute("path")));
    }
  d.beginEdit();
  clearGraph(graph, pane, cell);
  if (drop(graph, object, false) && follow) d.setAttribute(graph, "title", lastPart(object.path));
  d.endEdit();
  }

  void DatumGuiApp::showFolder(const ObjectReference & object)
  {
  // all the objects of a folder: a graph each, in the canvas of the selection or of the current pane (or a new canvas)
  Tab * t = tab();
  if (!t) { DatumApp::showFolder(object.file, object.path); return; }   // no plot file: a canvas of the current window
  String fileName;
  if (objectsOf(object, true, fileName).empty()) { _gui.setStatus("Nothing to plot there"); return; }
  bool ok = false;
  Path canvas = t->hasSelection ? t->document->enclosing(t->selected, PlotDocument::kCanvas, ok) : Path();
  Path paneCanvas, graph;
  if (!ok && currentCanvas(paneCanvas, graph)) { canvas = paneCanvas; ok = true; }
  CanvasPane * shown = plotPane();
  bool replace = ok && shown && shown->mode() == CanvasPane::kReplace;
  PlotDocument & d = *t->document;
  d.beginEdit();
  if (replace)
    {
    // Replace: the graphs of the canvas go, the folder takes their place
    std::vector<Path> graphs = d.children(canvas, PlotDocument::kGraph);
    for (auto p = graphs.rbegin(); p != graphs.rend(); ++p) d.remove(*p);
    if (!shown->keepShapes())
      for (int k=0; k<shown->canvas().nGraphs(); k++) shown->canvas().graph(k).clearShapes();
    }
  drop(ok ? canvas : Path(), object, true);
  d.endEdit();
  }

  void DatumGuiApp::dropOnPane(const Path & target, const ObjectReference & object, CanvasPane & pane, int cell)
  {
  Tab * t = tab();
  if (!t) return;
  String fileName;
  if (objectsOf(object, false, fileName).empty()) { _gui.setStatus("Nothing to plot there"); return; }
  PlotDocument & d = *t->document;
  const XmlElement * e = d.element(target);
  bool replace = pane.mode() == CanvasPane::kReplace && e && PlotDocument::kind(*e) == PlotDocument::kGraph;
  if (replace) { replaceGraph(target, object, &pane, cell); return; }
  drop(target, object, false);
  }

  PlotDocument::Path DatumGuiApp::dropTarget()
  {
  Tab * t = tab();
  if (!t || !t->hasSelection) return Path();
  bool ok = false;
  Path g = t->document->enclosing(t->selected, PlotDocument::kGraph, ok);
  if (ok) return g;
  Path c = t->document->enclosing(t->selected, PlotDocument::kCanvas, ok);
  if (ok) return c;
  return t->selected;
  }

  void DatumGuiApp::layoutCanvases()
  {
  Tab * t = tab();
  if (!t)
    {
    // no plot file: its windows close
    bool shown = false;
    for (int k=0; k<_gui.nPaneWindows(); k++) if (_gui.paneWindowShown(k)) shown = true;
    if (shown) _gui.closePaneWindowsFrom(0);
    _drawnTab = -1;
    _links.clear();
    return;
    }
  PlotDocument & d = *t->document;
  std::vector<Path> windows = d.children(Path(), PlotDocument::kWindow);
  int window = _drawnTab == t->id ? _drawnWindow : 0, canvas = -1;
  if (t->hasSelection) d.locate(t->selected, window, canvas);
  if (window < 0 || window >= int(windows.size())) window = windows.empty() ? -1 : 0;
  _detachedWindows = configuration().gui.detachedWindows;   // also set in Datum > Preferences
  bool all = _allWindows || _detachedWindows;
  if (all) window = windows.empty() ? -1 : 0;
  bool linksValid = true;
  for (const auto & link : _links) if (paneIndexOf(link.pane) < 0) linksValid = false;   // a pane closed by the user
  if (linksValid && _drawnTab == t->id && _drawnVersion == d.version() && _drawnWindow == window && _drawnAll == _allWindows
      && _drawnDetached == _detachedWindows) return;
  // shapes drawn on the canvases are kept when the canvases are drawn again
  std::map<std::string, std::vector<GRAPHICS::Shape>> shapes;
  for (const auto & link : _links)
    {
    if (paneIndexOf(link.pane) < 0) continue;
    const GRAPHICS::Canvas & c = link.pane->canvas();
    for (int k=0; k<c.nGraphs(); k++)
      if (!c.graph(k).shapes().empty())
        {
        std::string key;
        for (int i : link.canvas) key += std::to_string(i) + ".";
        shapes[key + "#" + std::to_string(k)] = c.graph(k).shapes();
        }
    }
  _links.clear();
  _drawnTab = t->id;
  _drawnVersion = d.version();
  _drawnWindow = window;
  _drawnAll = _allWindows;
  _drawnDetached = _detachedWindows;
  std::unique_ptr<GRAPHICS::PlotSpec> spec(new GRAPHICS::PlotSpec());
  String error, problems;
  String title = String("DatumGui: ") + d.displayName();
  if (window < 0 || !spec->read(d.document(), d.directory(), error))
    {
    // the DatumGui window of the plot file, with one canvas to drop histograms onto
    _gui.closePaneWindowsFrom(1);
    _gui.setPaneWindow(0, title, 1, 1, 1000, 700);
    _gui.setPaneWindowGui(0, true);
    _gui.setPaneWindowPaneCount(0, 1);
    CanvasPane & pane = *_gui.panesOf(0)[0];
    pane.canvas() = GRAPHICS::Canvas();
    pane.divide(1, 1);
    pane.setSelectedGraph(0);
    pane.setTitle(window < 0 ? "Drop histograms here" : "Cannot draw the plot file");
    pane.onDrop = [this](int, const ObjectReference & object) { drop(Path(), object, false); };
    if (window >= 0) _gui.setStatus(String("Cannot draw the plot file: ") + error);
    _spec = std::move(spec);
    return;
    }
  // the inputs, opened by the application (shown in the browser, shared with it)
  for (std::size_t k=0; k<spec->inputs().size(); k++)
    {
    String path = spec->resolve(spec->inputs()[k].path);
    BrowserFile * f = dataFile(path);                  // a tree of the browser (saved or not), or a file read
    if (!f && !FileSystem::isFile(path))
      {
      if (_missing.insert(path.str()).second)
        _gui.setStatus(path.str().compare(0, 7, "/@tree/") == 0 ? String("A tree not saved is no longer open: ") + path
                                                                : String("No input file ") + path);
      continue;
      }
    int id = f ? f->id : openFile(path);
    f = id >= 0 ? file(id) : nullptr;
    if (f) spec->setInputFile(int(k), f->file);
    }
  // the windows drawn: the first one (or the one selected) in the DatumGui window of the plot file (menus, status
  // bar), and with All Windows or Detached Windows the others in windows of canvases of their own
  std::vector<int> shown;
  if (all) for (int w=0; w<int(windows.size()); w++) shown.push_back(w);
  else shown.push_back(window);
  const int offset = 0;   // window shown s is in the window of canvases s (the first one: the DatumGui window of the file)
  _gui.closePaneWindowsFrom(int(shown.size()) - offset);
  String hidden;
  for (std::size_t s=0; s<shown.size(); s++)
    {
    int w = shown[s], host = int(s) - offset;
    const GRAPHICS::PlotSpec::WindowSpec & ws = spec->windows()[std::size_t(w)];
    int n = int(ws.canvases.size());
    // exactly one pane per canvas: the last row holds the rest, no empty cell
    int count = std::max(1, std::min(36, n));
    int columns = std::max(1, std::min({ 6, ws.columns, count }));
    int rows = (count + columns - 1)/columns;
    if (n > 36) hidden += String(" ") + ws.title;
    if (host < 0)
      {
      if (_gui.nColumns() != columns || _gui.nRows() != rows || int(_gui.panesOf(-1).size()) != count) _gui.setPaneGrid(columns, count);
      title += String(": ") + ws.title;
      }
    else
      {
      _gui.setPaneWindow(host, host == 0 ? title + String(": ") + ws.title : ws.title, columns, rows, ws.width, ws.height);
      _gui.setPaneWindowGui(host, host == 0);
      _gui.setPaneWindowPaneCount(host, count);
      }
    std::vector<Path> canvasPaths = d.children(windows[std::size_t(w)], PlotDocument::kCanvas);
    std::vector<CanvasPane *> panes = _gui.panesOf(host);
    for (int k=0; k<int(panes.size()); k++)
      {
      CanvasPane & pane = *panes[std::size_t(k)];
      if (k >= n)
        {
        pane.canvas() = GRAPHICS::Canvas();
        pane.divide(1, 1);
        pane.setSelectedGraph(0);
        pane.setTitle("(empty cell)");
        Path windowPath = windows[std::size_t(w)];
        pane.onDrop = [this, windowPath](int, const ObjectReference & object) { drop(windowPath, object, false); };
        continue;
        }
      pane.canvas() = GRAPHICS::Canvas();   // nothing of the canvas drawn before stays (logarithmic axes, ranges, ...)
      spec->build(ws.canvases[std::size_t(k)], pane.canvas(), problems);
      pane.setTitle(ws.canvases[std::size_t(k)].title);
      if (pane.selectedGraph() >= pane.canvas().nGraphs()) pane.setSelectedGraph(0);
      PaneLink link;
      link.pane = &pane;
      link.canvas = k < int(canvasPaths.size()) ? canvasPaths[std::size_t(k)] : Path();
      // the Graph element of each cell, as PlotSpec::build() places them
      int next = 0;
      for (const Path & g : d.children(link.canvas, PlotDocument::kGraph))
        {
        const XmlElement * e = d.element(g);
        int cell = e->hasAttribute("cell") ? int(e->attributeInt("cell")) : next;
        next = cell + 1;
        link.graphs[cell] = g;
        }
      std::string key;
      for (int i : link.canvas) key += std::to_string(i) + ".";
      for (int g=0; g<pane.canvas().nGraphs(); g++)
        {
        auto found = shapes.find(key + "#" + std::to_string(g));
        if (found != shapes.end()) for (const auto & shape : found->second) pane.canvas().graph(g).addShape(shape);
        }
      link.drawn = spec->canvasXml(pane.canvas());
      link.drawnText = link.drawn.toString().str();
      Path canvasPath = link.canvas;
      int id = t->id;
      CanvasPane * shown = &pane;
      pane.onDrop = [this, id, canvasPath, shown](int graph, const ObjectReference & object)
        {
        if (!tab() || tab()->id != id) return;
        Path target = canvasPath;
        for (const auto & l : _links)
          if (l.canvas == canvasPath) { auto f = l.graphs.find(graph); if (f != l.graphs.end()) target = f->second; }
        dropOnPane(target, object, *shown, graph);
        };
      _links.push_back(link);
      }
    }
  if (!hidden.isNull()) _gui.setStatus(String("Only the first 36 canvases of a window are shown:") + hidden
                                       + String(" (Regroup can spread canvases over several windows)"));
  _spec = std::move(spec);   // the old specification goes after the canvases stopped using its functions
  if (!problems.isNull())
    {
    std::string first = problems.str().substr(0, problems.str().find('\n'));
    _gui.setStatus(String("Not drawn: ") + String(first));
    }
  // the selection follows on the canvases
  _seenPane = nullptr;
  _seenHasSelection = !t->hasSelection;
  }

  void DatumGuiApp::recordCanvases()
  {
  Tab * t = tab();
  if (!t || !_spec || _links.empty() || _drawnTab != t->id || _drawnVersion != t->document->version()) return;
  // the canvases as drawn, once drawn (the first frames complete their state)
  for (auto & link : _links)
    if (link.settle > 0 && --link.settle == 0 && paneIndexOf(link.pane) >= 0)
      {
      link.drawn = _spec->canvasXml(link.pane->canvas());
      link.drawnText = link.drawn.toString().str();
      }
  // not while the mouse is pressed or a widget is in use: once an edit on a canvas is done
  if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsAnyItemActive()) return;
  if (++_frame % 6 != 0) return;
  PlotDocument & d = *t->document;
  for (auto & link : _links)
    {
    if (link.settle > 0 || paneIndexOf(link.pane) < 0 || !d.element(link.canvas)) continue;
    XmlElement now = _spec->canvasXml(link.pane->canvas());
    std::string text = now.toString().str();
    if (text == link.drawnText) continue;
    XmlElement element = *d.element(link.canvas);
    if (PlotDocument::syncCanvas(element, link.drawn, now)) d.replace(link.canvas, element);
    link.drawn = now;
    link.drawnText = text;
    // the canvases already show it: not drawn again; the cells of the graphs may have new elements
    int next = 0;
    link.graphs.clear();
    for (const Path & g : d.children(link.canvas, PlotDocument::kGraph))
      {
      const XmlElement * e = d.element(g);
      int cell = e->hasAttribute("cell") ? int(e->attributeInt("cell")) : next;
      next = cell + 1;
      link.graphs[cell] = g;
      }
    _drawnVersion = d.version();
    }
  }

  void DatumGuiApp::syncSelection()
  {
  Tab * t = tab();
  if (!t || _gui.nPanes() == 0) return;
  CanvasPane & pane = _gui.currentPane();
  int graph = pane.selectedGraph();
  bool treeChanged = t->hasSelection != _seenHasSelection || (t->hasSelection && !(t->selected == _seenSelection));
  bool canvasChanged = &pane != _seenPane || graph != _seenGraph;
  if (treeChanged && t->hasSelection)
    {
    // the canvas of the selection made current, its graph selected
    for (const auto & link : _links)
      {
      if (t->selected.size() < link.canvas.size() || !std::equal(link.canvas.begin(), link.canvas.end(), t->selected.begin())) continue;
      int k = paneIndexOf(link.pane);
      if (k < 0) break;
      _gui.setCurrentPane(k, false);
      for (const auto & g : link.graphs)
        if (t->selected.size() >= g.second.size() && std::equal(g.second.begin(), g.second.end(), t->selected.begin()))
          link.pane->setSelectedGraph(g.first);
      break;
      }
    _seenPane = &_gui.currentPane();
    _seenGraph = _gui.currentPane().selectedGraph();
    }
  else if (canvasChanged && _seenPane)
    {
    // a graph clicked on a canvas: its Graph selected in the tree
    for (const auto & link : _links)
      if (link.pane == &pane)
        {
        auto found = link.graphs.find(graph);
        t->selected = found != link.graphs.end() ? found->second : link.canvas;
        t->hasSelection = true;
        }
    _seenPane = &pane;
    _seenGraph = graph;
    }
  else if (!_seenPane) { _seenPane = &pane; _seenGraph = graph; }
  _seenSelection = t->selected;
  _seenHasSelection = t->hasSelection;
  }

  // ------------------------------------------------------------------------------------------------ new from data

  void DatumGuiApp::startTemplate(const GRAPHICS::PlotTemplate & catalog, const GRAPHICS::PlotTemplate::Options & options,
                                const String & fileName)
  {
  _template = catalog;
  _templateOptions = options;
  _templateFile = fileName.str();
  _templateFilter.clear();
  _templateOpen = true;
  }

  bool DatumGuiApp::createFromTemplate()
  {
  if (_template.nSelected() == 0) { _gui.showMessage("Plot Template", "No histogram selected"); return false; }
  XmlDocument document = _template.makeSpec(_templateOptions);
  if (!_templateFile.empty())
    {
    String path(_templateFile.c_str());
    if (!FileSystem::hasExtension(path, ".xml")) path += String(".xml");
    try { document.write(path); }
    catch (const std::exception & e) { _gui.showMessage("Plot Template", path + String(": ") + String(e.what())); return false; }
    return openDocument(path);
    }
  int k = newDocument();
  PlotDocument & d = *_documents[std::size_t(k)].document;
  d.document() = document;
  // the inputs of the catalog are absolute or relative to the current folder: written absolute until saved
  for (XmlElement * e : d.document().root().children())
    if (e->isNamed("Input")) e->setAttribute("file", FileSystem::normalize(e->attribute("file", "")));
  d.setDirty(true);
  return true;
  }

  void DatumGuiApp::renderTemplate()
  {
  if (!_templateOpen) return;
  ImGui::SetNextWindowSize(ImVec2(820, 640), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Plot Template", &_templateOpen)) { ImGui::End(); return; }
  using Options = GRAPHICS::PlotTemplate::Options;
  ImGui::Text("Histograms: %d, tasks: %d, selected: %d", int(_template.entries().size()), int(_template.tasks().size()), _template.nSelected());
  ImGui::SetNextItemWidth(260);
  ImGui::InputTextWithHint("##filter", "filter the names (e.g. *R2*)", &_templateFilter);
  ImGui::SameLine();
  auto shown = [this](const String & name) { return _templateFilter.empty() || GRAPHICS::PlotTemplate::matches(name, String(_templateFilter.c_str())); };
  if (ImGui::Button("Select shown"))
    for (const String & t : _template.tasks()) for (const String & n : _template.names(t)) if (shown(n)) _template.setNameSelected(t, n, true);
  ImGui::SameLine();
  if (ImGui::Button("Unselect shown"))
    for (const String & t : _template.tasks()) for (const String & n : _template.names(t)) if (shown(n)) _template.setNameSelected(t, n, false);
  ImGui::Separator();
  float optionsHeight = 8.0f*ImGui::GetFrameHeightWithSpacing();
  ImGui::BeginChild("##catalog", ImVec2(0, -optionsHeight), ImGuiChildFlags_Borders);
  for (const String & task : _template.tasks())
    {
    String label = task.isNull() ? String("(top folder)") : task;
    ImGui::PushID(label.data());
    if (ImGui::TreeNodeEx(label.data(), ImGuiTreeNodeFlags_DefaultOpen))
      {
      if (ImGui::SmallButton("All")) _template.selectAll(task, true);
      ImGui::SameLine();
      if (ImGui::SmallButton("None")) _template.selectAll(task, false);
      if (ImGui::BeginTable("##lists", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
        {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Histograms");
        for (const String & name : _template.names(task))
          {
          if (!shown(name)) continue;
          bool v = _template.isNameSelected(task, name);
          String text = name + (_template.rankOf(task, name) == 2 ? String("  (2D)") : String(""));
          if (ImGui::Checkbox((text + String("##n")).data(), &v)) _template.setNameSelected(task, name, v);
          }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("Combinations of filters");
        for (const String & cb : _template.combinations(task))
          {
          bool v = _template.isCombinationSelected(task, cb);
          if (ImGui::Checkbox(((cb.isNull() ? String("(none)") : cb) + String("##c")).data(), &v)) _template.setCombinationSelected(task, cb, v);
          }
        ImGui::EndTable();
        }
      ImGui::TreePop();
      }
    ImGui::PopID();
    }
  ImGui::EndChild();
  int grouping = int(_templateOptions.grouping);
  ImGui::TextUnformatted("Canvases:");
  ImGui::SameLine();
  if (ImGui::RadioButton("one per family of related histograms (n1_*, R2_*, ...)", grouping == 2)) _templateOptions.grouping = Options::kByFamily;
  ImGui::SetItemTooltip("Histograms of a same rank whose names share their first word (before the first '_') share a canvas,\na graph per histogram and combination; the histograms alone in their family share the canvas 'others'");
  ImGui::TextUnformatted("         ");
  ImGui::SameLine();
  if (ImGui::RadioButton("one per histogram, a graph per combination", grouping == 0)) _templateOptions.grouping = Options::kByHistogram;
  ImGui::SameLine();
  if (ImGui::RadioButton("one per combination, a graph per histogram", grouping == 1)) _templateOptions.grouping = Options::kByCombination;
  ImGui::Checkbox("Overlay the combinations of 1D histograms in one graph", &_templateOptions.overlay);
  ImGui::SetNextItemWidth(120);
  ImGui::InputInt("graphs per canvas at most", &_templateOptions.maxGraphs);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::InputInt("canvases per window at most", &_templateOptions.maxCanvases);
  _templateOptions.maxGraphs = std::max(1, std::min(36, _templateOptions.maxGraphs));
  _templateOptions.maxCanvases = std::max(1, std::min(36, _templateOptions.maxCanvases));
  ImGui::SetNextItemWidth(420);
  ImGui::InputTextWithHint("plot file", "empty: a new, untitled plot file", &_templateFile);
  if (ImGui::Button(_templateFile.empty() ? "Create" : "Write and Open")) { if (createFromTemplate()) _templateOpen = false; }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) _templateOpen = false;
  ImGui::End();
  }

  } // namespace GUI
} // namespace Datum
