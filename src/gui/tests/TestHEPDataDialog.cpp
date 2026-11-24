/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestHEPDataDialog.cpp
//! @brief Test program of the HEPData windows of DatumGui, drawn without a screen (run by ctest).
//
// TestHEPDataDialog <data directory>
// Dear ImGui runs without a window or renderer: frames are made and drawn into nothing. The import and export windows
// and their preview are drawn, expanded, with the editor of a table open; Preview, OK and Cancel are called as the
// buttons would; no frame may show two widgets with the same identifier. Prints each failed check and exits with the
// number of failures (0: all passed).
//
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"
#include "HEPDataDialog.hpp"
#include "FileDialog.hpp"
#include "Folder.hpp"
#include "HistogramSyst.hpp"
#include "Fitter.hpp"
#include "Linear.hpp"
#include "Exponential.hpp"

using namespace Datum;
using namespace Datum::GUI;
using namespace Datum::MATH;
namespace fs = std::filesystem;

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

#define CHECK(c) check((c), #c, __LINE__)

//! Access to what the buttons and clicks set.
struct Dialog : public HEPDataDialog
{
  void expandAll() { _expand = 1; }
  void collapseAll() { _expand = -1; }
  void editItem(int k) { _selected = k; }
  void archive(int kind) { _archive = kind; _stale = true; }
};

int conflicts = 0;

//! A frame: the windows drawn (into nothing).
void frame(Dialog & dialog, FileDialog & files, int n=2)
{
  for (int k=0; k<n; k++)
    {
    ImGuiIO & io = ImGui::GetIO();
    io.DeltaTime = 1.0f/60.0f;
    ImGui::NewFrame();
    dialog.render(files);
    files.render();
    ImGui::Render();
    if (ImGui::GetCurrentContext()->DebugDrawIdConflictsId!=0) conflicts++;
    }
}

HistogramSyst spectrum(const char * name)
{
  HistogramSyst h(name,"dN/dpT (1/GEV)",{0.0,1.0,2.0,4.0,8.0});
  h.setDimensionTitle(0,"pT (GEV)");
  for (unsigned int k=0; k<4; k++)
    {
    double v = 100.0*std::exp(-0.6*h.binCenter(k,0));
    h.setBinAtGlobal(k,1,v,0.05*v);
    h.setBinSystAtGlobal(k,0.08*v,0.06*v);
    }
  return h;
}

void testImport(Dialog & d, FileDialog & files, const std::string & data, const std::string & work)
{
  d.openImport(String(data+"/ALICE_OO_pO_pi0"));
  frame(d,files);
  CHECK(d.isOpen() && d.mode()==HEPDataDialog::kImport && !d.previewShown() && !d.canConfirm());
  d.openHelp();                                                  // the help window, all its sections drawn
  frame(d,files,3);
  CHECK(d.helpShown());
  CHECK(d.preview() && d.previewShown() && d.canConfirm());
  CHECK(d.importPlan().nObjects()==8 && d.previewTree().count(HEPDataPreviewNode::kAction)==9);
  frame(d,files);
  d.expandAll();
  frame(d,files);
  d.collapseAll();
  frame(d,files);
  // a change: OK waits for Preview again
  d.importName() = "pi0";
  d.setStale();
  frame(d,files);
  CHECK(!d.canConfirm() && !d.confirm());
  d.preview();
  CHECK(d.previewTree().find("data file \"pi0\" with 8 objects")!=nullptr);
  // Cancel: nothing made, the window stays
  int calls = 0;
  Folder made("made");
  d.onImport = [&](const String & name, const HEPDataImportPlan & plan, String & message)
    {
    calls++;
    made.setName(name);
    plan.addTo(made);
    message = String(name.str()+": imported");
    return true;
    };
  String status;
  d.onStatus = [&](const String & s) { status = s; };
  d.cancel();
  frame(d,files);
  CHECK(calls==0 && !d.previewShown() && d.isOpen());
  // OK
  d.preview();
  CHECK(d.confirm() && calls==1 && made.size()==8 && made.name()==String("pi0") && status==String("pi0: imported"));
  CHECK(!d.isOpen());
  frame(d,files);
  // a folder without anything to import: no OK
  fs::create_directories(work+"/nothing");
  d.openImport(String());
  d.importPath() = String(work+"/nothing");
  d.preview();
  frame(d,files);
  CHECK(d.previewShown() && !d.canConfirm() && d.previewTree().count(HEPDataPreviewNode::kError)==1);
  d.importPath() = String(work+"/absent");
  CHECK(!d.preview() && d.message().str().find("Not found")==0);
  frame(d,files);
  d.close();
}

