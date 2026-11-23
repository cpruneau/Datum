/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file HEPDataDialog.cpp
//! @brief HEPDataDialog: the HEPData import and export windows of DatumGui, with their preview.
#include "HEPDataDialog.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include "FileDialog.hpp"
#include "Folder.hpp"
#include "DataPoints.hpp"
#include "ParameterizedFunction.hpp"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace fs = std::filesystem;

namespace Datum
{
  namespace GUI
  {

  namespace
  {
  const ImVec4 kActionColor (0.35f, 0.80f, 0.40f, 1.0f);
  const ImVec4 kWarningColor(0.95f, 0.78f, 0.25f, 1.0f);
  const ImVec4 kErrorColor  (0.95f, 0.35f, 0.30f, 1.0f);

  std::string labelsText(const std::vector<String> & labels)
  {
    std::string s;
    for (const String & l : labels) s += (s.empty() ? "" : ", ")+l.str();
    return s;
  }

  std::vector<String> labelsOf(const String & text)
  {
    std::vector<String> labels;
    std::string s = text.str(), part;
    for (std::size_t k=0; k<=s.size(); k++)
      {
      // labels are separated by commas, but "sys,common" is one label: a comma followed by a space separates
      if (k==s.size() || (s[k]==',' && k+1<s.size() && s[k+1]==' '))
        {
        std::size_t a = part.find_first_not_of(' '), b = part.find_last_not_of(' ');
        if (a!=std::string::npos) labels.push_back(String(part.substr(a,b-a+1)));
        part.clear();
        }
      else part += s[k];
      }
    return labels;
  }

  //! Help shown when the mouse is over the item drawn last.
  void hint(const char * text)
  {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text);
  }
  }

  HEPDataDialog::HEPDataDialog()
  {
  _commonLabels = String(labelsText(_importOptions.commonLabels));
  }

  void HEPDataDialog::say(const String & text, bool error)
  {
  _message = text;
  _error = error;
  }

  void HEPDataDialog::openImport(const String & path)
  {
  if (_mode!=kImport) { _previewOpen = false; _tree = HEPDataPreviewNode(); }
  _mode = kImport;
  if (_importPath.isNull()) _importPath = path;
  _open = true;
  _focus = true;
  _stale = true;
  say("", false);
  }

  void HEPDataDialog::openExport(const std::vector<ExportFile> & files, const String & directory)
  {
  if (_mode!=kExport) { _previewOpen = false; _tree = HEPDataPreviewNode(); }
  _mode = kExport;
  // files: those already offered keep their selection
  std::vector<ExportFile> merged = files;
  for (ExportFile & f : merged)
    for (const ExportFile & old : _files)
      if (old.folder==f.folder) f.selected = old.selected || f.selected;
  _files = merged;
  if (_exportPlan.directory.isNull() && !directory.isNull())
    _exportPlan.directory = String((fs::path(directory.str())/"HEPData").string());
  updateItems();
  _open = true;
  _focus = true;
  _stale = true;
  say("", false);
  }

  void HEPDataDialog::updateItems()
  {
  std::vector<HEPDataExportPlan::Item> items;
  for (const ExportFile & f : _files)
    {
    if (!f.selected || !f.folder) continue;
    for (HEPDataExportPlan::Item & item : HEPDataExportPlan::itemsOf(*f.folder,f.label,f.folder))
      {
      for (const HEPDataExportPlan::Item & old : _exportPlan.items)   // the settings of an item kept
        if (old.owner==item.owner && old.path==item.path && old.points==item.points && old.function==item.function)
          { item = old; break; }
      items.push_back(item);
      }
    }
  _exportPlan.items = items;
  if (_selected>=int(items.size())) _selected = -1;
  _stale = true;
  }

  bool HEPDataDialog::edit(const char * label, String & value, const char * hintText, float width)
  {
  std::string text = value.str();
  if (width>0.0f) ImGui::SetNextItemWidth(width);
  bool changed = hintText ? ImGui::InputTextWithHint(label,hintText,&text) : ImGui::InputText(label,&text);
  if (changed) { value = String(text); _stale = true; }
  return changed;
  }

