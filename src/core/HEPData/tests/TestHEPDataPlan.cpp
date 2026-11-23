/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestHEPDataPlan.cpp
//! @brief Test program of the plans of HEPData imports and exports, and of their previews (run by ctest).
//
// TestHEPDataPlan <data directory>
//  - publication: comment lines at the top of submission.yaml, written and read back, other names accepted, notes kept;
//  - import: what scan() finds in a folder (a submission; sub-folders, archives and loose data files), the objects
//    made (the same as HEPDataConverter::toObjects()), the preview (one action per object), addTo() (one folder per
//    source when there are several, classes kept, functions), a broken file reported without stopping the others,
//    the real record of the data directory;
//  - export: the items of a folder, the submission built (tables of objects, fits and functions, names made unique,
//    keywords, qualifiers, licence, comment with the publication), the preview (files, warnings, errors), write()
//    and a read back, strict mode.
// Prints each failed check and exits with the number of failures (0: all passed).
//
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>
#include "HEPDataPlan.hpp"
#include "Folder.hpp"
#include "Histogram.hpp"
#include "HistogramSyst.hpp"
#include "DataPointsVector.hpp"
#include "Fitter.hpp"
#include "Linear.hpp"
#include "Exponential.hpp"
#include "Exceptions.hpp"

using namespace Datum;
using namespace Datum::MATH;
namespace fs = std::filesystem;
typedef HEPDataConverter C;
typedef HEPDataPreviewNode N;

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

template <class T>
bool throws(T f) { try { f(); } catch (const Exception &) { return true; } return false; }

bool has(const std::string & text, const std::string & part) { return text.find(part)!=std::string::npos; }

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
  h.setCommonSyst(0.03);
  return h;
}

//! A folder with a histogram (fitted), points, a function, and an empty histogram.
Folder objects()
{
  Folder f("objects");
  HistogramSyst * h = f.add(new HistogramSyst(spectrum("spectrum")),"spectrum");
  Linear line(1.0,-1.0);
  Fitter::fitAndAttach(line,*h);
  DataPointsVector * v = new DataPointsVector();
  v->setName("ratio");
  v->setTitle("R");
  for (int k=0; k<5; k++) v->addPoint(DataPoint2D(0.5f+k,0.0f,1.0f+0.1f*k,0.02f,0.03f));
  f.add(v,"ratio");
  Folder * sub = f.addFolder("models");
  Exponential * e = new Exponential(80.0,-0.5);
  e->setName("model");
  sub->add<ParameterizedFunction>(e,"model");
  f.add(new Histogram("empty","nothing",5,0.0,1.0),"empty");
  return f;
}

void testPublication(const std::string & work)
{
  HEPDataPublication p;
  p.collaboration = "ALICE";
  p.title = "Neutral pions in O-O and p-O collisions";
  p.inspire = "1234567";
  p.arXiv = "2511.00001";
  p.contact = "A. Author <a@cern.ch>";
  p.notes = {String("Datum test")};
  String h = p.headerLines();
  CHECK(has(h.str(),"# Collaboration: ALICE\n") && has(h.str(),"# INSPIRE: 1234567\n") && has(h.str(),"# Datum test\n"));
  HEPDataPublication q = HEPDataPublication::fromHeader(h);
  CHECK(q.collaboration==p.collaboration && q.title==p.title && q.inspire==p.inspire && q.arXiv==p.arXiv);
  CHECK(q.contact==p.contact && q.notes.size()==1 && q.notes[0]==String("Datum test") && q.doi.isNull());
  CHECK(p.summary()==String("ALICE Collaboration, Neutral pions in O-O and p-O collisions, arXiv:2511.00001 (INSPIRE 1234567)."));
  HEPDataPublication phenix = HEPDataPublication::fromHeader("# PPG999\n# InspireHEP: 99999\n---\n# not this\nname: x\n");
  CHECK(phenix.inspire==String("99999") && phenix.notes.size()==1 && phenix.notes[0]==String("PPG999"));
  CHECK(HEPDataPublication().isEmpty() && HEPDataPublication().summary().isNull());
  // through a submission
  HEPDataSubmission s;
  s.publication = p;
  s.comment = "A comment.";
  HEPDataTable t = C::toTable(spectrum("s"));
  t.name = "Spectrum";
  t.description = "A spectrum.";
  t.setKeyword("observables",{"DN/DPT"});
  s.addTable(t);
  s.write(String(work+"/publication"));
  HEPDataSubmission r = HEPDataSubmission::read(String(work+"/publication"));
  CHECK(r.publication.collaboration==String("ALICE") && r.publication.notes.size()==1 && r.comment==String("A comment."));
  CHECK(r.tables.size()==1 && r.problems().empty());
}