void testExport(Dialog & d, FileDialog & files, const std::string & work)
{
  auto folder = std::make_shared<Folder>("objects");
  HistogramSyst * h = folder->add(new HistogramSyst(spectrum("spectrum")),"spectrum");
  Linear line(1.0,-1.0);
  Fitter::fitAndAttach(line,*h);
  Exponential * e = new Exponential(80.0,-0.5);
  e->setName("model");
  folder->addFolder("models")->add<ParameterizedFunction>(e,"model");
  auto other = std::make_shared<Folder>("other");
  other->add(new HistogramSyst(spectrum("another")),"another");

  d.openExport({ {String("objects.datum"),folder,true}, {String("other.datum"),other,false} },String(work));
  frame(d,files);
  CHECK(d.isOpen() && d.mode()==HEPDataDialog::kExport && d.exportPlan().items.size()==2);
  CHECK(d.exportPlan().directory==String(work+"/HEPData"));
  // the second file offered: its objects added, the settings of the others kept
  d.exportPlan().items[0].tableName = "Spectrum";
  d.exportFiles()[1].selected = true;
  d.updateItems();
  CHECK(d.exportPlan().items.size()==3 && d.exportPlan().items[0].tableName==String("Spectrum"));
  d.editItem(0);
  frame(d,files);
  d.editItem(1);                                                 // the function: its range
  d.exportPlan().items[1].high = 6.0;
  frame(d,files);
  HEPDataExportPlan & p = d.exportPlan();
  p.publication.collaboration = "ALICE";
  p.defaults.observables = "DN/DPT";
  p.defaults.reactions = "P P --> PI0 X";
  p.defaults.cmenergies = "13000";
  p.comment = "Spectra.";
  d.archive(1);
  d.setStale();
  CHECK(d.preview() && d.canConfirm());
  CHECK(d.previewTree().find("export.tar.gz")==nullptr && d.previewTree().find("HEPData.tar.gz")!=nullptr);
  d.expandAll();
  frame(d,files,3);
  String status;
  d.onStatus = [&](const String & s) { status = s; };
  CHECK(d.confirm() && !d.isOpen());
  CHECK(status.str().find("4 tables written")==0 && fs::exists(work+"/HEPData/submission.yaml") && fs::exists(work+"/HEPData.tar.gz"));
  HEPDataSubmission r = HEPDataSubmission::read(String(work+"/HEPData"));
  CHECK(r.tables.size()==4 && r.tables[0].name==String("Spectrum") && r.publication.collaboration==String("ALICE"));
  // opened again: the files selected, the settings and the tables kept; the submission there: a warning
  d.openExport({ {String("objects.datum"),folder,false} },String(work));
  CHECK(d.exportFiles()[0].selected && d.exportPlan().publication.collaboration==String("ALICE"));
  CHECK(d.exportPlan().items.size()==2 && d.exportPlan().items[0].tableName==String("Spectrum"));
  d.preview();
  frame(d,files);
  CHECK(d.previewTree().count(HEPDataPreviewNode::kWarning)>=1 && d.previewTree().find("already in")!=nullptr);
  d.close();
  frame(d,files);
}
}

int main(int argc, char ** argv)
{
  std::setvbuf(stdout,nullptr,_IONBF,0);
  if (argc<2) { std::printf("usage: TestHEPDataDialog <data directory>\n"); return 1; }
  std::string pattern = (fs::temp_directory_path()/"datum_testdialog_XXXXXX").string();
  std::vector<char> name(pattern.begin(),pattern.end());
  name.push_back('\0');
  if (!::mkdtemp(name.data())) { std::printf("FAILED: no temporary directory\n"); return 1; }
  std::string work(name.data());

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO & io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(1400, 1000);
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;   // textures requested, never made: nothing is drawn
  try
    {
    Dialog dialog;
    FileDialog files;
    testImport(dialog,files,argv[1],work);
    testExport(dialog,files,work);
    }
  catch (const std::exception & e)
    {
    std::printf("FAILED: exception: %s\n", e.what());
    nFailures++;
    }
  CHECK(conflicts==0);
  ImGui::DestroyContext();
  std::error_code ignored;
  fs::remove_all(work,ignored);
  std::printf("TestHEPDataDialog: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
