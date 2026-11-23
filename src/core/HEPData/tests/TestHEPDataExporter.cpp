/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestHEPDataExporter.cpp
//! @brief Test program of the task HEPDataExporter (run by ctest).
//
// TestHEPDataExporter -- the task made from an XML TASK element as bin/Datum makes it, run on Datum files written
// here: the tables listed (TABLE_1, TABLE_2, ...: names, metadata, a function over its range, an object chosen in one
// of two files), or all the objects matching OBJECTS and not EXCLUDE; the publication, the defaults of the tables, the
// archive, lossless and strict modes; the files read back into the same objects; PREVIEW_ONLY (nothing written);
// FAIL_ON_PROBLEMS; errors (a missing object, file, directory, a wrong MODE). Prints each failed check and exits
// with the number of failures (0: all passed).
//
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>
#include "HEPDataExporter.hpp"
#include "HEPDataConverter.hpp"
#include "DatumFile.hpp"
#include "XmlDocument.hpp"
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
bool throws(T f) { try { f(); } catch (const std::exception &) { return true; } return false; }

bool has(const std::string & text, const std::string & part) { return text.find(part)!=std::string::npos; }

HistogramSyst spectrum(const char * name, double scale)
{
  HistogramSyst h(name,"dN/dpT (1/GEV)",{0.0,1.0,2.0,4.0,8.0});
  h.setDimensionTitle(0,"pT (GEV)");
  for (unsigned int k=0; k<4; k++)
    {
    double v = scale*std::exp(-0.6*h.binCenter(k,0));
    h.setBinAtGlobal(k,1,v,0.05*v);
    h.setBinSystAtGlobal(k,0.08*v,0.06*v);
    }
  h.setCommonSyst(0.03);
  return h;
}

//! A Datum file: two spectra in a folder (the first fitted), points, a function.
void writeFile(const std::string & directory, const char * name, double scale)
{
  std::unique_ptr<DatumFile> f(DatumFile::openRecreateDatumFile(String(directory+"/"),String(name)));
  Folder * spectra = f->addFolder("spectra");
  HistogramSyst * h = spectra->add(new HistogramSyst(spectrum("pi0",scale)),"pi0");
  Linear line(1.0,-1.0);
  Fitter::fitAndAttach(line,*h);
  spectra->add(new HistogramSyst(spectrum("eta",0.5*scale)),"eta");
  DataPointsVector * v = new DataPointsVector();
  v->setName("ratio");
  v->setTitle("R");
  for (int k=0; k<5; k++) v->addPoint(DataPoint2D(0.5f+k,0.0f,1.0f+0.1f*k,0.02f,0.03f));
  f->add(v,"ratio");
  Exponential * e = new Exponential(80.0,-0.5);
  e->setName("model");
  f->add<ParameterizedFunction>(e,"model");
  f->write();
}

//! The task of a TASK element (as bin/Datum makes it), run. @return the task (to be deleted)
HEPDataExporter * run(const std::string & xml, bool execute=true)
{
  XmlDocument document = XmlDocument::parse(String(xml));
  const XmlElement * element = document.findFirst("TASK");
  Task creator;
  Task * task = creator.createTask(nullptr,*element);
  HEPDataExporter * exporter = dynamic_cast<HEPDataExporter *>(task);
  if (!exporter) { delete task; throw Exception("not a HEPDataExporter","TestHEPDataExporter"); }
  if (execute)
    {
    try { exporter->initialize(); exporter->execute(); exporter->finalize(); }
    catch (...) { delete exporter; throw; }
    }
  return exporter;
}

std::string parameter(const char * name, const std::string & value)
{
  return std::string("<PARAMETER name=\"")+name+"\" type=\"string\">"+value+"</PARAMETER>";
}

std::string task(const std::string & body)
{
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Datum version=\"1\">"
         "<TASK name=\"Export\" class=\"Datum::HEPDataExporter\" severity=\"WARNING\">"+body+"</TASK></Datum>";
}

std::string files(const std::string & work, const std::string & names, const std::string & out)
{
  return "<GROUP name=\"HISTOGRAM\"><GROUP name=\"IMPORT\">"+parameter("PATH",work)+parameter("FILE_NAME",names)+"</GROUP></GROUP>"
         "<GROUP name=\"HEPDATA\"><GROUP name=\"EXPORT\">"+parameter("PATH",work+"/"+out)+"</GROUP></GROUP>";
}

const std::string keywords =
  "<GROUP name=\"TABLES\">"+parameter("OBSERVABLES","DN/DPT")+parameter("REACTIONS","P P --> PI0 X")
  +parameter("CMENERGIES","13600")+parameter("QUALIFIERS","SQRT(S) = 13.6 [TEV]")+parameter("LICENSE","CC0")+"</GROUP>";