void testImport(const std::string & work, const std::string & data)
{
  // a folder holding a submission: that one only
  std::vector<HEPDataImportPlan::Source> one = HEPDataImportPlan::scan(String(work+"/publication"));
  CHECK(one.size()==1 && one[0].kind==String("submission") && one[0].folder==String("publication"));
  // a folder of things: a sub-folder with a submission, an archive, loose data files, a broken file, other files
  std::string mixed = work+"/mixed";
  fs::create_directories(mixed+"/first");
  fs::create_directories(mixed+"/notASubmission");
  fs::copy(work+"/publication",mixed+"/first",fs::copy_options::recursive);
  HEPDataSubmission second;
  HEPDataTable a = C::toTable(spectrum("a"));
  a.name = "A";
  HEPDataTable b = C::functionTable(Exponential(2.0,-1.0),0.0,5.0);
  b.name = "B";
  second.addTable(a);
  second.addTable(b);
  second.write(String(work+"/second"),String(mixed+"/second.tar.gz"));
  HEPDataTable loose = C::toTable(spectrum("loose"));
  loose.writeDataFile(String(mixed+"/loose.yaml"));
  { std::ofstream bad(mixed+"/broken.yaml"); bad << "independent_variables: [ {header: oops\n"; }
  { std::ofstream other(mixed+"/notes.txt"); other << "not read\n"; }
  std::vector<HEPDataImportPlan::Source> found = HEPDataImportPlan::scan(String(mixed));
  CHECK(found.size()==3);
  if (found.size()==3)
    {
    CHECK(found[0].kind==String("submission") && found[0].folder==String("first"));
    CHECK(found[1].kind==String("archive") && found[1].folder==String("second"));
    CHECK(found[2].kind==String("data files") && found[2].files.size()==2);
    }
  HEPDataImportPlan plan;
  unsigned int n = plan.prepare(String(mixed));
  CHECK(n==4 && plan.nObjects()==4 && plan.nErrors()==1 && plan.name()==String("mixed"));
  N preview = plan.preview();
  std::string text = preview.toText().str();
  CHECK(preview.children.size()==4 && preview.children[0].kind==N::kAction);
  CHECK(has(preview.children[0].text.str(),"4 objects in 3 folders"));
  CHECK(preview.count(N::kAction)==1+4+3);                    // summary, one per object, one folder line per source
  CHECK(preview.count(N::kError)==1 && has(text,"broken.yaml"));
  CHECK(preview.find("makes HistogramSyst \"a\"")!=nullptr && preview.find("makes Function Exponential")!=nullptr);
  CHECK(preview.find("Collaboration: ALICE")!=nullptr && preview.find("Datum block")!=nullptr);
  CHECK(has(text,"- => OK makes a data file \"mixed\"") && has(text,"  - \"first\" - submission: "));
  // OK: the objects added, in a folder per source; again: names made unique
  Folder top("HEPData");
  CHECK(plan.addTo(top)==4);
  CHECK(top.folder("first")!=nullptr && top.folder("second")!=nullptr && top.folder("mixed")!=nullptr);
  CHECK(top.get<HistogramSyst>("second/a")!=nullptr && top.get<ParameterizedFunction>("second/B")!=nullptr);
  const HistogramSyst * back = top.get<HistogramSyst>("second/a");
  HistogramSyst original = spectrum("a");
  CHECK(back && back->valueAt(2)==original.valueAt(2) && back->sysErrorPlusAt(1)==original.sysErrorPlusAt(1)
        && back->getCommonSyst()==original.getCommonSyst());
  CHECK(plan.addTo(top)==4 && top.folder("first_2")!=nullptr);
  // a single source: the objects directly in the folder
  HEPDataImportPlan single;
  single.prepare(String(work+"/second"));
  Folder alone("x");
  CHECK(single.addTo(alone)==2 && alone.get<Histogram>("a")!=nullptr && alone.folder("second")==nullptr);
  CHECK(!has(single.preview().toText().str(),"folders"));
  // nothing here
  HEPDataImportPlan none;
  fs::create_directories(work+"/empty");
  CHECK(none.prepare(String(work+"/empty"))==0 && none.preview().count(N::kError)==1);
  // the real record: as many objects as toObjects() gives
  HEPDataImportPlan alice;
  String record(data+"/ALICE_OO_pO_pi0");
  unsigned int nAlice = alice.prepare(record);
  CHECK(nAlice>0 && alice.nErrors()==0 && alice.sources().size()==1);
  CHECK(nAlice==C::toObjects(HEPDataSubmission::read(record)).size());
  N ap = alice.preview(2);
  CHECK(ap.count(N::kAction)==1+nAlice && ap.find("First 2 rows")!=nullptr);
}