  bool HEPDataDialog::preview()
  {
  say("", false);
  if (_mode==kImport)
    {
    if (_importPath.isNull()) { say("Choose the folder (or archive, or file) to read", true); return false; }
    std::error_code error;
    if (!fs::exists(fs::path(_importPath.str()),error)) { say(String("Not found: "+_importPath.str()), true); return false; }
    _importOptions.commonLabels = labelsOf(_commonLabels);
    _importPlan = HEPDataImportPlan();
    _importPlan.prepare(_importPath,_importOptions);
    _tree = _importPlan.preview(_rows,_importName);
    _count = _importPlan.nObjects();
    }
  else
    {
    // the archive: next to the directory, named after it
    _exportPlan.archive = String();
    if (_archive>0 && !_exportPlan.directory.isNull())
      {
      fs::path d(_exportPlan.directory.str());
      std::string name = _archiveName.isNull() ? d.filename().string() : _archiveName.str();
      for (const char * end : {".tar.gz",".tgz",".zip"})
        if (name.size()>std::string(end).size() && name.compare(name.size()-std::string(end).size(),std::string::npos,end)==0)
          name = name.substr(0,name.size()-std::string(end).size());
      _exportPlan.archive = String((d.parent_path()/(name+(_archive==1 ? ".tar.gz" : ".zip"))).string());
      }
    _tree = _exportPlan.preview(_rows);
    _count = unsigned(_exportPlan.build().tables.size());
    }
  _previewOpen = true;
  _previewFocus = true;
  _stale = false;
  _expand = 0;
  return canConfirm();
  }

  bool HEPDataDialog::canConfirm() const
  {
  if (!_previewOpen || _stale) return false;
  if (_mode==kImport) return _importPlan.nObjects()>0;
  return !_exportPlan.directory.isNull() && _tree.count(HEPDataPreviewNode::kAction)>0;
  }

  bool HEPDataDialog::confirm()
  {
  if (!canConfirm()) return false;
  if (_mode==kImport)
    {
    String name = _importName.isNull() ? _importPlan.name() : _importName;
    String message;
    bool ok = false;
    if (onImport) ok = onImport(name,_importPlan,message);
    else message = "Nothing to receive the objects";
    if (!ok) { say(message.isNull() ? String("The import failed") : message, true); return false; }
    if (onStatus) onStatus(message.isNull() ? String(name.str()+": "+std::to_string(_importPlan.nObjects())+" objects imported") : message);
    }
  else
    {
    try
      {
      HEPDataSubmission s = _exportPlan.write();
      String done = String(std::to_string(s.tables.size())+(s.tables.size()==1 ? " table" : " tables")+" written to "
                           +_exportPlan.directory.str()+(_exportPlan.archive.isNull() ? std::string() : " and "+_exportPlan.archive.str()));
      if (onStatus) onStatus(done);
      }
    catch (const std::exception & e)
      {
      say(String(std::string("Cannot write the submission: ")+e.what()), true);
      return false;
      }
    }
  close();
  return true;
  }

  void HEPDataDialog::cancel()
  {
  _previewOpen = false;
  say("Cancelled: nothing done", false);
  }

  void HEPDataDialog::close()
  {
  _open = false;
  _previewOpen = false;
  _importPlan = HEPDataImportPlan();     // the objects converted are dropped
  _tree = HEPDataPreviewNode();
  }

  // ---------------------------------------------------------------------------------------------- windows