void testListed(const std::string & work)
{
  std::string xml = task(files(work,"one.datum two.datum","listed")
    +"<GROUP name=\"HEPDATA\"><GROUP name=\"EXPORT\">"+parameter("ARCHIVE","listed.tar.gz")+"</GROUP></GROUP>"
    +keywords
    +"<GROUP name=\"PUBLICATION\">"+parameter("COLLABORATION","ALICE")+parameter("INSPIRE","1234567")+"</GROUP>"
    +parameter("COMMENT","Spectra of neutral mesons.")
    +"<GROUP name=\"TABLE\">"+parameter("N","3")+"</GROUP>"
    +"<GROUP name=\"TABLE_1\">"+parameter("PATH","spectra/pi0")+parameter("NAME","Figure 1a")
      +parameter("DESCRIPTION","Neutral pions.")+parameter("LOCATION","Figure 1 (left)")+parameter("X_NAME","PT")
      +parameter("X_UNITS","GEV/c")+"</GROUP>"
    +"<GROUP name=\"TABLE_2\">"+parameter("PATH","spectra/eta")+parameter("FILE","two.datum")+parameter("NAME","Figure 1b")
      +parameter("OBSERVABLES","DN/DPT(ETA)")+"</GROUP>"
    +"<GROUP name=\"TABLE_3\">"+parameter("PATH","model")+parameter("LOW","0")+parameter("HIGH","8")+parameter("POINTS","17")+"</GROUP>");
  std::unique_ptr<HEPDataExporter> e(run(xml));
  const HEPDataSubmission & s = e->written();
  CHECK(s.tables.size()==4);                                     // pi0, its fit, eta, the function
  if (s.tables.size()!=4) return;
  CHECK(s.tables[0].name==String("Figure 1a") && s.tables[1].name==String("Figure 1a fit") && s.tables[2].name==String("Figure 1b"));
  CHECK(s.tables[3].name==String("model") && s.tables[3].nRows()==17 && s.tables[3].independent[0].values[16].value==8.0);
  CHECK(s.tables[0].description==String("Neutral pions.") && s.tables[0].location==String("Figure 1 (left)"));
  CHECK(s.tables[0].independent[0].name==String("PT") && s.tables[0].independent[0].units==String("GEV/c"));
  CHECK(s.tables[2].keyword("observables")[0]==String("DN/DPT(ETA)") && s.tables[0].keyword("observables")[0]==String("DN/DPT"));
  CHECK(s.tables[3].keyword("cmenergies")[0]==String("13600") && s.tables[0].licenseName==String("CC0"));
  CHECK(s.tables[0].dependent[0].qualifier("SQRT(S)")!=nullptr && s.problems().empty());
  CHECK(fs::exists(work+"/listed/submission.yaml") && fs::exists(work+"/listed/data4.yaml") && fs::exists(work+"/listed.tar.gz"));
  CHECK(e->preview().find("OK writes submission.yaml and 4 data files")!=nullptr);
  // read back: the objects of the files
  HEPDataSubmission r = HEPDataSubmission::read(String(work+"/listed"));
  CHECK(r.publication.collaboration==String("ALICE") && has(r.comment.str(),"Spectra of neutral mesons."));
  std::vector<C::Imported> pi0 = C::toObjects(r.tables[0]), eta = C::toObjects(r.tables[2]);
  HistogramSyst one = spectrum("pi0",100.0), two = spectrum("eta",0.5*300.0);
  CHECK(pi0.size()==1 && pi0[0].object->valueAt(1)==one.valueAt(1) && pi0[0].object->nFits()==1);
  CHECK(eta.size()==1 && eta[0].object->valueAt(2)==two.valueAt(2));   // the eta of two.datum, not of one.datum
  CHECK(C::toFunction(r.tables[3])!=nullptr);
}

void testAll(const std::string & work)
{
  // all the objects but the points, strict, without fits
  std::string xml = task(files(work,"one.datum","all")+keywords+parameter("MODE","STRICT")+parameter("FITS","false")
                         +parameter("EXCLUDE","rat*")
                         +"<GROUP name=\"FUNCTION\">"+parameter("HIGH","5")+parameter("POINTS","6")+"</GROUP>");
  std::unique_ptr<HEPDataExporter> e(run(xml));
  const HEPDataSubmission & s = e->written();
  CHECK(s.tables.size()==3);
  if (s.tables.size()==3)
    {
    CHECK(s.tables[0].name==String("pi0") && s.tables[1].name==String("eta") && s.tables[2].name==String("model"));
    CHECK(s.tables[0].datumBlock.isNull() && s.tables[2].nRows()==6 && s.tables[2].independent[0].values[5].value==5.0);
    }
  CHECK(e->preview().find("Strict")!=nullptr);
  // OBJECTS: only the spectra
  std::unique_ptr<HEPDataExporter> f(run(task(files(work,"one.datum","spectra")+keywords+parameter("OBJECTS","spectra/*"))));
  CHECK(f->written().tables.size()==3);                          // pi0, its fit, eta
}