void testExport(const std::string & work)
{
  auto folder = std::make_shared<Folder>(objects());
  std::vector<HEPDataExportPlan::Item> items = HEPDataExportPlan::itemsOf(*folder,"objects.datum",folder);
  CHECK(items.size()==3);                                       // the empty histogram is left out
  if (items.size()!=3) return;
  CHECK(items[0].points && items[0].tableName==String("spectrum") && items[1].tableName==String("ratio"));
  CHECK(items[2].function && items[2].path==String("models/model") && items[2].owner);
  // qualifiers
  std::vector<HEPDataQualifier> q = HEPDataExportPlan::parseQualifiers("SQRT(S)/NUCLEON = 5.36 [TEV]; CENTRALITY = 0-10 [%]; junk; RE = P P --> PI0 X");
  CHECK(q.size()==3 && q[0].name==String("SQRT(S)/NUCLEON") && q[0].value==String("5.36") && q[0].units==String("TEV"));
  CHECK(q[2].value==String("P P --> PI0 X") && q[2].units.isNull());

  HEPDataExportPlan plan;
  plan.publication.collaboration = "ALICE";
  plan.publication.inspire = "1234567";
  plan.comment = "Spectra.";
  plan.defaults.observables = "DN/DPT";
  plan.defaults.reactions = "P P --> PI0 X";
  plan.defaults.cmenergies = "13000";
  plan.defaults.qualifiers = "SQRT(S) = 13 [TEV]";
  plan.defaults.licenseName = "CC0";
  plan.items = items;
  plan.items[1].tableName = "spectrum";                         // the same name: made unique
  plan.items[1].info.observables = "R";
  plan.items[1].info.description = "Ratio.";
  plan.items[1].xName = "x";
  plan.items[1].xUnits = "GEV";
  plan.items[2].low = 0.0;
  plan.items[2].high = 8.0;
  plan.items[2].nPoints = 9;
  // no directory yet
  N p0 = plan.preview();
  CHECK(p0.count(N::kError)==1 && p0.find("Choose the directory")!=nullptr);
  CHECK(throws([&]() { plan.write(); }));
  plan.directory = String(work+"/export");
  plan.archive = String(work+"/export.tar.gz");
  std::vector<String> errors, origins;
  HEPDataSubmission s = plan.build(&errors,&origins);
  CHECK(errors.empty() && s.tables.size()==4 && origins.size()==4);
  if (s.tables.size()==4)
    {
    CHECK(s.tables[0].name==String("spectrum") && s.tables[1].name==String("spectrum fit") && s.tables[2].name==String("spectrum (2)"));
    CHECK(s.tables[3].name==String("model") && s.tables[3].nRows()==9);
    CHECK(s.tables[0].keyword("reactions").size()==1 && s.tables[2].keyword("observables")[0]==String("R"));
    CHECK(s.tables[2].description==String("Ratio.") && s.tables[0].description==String("dN/dpT (1/GEV)"));
    CHECK(s.tables[2].independent[0].name==String("x") && s.tables[2].independent[0].units==String("GEV"));
    CHECK(s.tables[0].dependent[0].qualifier("SQRT(S)")!=nullptr && s.tables[3].dependent[0].qualifier("SQRT(S)")!=nullptr);
    CHECK(s.tables[1].dependent[0].qualifier("CHI2/NDF")!=nullptr && has(s.tables[1].description.str(),"of table \"spectrum\""));
    CHECK(s.tables[0].licenseName==String("CC0") && s.tables[3].dataFile==String("data4.yaml"));
    CHECK(has(origins[1].str(),"From fit 0") && has(origins[3].str(),"models/model"));
    }
  CHECK(s.comment==String("ALICE Collaboration (INSPIRE 1234567).\n\nSpectra.") && s.problems().empty());
  std::vector<String> files = plan.files();
  CHECK(files.size()==7 && has(files[1].str(),"submission.yaml") && files[6]==plan.archive);
  N p1 = plan.preview(2);
  std::string text = p1.toText().str();
  CHECK(p1.count(N::kError)==0 && p1.count(N::kWarning)==0 && p1.children[0].kind==N::kAction);
  CHECK(has(p1.children[0].text.str(),"4 data files") && has(text,"export.tar.gz") && has(text,"Lossless"));
  CHECK(p1.find("From objects.datum: spectrum - HistogramSyst \"spectrum\"")!=nullptr && p1.find("1 fit")!=nullptr);
  CHECK(p1.find("Collaboration: ALICE")!=nullptr && p1.find("Qualifier SQRT(S) = 13 TEV")!=nullptr);
  CHECK(p1.find("Independent variable pT [GEV]: 4 bins from 0 to 8 (variable widths)")!=nullptr);
  CHECK(p1.find("First 2 rows")!=nullptr);
  // OK
  HEPDataSubmission written = plan.write();
  CHECK(written.tables.size()==4 && fs::exists(work+"/export/data4.yaml") && fs::exists(work+"/export.tar.gz"));
  HEPDataSubmission r = HEPDataSubmission::read(String(work+"/export"));
  CHECK(r.publication.collaboration==String("ALICE") && r.tables.size()==4 && r.problems().empty());
  std::vector<C::Imported> o = C::toObjects(r.tables[0]);
  CHECK(o.size()==1 && dynamic_cast<HistogramSyst *>(o[0].object.get())!=nullptr && o[0].object->nFits()==1);
  CHECK(C::toFunction(r.tables[3])!=nullptr && C::toFit(r.tables[1])!=nullptr);
  // again: the existing submission and archive are replaced (warnings)
  N p2 = plan.preview();
  CHECK(p2.count(N::kWarning)==2 && p2.find("already in")!=nullptr);
  // strict, without fits, an item left out, an item that fails
  plan.lossless = false;
  plan.archive = String();
  plan.directory = String(work+"/strict");
  plan.items[0].fits = false;
  plan.items[1].include = false;
  plan.items[2].high = 0.0;                                      // empty range: fails
  N p3 = plan.preview();
  CHECK(p3.count(N::kError)==1 && p3.find("Left out: objects.datum: models/model")!=nullptr);
  CHECK(has(p3.children[0].text.str(),"1 data file") && p3.find("Strict")!=nullptr && p3.find("Datum block")==nullptr);
  HEPDataSubmission strict = plan.write();
  CHECK(strict.tables.size()==1 && strict.tables[0].datumBlock.isNull());
  // nothing to write
  plan.items[0].include = false;                                 // the one left fails
  CHECK(plan.preview().find("None of the objects chosen can be written")!=nullptr && throws([&]() { plan.write(); }));
  plan.items[2].include = false;
  CHECK(plan.preview().find("No object chosen")!=nullptr && throws([&]() { plan.write(); }));
  // missing keywords: what hepdata.net would reject
  HEPDataExportPlan bare;
  bare.directory = String(work+"/bare");
  bare.items = items;
  N p4 = bare.preview();
  CHECK(p4.count(N::kWarning)>=4 && p4.find("hepdata.net would reject")!=nullptr && p4.count(N::kError)==0);
}
}

int main(int argc, char ** argv)
{
  std::setvbuf(stdout,nullptr,_IONBF,0);
  if (argc<2) { std::printf("usage: TestHEPDataPlan <data directory>\n"); return 1; }
  std::string data(argv[1]);
  std::string pattern = (fs::temp_directory_path()/"datum_testplan_XXXXXX").string();
  std::vector<char> name(pattern.begin(),pattern.end());
  name.push_back('\0');
  if (!::mkdtemp(name.data())) { std::printf("FAILED: no temporary directory\n"); return 1; }
  std::string work(name.data());
  try
    {
    testPublication(work);
    testImport(work,data);
    testExport(work);
    }
  catch (const std::exception & e)
    {
    std::printf("FAILED: exception: %s\n", e.what());
    nFailures++;
    }
  std::error_code ignored;
  fs::remove_all(work,ignored);
  std::printf("TestHEPDataPlan: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