  void HEPDataDialog::render(FileDialog & files)
  {
  if (_open)
    {
    ImGui::SetNextWindowSize(ImVec2(760, 640), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 300), ImVec2(1.0E5f, 1.0E5f));
    if (_focus) { ImGui::SetNextWindowFocus(); _focus = false; }
    std::string title = std::string(_mode==kImport ? "HEPData Import" : "HEPData Export")+"###HEPDataDialog";
    bool open = true;
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
      {
      // the settings scroll; the buttons stay at the bottom
      float footer = ImGui::GetFrameHeightWithSpacing() + (_message.isNull() ? 0.0f : ImGui::GetTextLineHeightWithSpacing())
                     + ImGui::GetStyle().ItemSpacing.y;
      if (ImGui::BeginChild("settings", ImVec2(0, -footer), ImGuiChildFlags_None))
        {
        if (_mode==kImport) renderImport(files); else renderExport(files);
        }
      ImGui::EndChild();
      if (!_message.isNull())
        {
        if (_error) ImGui::TextColored(kErrorColor, "%s", _message.data());
        else ImGui::TextDisabled("%s", _message.data());
        }
      if (ImGui::Button("Preview", ImVec2(110, 0))) preview();
      hint(_mode==kImport ? "Read the files and show what OK will make (nothing is added yet)"
                          : "Show the submission planned and the files to be written (nothing is written yet)");
      ImGui::SameLine();
      if (ImGui::Button("Close", ImVec2(110, 0))) open = false;
      if (_previewOpen && _stale) { ImGui::SameLine(); ImGui::TextColored(kWarningColor, "Changed since the preview"); }
      }
    ImGui::End();
    if (!open) close();
    }
  if (_previewOpen) renderPreview();
  }

  void HEPDataDialog::renderImport(FileDialog & files)
  {
  ImGui::SeparatorText("Read from");
  ImGui::TextWrapped("A folder holding a submission (submission.yaml and its data files), or a folder of such folders, "
                     "of archives (.tar.gz, .zip) and of YAML data files; or one archive or file.");
  float browse = ImGui::CalcTextSize("Folder...").x + ImGui::CalcTextSize("File...").x + 4*ImGui::GetStyle().FramePadding.x
                 + 2*ImGui::GetStyle().ItemSpacing.x;
  edit("##path", _importPath, "folder, archive or YAML file", std::max(200.0f, ImGui::GetContentRegionAvail().x - browse));
  ImGui::SameLine();
  if (ImGui::Button("Folder..."))
    {
    String start = _importPath.isNull() ? String() : _importPath;
    files.open(FileDialog::kOpenFolder, "Folder of the HEPData files to import", start, {}, "",
               [this](const String & path) { _importPath = path; _stale = true; });
    }
  ImGui::SameLine();
  if (ImGui::Button("File..."))
    files.open(FileDialog::kOpenFile, "HEPData archive or YAML file to import", _importPath, {".tar.gz", ".tgz", ".tar", ".zip", ".yaml", ".yml", ".json"}, "",
               [this](const String & path) { _importPath = path; _stale = true; });

  ImGui::SeparatorText("Data file made");
  edit("name", _importName, "the name of the folder", 360);
  hint("Name of the new data file shown in the browser (Save writes it as a Datum file)");

  ImGui::SeparatorText("Conversion");
  if (ImGui::Checkbox("Histograms from bins", &_importOptions.histograms)) _stale = true;
  hint("Tables whose independent variables are bins give histograms (HistogramSyst with systematic uncertainties); "
       "otherwise data points");
  if (ImGui::Checkbox("Points where values are given within the bins", &_importOptions.keepPositions)) _stale = true;
  hint("A histogram keeps only the bins: when a table gives positions within them (e.g. the mean pT), make data points instead");
  edit("common systematic labels", _commonLabels, nullptr, 360);
  hint("Labels of the uncertainties common to all the rows (e.g. normalization), separated by \", \": they become the "
       "common systematic uncertainty of the object; the other systematic uncertainties are added in quadrature");
  int rows = int(_rows);
  ImGui::SetNextItemWidth(200);
  if (ImGui::SliderInt("rows shown per table", &rows, 0, 50)) { _rows = unsigned(rows); _stale = true; }
  }

  void HEPDataDialog::renderExport(FileDialog & files)
  {
  HEPDataExportPlan & p = _exportPlan;
  // ---- the objects
  if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen))
    {
    if (_files.empty()) ImGui::TextDisabled("No data file is open: open one in the browser (File > Open) first.");
    else ImGui::TextUnformatted("Data files whose objects are offered:");
    bool changed = false;
    for (std::size_t k=0; k<_files.size(); k++)
      {
      ImGui::PushID(int(k));
      changed |= ImGui::Checkbox(_files[k].label.data(), &_files[k].selected);
      ImGui::PopID();
      }
    if (changed) updateItems();
    if (!p.items.empty())
      {
      if (ImGui::SmallButton("All")) { for (auto & i : p.items) i.include = true; _stale = true; }
      ImGui::SameLine();
      if (ImGui::SmallButton("None")) { for (auto & i : p.items) i.include = false; _stale = true; }
      ImGui::SameLine();
      ImGui::TextDisabled("%u of %u objects exported; click a row to edit its table", p.nIncluded(), unsigned(p.items.size()));
      ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
      float height = std::min(240.0f, ImGui::GetFrameHeightWithSpacing()*(float(p.items.size())+1.5f));
      if (ImGui::BeginTable("items", 4, flags, ImVec2(0, height)))
        {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Export", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Table", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Fits", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (std::size_t k=0; k<p.items.size(); k++)
          {
          HEPDataExportPlan::Item & item = p.items[k];
          ImGui::PushID(int(k));
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          if (ImGui::Checkbox("##include", &item.include)) _stale = true;
          ImGui::TableNextColumn();
          std::string what = item.source.str()+": "+item.path.str()+(item.function ? "  (function)" : "");
          if (ImGui::Selectable(what.c_str(), _selected==int(k), ImGuiSelectableFlags_None)) _selected = int(k);
          if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", (item.points ? HEPDataPreview::describe(*item.points) : HEPDataPreview::describe(*item.function)).data());
          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-1);
          edit("##table", item.tableName, item.path.data());
          ImGui::TableNextColumn();
          if (item.points && item.points->nFits()>0) { if (ImGui::Checkbox("##fits", &item.fits)) _stale = true; }
          else ImGui::TextDisabled("-");
          ImGui::PopID();
          }
        ImGui::EndTable();
        }
      if (_selected>=0 && _selected<int(p.items.size())) renderItemEditor(p.items[std::size_t(_selected)]);
      }
    }
  // ---- the files written
  if (ImGui::CollapsingHeader("Submission", ImGuiTreeNodeFlags_DefaultOpen))
    {
    float browse = ImGui::CalcTextSize("Browse...").x + 2*ImGui::GetStyle().FramePadding.x + ImGui::GetStyle().ItemSpacing.x;
    edit("##directory", p.directory, "directory to write submission.yaml and the data files to",
         std::max(200.0f, ImGui::GetContentRegionAvail().x - browse));
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
      files.open(FileDialog::kOpenFolder, "Directory of the HEPData submission", p.directory.isNull() ? String() : String(fs::path(p.directory.str()).parent_path().string()), {}, "",
                 [this](const String & path) { _exportPlan.directory = path; _stale = true; });
    ImGui::TextUnformatted("Archive:");
    for (int k=0; k<3; k++)
      {
      ImGui::SameLine();
      if (ImGui::RadioButton(k==0 ? "none" : (k==1 ? ".tar.gz" : ".zip"), _archive==k)) { _archive = k; _stale = true; }
      }
    if (_archive>0) { ImGui::SameLine(); edit("##archive", _archiveName, "named after the directory", 220); }
    if (ImGui::RadioButton("Lossless", p.lossless)) { p.lossless = true; _stale = true; }
    hint("The data files keep a datum block (class, binning, entries, fits): Datum reads the objects back exactly. "
         "hepdata.net ignores it.");
    ImGui::SameLine();
    if (ImGui::RadioButton("Strict (for hepdata.net)", !p.lossless)) { p.lossless = false; _stale = true; }
    hint("Plain HEPData files, as uploaded to hepdata.net");
    int digits = int(p.digits);
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderInt("significant digits (0: exact)", &digits, 0, 12)) { p.digits = unsigned(digits); _stale = true; }
    }
  // ---- the publication
  if (ImGui::CollapsingHeader("Publication and collaboration", ImGuiTreeNodeFlags_DefaultOpen))
    {
    ImGui::TextWrapped("hepdata.net takes the paper (title, authors, journal) from its INSPIRE record: these are kept "
                       "as comment lines at the top of submission.yaml, and can open the comment.");
    HEPDataPublication & pub = p.publication;
    float w = std::max(240.0f, ImGui::GetContentRegionAvail().x - 130.0f);
    edit("collaboration", pub.collaboration, "e.g. ALICE", w);
    edit("title", pub.title, "title of the paper", w);
    edit("INSPIRE", pub.inspire, "record number, e.g. 2912345", w);
    edit("arXiv", pub.arXiv, "e.g. 2511.01234", w);
    edit("DOI", pub.doi, "e.g. 10.1103/...", w);
    edit("journal", pub.journal, "journal reference", w);
    edit("contact", pub.contact, "name <e-mail>", w);
    if (ImGui::Checkbox("Begin the comment with the publication", &p.publicationInComment)) _stale = true;
    std::string comment = p.comment.str();
    if (ImGui::InputTextMultiline("comment", &comment, ImVec2(w, ImGui::GetTextLineHeight()*5)))
      { p.comment = String(comment); _stale = true; }
    hint("Information that applies to all the tables (the abstract of the data)");
    }
  // ---- the defaults of the tables
  if (ImGui::CollapsingHeader("All tables (unless set for a table)"))
    {
    HEPDataExportPlan::TableInfo & d = p.defaults;
    float w = std::max(240.0f, ImGui::GetContentRegionAvail().x - 130.0f);
    edit("observables", d.observables, "e.g. DSIG/DPT, E*D3SIG/DP**3", w);
    edit("reactions", d.reactions, "e.g. P P --> PI0 X", w);
    edit("cmenergies", d.cmenergies, "in GeV, e.g. 13600", w);
    edit("phrases", d.phrases, "e.g. Inclusive, Transverse Momentum", w);
    edit("qualifiers", d.qualifiers, "NAME = value [units]; ... e.g. SQRT(S) = 13.6 [TEV]", w);
    edit("location", d.location, "e.g. Figure 1", w);
    edit("licence", d.licenseName, "e.g. CC0 (none: that of hepdata.net)", w);
    edit("licence URL", d.licenseUrl, "e.g. https://creativecommons.org/publicdomain/zero/1.0/", w);
    }
  }

  void HEPDataDialog::renderItemEditor(HEPDataExportPlan::Item & item)
  {
  std::string title = "Table of "+item.source.str()+": "+item.path.str();
  ImGui::SeparatorText(title.c_str());
  ImGui::PushID("editor");
  HEPDataExportPlan::TableInfo & i = item.info;
  const HEPDataExportPlan::TableInfo & d = _exportPlan.defaults;
  float w = std::max(240.0f, ImGui::GetContentRegionAvail().x - 130.0f);
  auto orDefault = [](const String & value, const char * otherwise) { return value.isNull() ? std::string(otherwise) : "all tables: "+value.str(); };
  String title0 = item.points ? (item.points->title().isNull() ? item.points->name() : item.points->title()) : String();
  edit("description", i.description, (item.points ? "default: "+title0.str() : std::string("default: the function")).c_str(), w);
  edit("location", i.location, orDefault(d.location, "e.g. Figure 3a").c_str(), w);
  edit("observables", i.observables, orDefault(d.observables, "e.g. DSIG/DPT").c_str(), w);
  edit("reactions", i.reactions, orDefault(d.reactions, "e.g. P P --> PI0 X").c_str(), w);
  edit("cmenergies", i.cmenergies, orDefault(d.cmenergies, "in GeV").c_str(), w);
  edit("phrases", i.phrases, orDefault(d.phrases, "").c_str(), w);
  edit("qualifiers", i.qualifiers, orDefault(d.qualifiers, "NAME = value [units]; ...").c_str(), w);
  float half = std::max(120.0f, (w - ImGui::GetStyle().ItemSpacing.x)/2);
  edit("##xname", item.xName, "x name (default: axis title)", half);
  ImGui::SameLine();
  edit("x name, units##xunits", item.xUnits, "x units", half);
  edit("##yname", item.yName, "y name (default: title)", half);
  ImGui::SameLine();
  edit("y name, units##yunits", item.yUnits, "y units", half);
  if (item.function)
    {
    ImGui::SetNextItemWidth(half);
    if (ImGui::InputDouble("from", &item.low)) _stale = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);
    if (ImGui::InputDouble("to", &item.high)) _stale = true;
    int n = int(item.nPoints);
    ImGui::SetNextItemWidth(half);
    if (ImGui::InputInt("points", &n)) { item.nPoints = unsigned(std::max(2, n)); _stale = true; }
    }
  ImGui::PopID();
  }

  // ---------------------------------------------------------------------------------------------- the preview

  void HEPDataDialog::renderNode(const HEPDataPreviewNode & node, int & id)
  {
  ImGui::PushID(id++);
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
  bool leaf = node.children.empty();
  if (leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  else if (node.open) flags |= ImGuiTreeNodeFlags_DefaultOpen;
  if (!leaf && _expand!=0) ImGui::SetNextItemOpen(_expand>0);
  bool colored = node.kind!=HEPDataPreviewNode::kItem;
  if (colored)
    ImGui::PushStyleColor(ImGuiCol_Text, node.kind==HEPDataPreviewNode::kAction ? kActionColor
                                         : (node.kind==HEPDataPreviewNode::kWarning ? kWarningColor : kErrorColor));
  static const char * prefixes[] = { "", "\xE2\x86\x92 ", "warning: ", "error: " };   // the action: an arrow
  bool open = ImGui::TreeNodeEx("##node", flags, "%s%s", prefixes[node.kind], node.text.data());
  if (colored) ImGui::PopStyleColor();
  if (ImGui::BeginPopupContextItem())
    {
    if (ImGui::MenuItem("Copy this line")) ImGui::SetClipboardText(node.text.data());
    if (!leaf && ImGui::MenuItem("Copy this branch")) ImGui::SetClipboardText(node.toText().data());
    ImGui::EndPopup();
    }
  if (open && !leaf)
    {
    for (const HEPDataPreviewNode & c : node.children) renderNode(c, id);
    ImGui::TreePop();
    }
  ImGui::PopID();
  }

  void HEPDataDialog::renderPreview()
  {
  ImGui::SetNextWindowSize(ImVec2(820, 600), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(360, 240), ImVec2(1.0E5f, 1.0E5f));
  if (_previewFocus) { ImGui::SetNextWindowFocus(); _previewFocus = false; }
  bool open = true;
  std::string title = std::string(_mode==kImport ? "HEPData Import: Preview" : "HEPData Export: Preview")+"###HEPDataPreview";
  if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
    {
    unsigned int warnings = _tree.count(HEPDataPreviewNode::kWarning), errors = _tree.count(HEPDataPreviewNode::kError);
    ImGui::Text(_mode==kImport ? "%u objects to make" : "%u tables to write", _count);
    ImGui::SameLine();
    ImGui::TextColored(warnings ? kWarningColor : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%u warnings", warnings);
    ImGui::SameLine();
    ImGui::TextColored(errors ? kErrorColor : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%u errors", errors);
    ImGui::SameLine(0, 24);
    if (ImGui::SmallButton("Expand All")) _expand = 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("Collapse All")) _expand = -1;
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(_tree.toText().data());
    hint("Copy the whole preview as text (a list of bullets)");
    float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::BeginChild("tree", ImVec2(0, -footer), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
      {
      int id = 0;
      renderNode(_tree, id);
      _expand = 0;
      }
    ImGui::EndChild();
    bool can = canConfirm();
    if (!can) ImGui::BeginDisabled();
    if (ImGui::Button("OK", ImVec2(110, 0))) confirm();
    if (!can) ImGui::EndDisabled();
    hint(_mode==kImport ? "Make the data file with these objects" : "Write the files");
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) cancel();
    hint("Close the preview: nothing is done");
    ImGui::SameLine();
    if (_stale) ImGui::TextColored(kWarningColor, "Changed since this preview: Preview again");
    else if (!can) ImGui::TextColored(kErrorColor, "Nothing to do");
    else if (!_message.isNull() && _error) ImGui::TextColored(kErrorColor, "%s", _message.data());
    }
  ImGui::End();
  if (!open) cancel();
  }

  } // namespace GUI
} // namespace Datum