void testOptions(const std::string & work)
{
  // PREVIEW_ONLY: nothing written
  std::unique_ptr<HEPDataExporter> p(run(task(files(work,"one.datum","preview")+keywords+parameter("PREVIEW_ONLY","true"))));
  CHECK(p->written().tables.empty() && !fs::exists(work+"/preview") && p->preview().count(HEPDataPreviewNode::kAction)==1);
  // without keywords: problems; FAIL_ON_PROBLEMS stops, otherwise written with warnings
  CHECK(throws([&]() { delete run(task(files(work,"one.datum","failed")+parameter("FAIL_ON_PROBLEMS","true"))); }));
  CHECK(!fs::exists(work+"/failed"));
  std::unique_ptr<HEPDataExporter> w(run(task(files(work,"one.datum","warned"))));
  CHECK(w->written().tables.size()==5 && !w->written().problems().empty());
  // errors
  CHECK(throws([&]() { delete run(task(files(work,"one.datum","x")+"<GROUP name=\"TABLE_1\">"+parameter("PATH","spectra/kaon")+"</GROUP>")); }));
  CHECK(throws([&]() { delete run(task(files(work,"one.datum","x")+"<GROUP name=\"TABLE_1\">"+parameter("PATH","spectra/pi0")
                                       +parameter("FILE","two.datum")+parameter("LOW","zero")+"</GROUP>")); }));
  CHECK(throws([&]() { delete run(task(files(work,"three.datum","x"))); }));
  CHECK(throws([&]() { delete run(task(files(work,"one.datum","x")+"<GROUP name=\"TABLE\">"+parameter("N","2")+"</GROUP>"
                                       +"<GROUP name=\"TABLE_1\">"+parameter("PATH","ratio")+"</GROUP>")); }));
  // TABLE:N 0: the tables listed until the first without PATH
  std::unique_ptr<HEPDataExporter> one(run(task(files(work,"one.datum","one")+keywords+"<GROUP name=\"TABLE_1\">"
                                                +parameter("PATH","ratio")+"</GROUP>")));
  CHECK(one->written().tables.size()==1 && one->written().tables[0].name==String("ratio"));
  CHECK(one->written().tables.size()==1 && one->written().tables[0].dependent[0].name==String("R"));   // the title, kept by the file
  CHECK(throws([&]() { delete run(task(files(work,"one.datum","x")+parameter("MODE","FAST"))); }));
  CHECK(throws([&]() { delete run(task("<GROUP name=\"HISTOGRAM\"><GROUP name=\"IMPORT\">"+parameter("PATH",work)
                                       +parameter("FILE_NAME","one.datum")+"</GROUP></GROUP>")); }));
  CHECK(throws([&]() { delete run(task(parameter("MODE","STRICT"))); }));
  // the defaults, as the template shows them
  std::unique_ptr<HEPDataExporter> d(run(task(""),false));
  const Configuration & c = d->configuration();
  CHECK(c.valueString("Export:MODE")==String("LOSSLESS") && c.valueString("Export:TABLE_1:PATH")==String("NONE"));
  CHECK(c.valueString("Export:OBJECTS")==String("*") && c.valueInt("Export:FUNCTION:POINTS")==100 && c.valueInt("Export:TABLE:N")==0);
}
}

int main()
{
  std::setvbuf(stdout,nullptr,_IONBF,0);
  std::string pattern = (fs::temp_directory_path()/"datum_testexporter_XXXXXX").string();
  std::vector<char> name(pattern.begin(),pattern.end());
  name.push_back('\0');
  if (!::mkdtemp(name.data())) { std::printf("FAILED: no temporary directory\n"); return 1; }
  std::string work(name.data());
  try
    {
    writeFile(work,"one.datum",100.0);
    writeFile(work,"two.datum",300.0);
    testListed(work);
    testAll(work);
    testOptions(work);
    }
  catch (const std::exception & e)
    {
    std::printf("FAILED: exception: %s\n", e.what());
    nFailures++;
    }
  std::error_code ignored;
  fs::remove_all(work,ignored);
  std::printf("TestHEPDataExporter: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
