/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestHEPData.cpp
//! @brief Test program of the HEPData data model (HEPDataSubmission, HEPDataTable) and its YAML files (run by ctest).
//
// TestHEPData <data directory> -- reads a HEPData record of ALICE (neutral pions in pp, pO and OO collisions) and the
// HEPData sample submission (tests/data), checks their content against the files; the sizes of the errors (symmetric, asymmetric, one-sided,
// percentages); writing and reading back (directory, .tar.gz archive) with identical content; a single data file, the
// single-YAML-file format, JSON, the datum block, significant digits, non-numeric values and infinite edges; the
// problems found by problems(); invalid files. Prints each failed check and exits with the number of failures.
//
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unistd.h>
#include <vector>
#include "HEPDataSubmission.hpp"
#include "Exceptions.hpp"

using namespace Datum;
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

template <class T>
bool throws(T f) { try { f(); } catch (const Exception &) { return true; } return false; }

bool sameValue(const HEPDataValue & a, const HEPDataValue & b)
{
  if (a.hasValue!=b.hasValue || a.numeric!=b.numeric || a.hasLow!=b.hasLow || a.hasHigh!=b.hasHigh) return false;
  if (a.hasValue && a.numeric && a.value!=b.value) return false;
  if (a.hasValue && !a.numeric && !(a.text==b.text)) return false;
  if (a.hasLow && a.low!=b.low) return false;
  if (a.hasHigh && a.high!=b.high) return false;
  if (a.errors.size()!=b.errors.size()) return false;
  for (std::size_t k=0; k<a.errors.size(); k++)
    {
    const HEPDataError & e = a.errors[k], & f = b.errors[k];
    if (e.kind!=f.kind || !(e.label==f.label) || e.percent!=f.percent || e.plusMissing!=f.plusMissing || e.minusMissing!=f.minusMissing) return false;
    if (!e.plusMissing && e.plus!=f.plus) return false;
    if (e.kind==HEPDataError::kAsymmetric && !e.minusMissing && e.minus!=f.minus) return false;
    }
  return true;
}

bool sameVariable(const HEPDataVariable & a, const HEPDataVariable & b)
{
  if (!(a.name==b.name) || !(a.units==b.units) || a.qualifiers.size()!=b.qualifiers.size() || a.values.size()!=b.values.size()) return false;
  for (std::size_t k=0; k<a.qualifiers.size(); k++)
    if (!(a.qualifiers[k].name==b.qualifiers[k].name) || !(a.qualifiers[k].value==b.qualifiers[k].value) || !(a.qualifiers[k].units==b.qualifiers[k].units)) return false;
  for (std::size_t k=0; k<a.values.size(); k++) if (!sameValue(a.values[k],b.values[k])) return false;
  return true;
}

bool sameTable(const HEPDataTable & a, const HEPDataTable & b, bool metadata=true)
{
  if (a.independent.size()!=b.independent.size() || a.dependent.size()!=b.dependent.size()) return false;
  for (std::size_t k=0; k<a.independent.size(); k++) if (!sameVariable(a.independent[k],b.independent[k])) return false;
  for (std::size_t k=0; k<a.dependent.size(); k++)   if (!sameVariable(a.dependent[k],b.dependent[k])) return false;
  if (!metadata) return true;
  if (!(a.name==b.name) || !(a.description==b.description) || !(a.location==b.location) || !(a.dataFile==b.dataFile)) return false;
  if (!(a.licenseName==b.licenseName) || !(a.licenseUrl==b.licenseUrl) || a.resources.size()!=b.resources.size()) return false;
  if (a.keywords.size()!=b.keywords.size()) return false;
  for (std::size_t k=0; k<a.keywords.size(); k++)
    if (!(a.keywords[k].name==b.keywords[k].name) || a.keywords[k].values!=b.keywords[k].values) return false;
  return true;
}

bool sameSubmission(const HEPDataSubmission & a, const HEPDataSubmission & b)
{
  if (a.tables.size()!=b.tables.size() || !(a.comment==b.comment) || a.resources.size()!=b.resources.size()) return false;
  for (std::size_t k=0; k<a.tables.size(); k++) if (!sameTable(a.tables[k],b.tables[k])) return false;
  return true;
}

std::string temporaryDirectory()
{
  std::string pattern = (fs::temp_directory_path()/"datum_testhepdata_XXXXXX").string();
  std::vector<char> name(pattern.begin(),pattern.end());
  name.push_back('\0');
  if (!::mkdtemp(name.data())) return std::string();
  return std::string(name.data());
}

void testErrors()
{
  HEPDataError s = HEPDataError::symmetric(0.5,"stat");
  CHECK(s.upper(10.0)==0.5 && s.lower(10.0)==0.5 && s.isStatistical());
  HEPDataError n = HEPDataError::symmetric(-0.5,"sys");   // a negative symmetric error: the same size
  CHECK(n.upper(10.0)==0.5 && n.lower(10.0)==0.5 && !n.isStatistical());
  HEPDataError a = HEPDataError::asymmetric(3.3,-3.0,"stat");
  CHECK(a.upper(25.4)==3.3 && a.lower(25.4)==3.0);
  HEPDataError up = HEPDataError::asymmetric(0.2,0.1);      // both shifts upward
  CHECK(up.upper(1.0)==0.2 && up.lower(1.0)==0.0);
  HEPDataError swapped = HEPDataError::asymmetric(-0.1,0.3); // signs swapped
  CHECK(swapped.upper(1.0)==0.3 && swapped.lower(1.0)==0.1);
  HEPDataError p = HEPDataError::symmetric(2.0,"sys",true);
  CHECK(std::fabs(p.upper(-50.0)-1.0)<1e-15);
  HEPDataError one = HEPDataError::asymmetric(0.4,0.0);
  one.minusMissing = true;
  CHECK(one.upper(1.0)==0.4 && one.lower(1.0)==0.0);
  CHECK(HEPDataError::symmetric(1.0,"stat,uncorrelated").isStatistical());
  CHECK(!HEPDataError::symmetric(1.0,"").isStatistical());
}

void testAlice(const std::string & data, const std::string & work)
{
  std::string record = data + "/ALICE_OO_pO_pi0";
  HEPDataSubmission s = HEPDataSubmission::read(String(record));
  CHECK(s.tables.size()==8);
  CHECK(s.comment.str().find("Ultra-relativistic heavy-ion collisions")==0);
  CHECK(s.comment.str().find("\n")!=std::string::npos);                 // a literal block: newlines kept
  CHECK(s.resources.empty());
  const HEPDataTable * t = s.table("$\\pi^{0}$ production in pp at 5.36 TeV");
  CHECK(t!=nullptr);
  if (!t) return;
  CHECK(&s.tables[0]==t && t->dataFile=="data1.yaml");
  CHECK(t->location=="Data from Figure 1 (yellow markers).");
  CHECK(t->description.str().find("Differential production cross section of neutral pions in pp")==0);
  CHECK(t->keyword("cmenergies").size()==1 && t->keyword("cmenergies")[0]=="5360");
  CHECK(t->keyword("observables").size()==1 && t->keyword("observables")[0]=="E*D3SIG/DP**3");
  CHECK(t->licenseName.isNull() && t->resources.empty());
  CHECK(t->nRows()==27 && t->independent.size()==1 && t->dependent.size()==1);
  // pT: bins of different widths, each with both edges and a value
  const HEPDataVariable & x = t->independent[0];
  CHECK(x.name=="PT" && x.units=="GEV/c" && x.isBinned());
  CHECK(x.values[0].low==1.2 && x.values[0].high==1.4 && x.values[0].isNumeric() && x.values[0].value==1.2958);
  CHECK(x.values[0].center()==1.2958);                                    // the value given, not the middle
  CHECK(x.values[17].low==3.8 && x.values[17].high==4.2);
  CHECK(x.values[26].low==15.0 && x.values[26].high==20.0 && x.values[26].value==17.1451);
  // the cross section: numbers written as quoted strings, three labelled errors
  const HEPDataVariable & y = t->dependent[0];
  CHECK(y.name=="E d**3sigma/dp**3" && y.units=="b GEV**-2 c**3");
  CHECK(y.qualifiers.size()==3);
  CHECK(y.qualifier("REACTION")!=nullptr && y.qualifier("REACTION")->value=="P P --> PI0 X");
  CHECK(y.qualifier("YRAP(C=CM)")!=nullptr && y.qualifier("YRAP(C=CM)")->value=="-0.8 < y < 0.8");
  CHECK(y.qualifier("SQRT(S)")!=nullptr && y.qualifier("SQRT(S)")->value=="5360" && y.qualifier("SQRT(S)")->units=="GEV");
  const HEPDataValue & v = y.values[0];
  CHECK(v.isNumeric() && v.value==1.4166e-03 && v.errors.size()==3);
  CHECK(v.errors[0].label=="stat" && v.errors[0].isStatistical() && v.errors[0].kind==HEPDataError::kSymmetric);
  CHECK(v.errors[0].plus==1.17e-05 && v.errors[0].upper(v.value)==1.17e-05 && v.errors[0].lower(v.value)==1.17e-05);
  CHECK(v.errors[1].label=="sys" && v.errors[1].plus==7.07e-05);
  CHECK(v.errors[2].label=="norm" && v.errors[2].plus==3.68e-05 && !v.errors[2].percent);
  CHECK(y.values[26].value==8.2107e-10 && y.values[26].errors[2].plus==2.13e-11);
  std::vector<String> labels = y.errorLabels();
  CHECK(labels.size()==3 && labels[0]=="stat" && labels[1]=="sys" && labels[2]=="norm");
  // the other tables
  const HEPDataTable & pO = s.tables[3];
  CHECK(pO.dependent[0].qualifier("REACTION")->value=="P O --> PI0 X" && pO.dependent[0].values[0].value==2.8837e-02);
  const HEPDataTable & rOO = s.tables[5];
  CHECK(rOO.name=="$\\pi^{0}$ $R_{\\rm OO}$ at 5.36 TeV");
  CHECK(rOO.dependent[0].units.isNull() && rOO.dependent[0].values[0].value==7.4009e-01);
  CHECK(rOO.independent[0].values[0].value==1.3);
  CHECK(rOO.keywords.size()==4 && rOO.keyword("reactions").size()==1 && rOO.keyword("reactions")[0]=="O O --> PI0 X");
  CHECK(rOO.keywords[2].name=="reactions" && rOO.keywords[2].values[0]=="P P --> PI0 X");   // a repeated name is kept
  const HEPDataTable & ratio = s.tables[7];
  CHECK(ratio.keywords.size()==6 && ratio.dependent[0].qualifiers.size()==2);
  CHECK(ratio.dependent[0].qualifier("SQRT(S)")==nullptr);
  CHECK(ratio.dependent[0].values[26].value==8.6787e-01 && ratio.dependent[0].values[26].errors[0].plus==1.70e-01);
  for (const HEPDataTable & u : s.tables) CHECK(u.nRows()==27 && u.dependent[0].size()==27);
  for (const String & p : s.problems()) std::printf("  ALICE: %s\n", p.c_str());
  CHECK(s.problems().empty());

  // a single data file: one table named after the file, no metadata
  HEPDataSubmission one = HEPDataSubmission::read(String(record+"/data1.yaml"));
  CHECK(one.tables.size()==1 && one.tables[0].name=="data1" && one.tables[0].description.isNull());
  CHECK(sameTable(one.tables[0],*t,false));
  // its submission.yaml file
  CHECK(sameSubmission(HEPDataSubmission::read(String(record+"/submission.yaml")),s));
  // written (numbers as plain numbers) and read back: identical; then through a .tar.gz archive
  s.write(String(work+"/alice"),String(work+"/alice.tar.gz"));
  CHECK(sameSubmission(HEPDataSubmission::read(String(work+"/alice")),s));
  CHECK(sameSubmission(HEPDataSubmission::read(String(work+"/alice.tar.gz")),s));
  std::string written = s.tables[0].dataYaml().str();
  CHECK(written.find("value: 0.0014166")!=std::string::npos);
  CHECK(written.find("{value: 1.2958, low: 1.2, high: 1.4}")!=std::string::npos);
  CHECK(written.find("{symerror: 1.17e-05, label: stat}")!=std::string::npos);
  CHECK(written.find("{value: 8.888, low: 8, high: 10}")!=std::string::npos);     // 10, not 1e+01
  CHECK(written.find("{value: 17.1451, low: 15, high: 20}")!=std::string::npos);
  CHECK(written.find("e+")==std::string::npos);
}

void testSample(const std::string & data, const std::string & work)
{
  HEPDataSubmission s = HEPDataSubmission::read(String(data+"/TestHEPSubmission"));
  CHECK(s.tables.size()==8);
  CHECK(s.resources.size()==1 && s.resources[0].description=="web page with auxiliary material");
  CHECK(s.comment.str().find("\n")!=std::string::npos);                 // a literal block: newlines kept
  const HEPDataTable * t = s.table("Table 1");
  CHECK(t!=nullptr);
  if (!t) return;
  CHECK(t->location=="Data from Page 17 of preprint");
  CHECK(t->licenseName=="GPL 2" && t->licenseDescription=="Tell me about it");
  CHECK(t->resources.size()==3 && t->resources[2].licenseName=="GPL 2");
  CHECK(t->keyword("cmenergies").size()==1 && t->keyword("cmenergies")[0]=="7000.0");
  CHECK(t->independent.empty() && t->dependent.size()==3 && t->nRows()==1);
  const HEPDataVariable & y = t->dependent[0];
  CHECK(y.units=="FB" && y.qualifier("SQRT(S)")!=nullptr && y.qualifier("SQRT(S)")->units=="GEV");
  CHECK(y.qualifier("SQRT(S)")->value=="7000");
  const HEPDataValue & v = y.values[0];
  CHECK(v.value==25.4 && v.errors.size()==3);
  CHECK(v.errors[0].kind==HEPDataError::kAsymmetric && v.errors[0].isStatistical());
  CHECK(v.errors[0].upper(v.value)==3.3 && v.errors[0].lower(v.value)==3.0);
  CHECK(v.errors[1].label=="sys" && v.errors[1].plus==1.2 && v.errors[1].minus==-1.0);
  CHECK(v.errors[2].kind==HEPDataError::kSymmetric && v.errors[2].label=="sys,lumi" && v.errors[2].plus==1.0);
  CHECK(y.errorLabels().size()==3);
  const HEPDataTable * t3 = s.table("Table 3");
  CHECK(t3!=nullptr && t3->independent.size()==1 && t3->independent[0].values[3].high==600.0);
  CHECK(t3 && t3->dependent[0].values[0].value==7000.0 && t3->dependent[0].values[0].errors[1].label=="sys,detector");

  // written and read back: identical
  std::string out = work + "/sample";
  s.write(String(out));
  HEPDataSubmission back = HEPDataSubmission::read(String(out));
  CHECK(sameSubmission(back,s));
  // through a .tar.gz archive
  std::string archive = work + "/sample.tar.gz";
  s.write(String(work+"/sample2"),String(archive));
  CHECK(fs::is_regular_file(archive));
  HEPDataSubmission fromArchive = HEPDataSubmission::read(String(archive));
  CHECK(sameSubmission(fromArchive,s));
  // an archive with a top directory, as hepdata.net makes them
  std::string top = work + "/top";
  fs::create_directories(top);
  s.write(String(top+"/HEPData-ins1203852-v1-yaml"));
  std::string archive2 = work + "/HEPData-ins1203852-v1-yaml.tar.gz";
  std::string command = "tar -czf '" + archive2 + "' -C '" + top + "' HEPData-ins1203852-v1-yaml";
  CHECK(std::system(command.c_str())==0);
  CHECK(sameSubmission(HEPDataSubmission::read(String(archive2)),s));
}

void testRoundTrips(const std::string & work)
{
  HEPDataTable t;
  t.name = "Spectrum";
  t.description = "A test table.";
  t.setKeyword("observables",{"DN/DPT"});
  HEPDataVariable x;
  x.name = "$p_{T}$";
  x.units = "GEV";
  x.values = {HEPDataValue::bin(0.0,0.5), HEPDataValue::bin(0.5,1.0),
              HEPDataValue::bin(1.0,std::numeric_limits<double>::infinity())};
  HEPDataVariable y;
  y.name = "$dN/dp_{T}$";
  y.qualifiers.push_back({String("SQRT(S)"),String("5020"),String("GEV")});
  HEPDataValue a = HEPDataValue::number(1.0/3.0);
  a.errors.push_back(HEPDataError::symmetric(0.1,"stat"));
  a.errors.push_back(HEPDataError::asymmetric(0.05,-0.02,"sys"));
  HEPDataValue b = HEPDataValue::number(2.5e-7);
  b.errors.push_back(HEPDataError::symmetric(4.0,"stat",true));
  HEPDataError oneSided = HEPDataError::asymmetric(1e-8,0.0,"sys");
  oneSided.minusMissing = true;
  b.errors.push_back(oneSided);
  HEPDataValue c = HEPDataValue::textValue("-");
  y.values = {a,b,c};
  t.independent.push_back(x);
  t.dependent.push_back(y);
  t.datumBlock = "class: Histogram\nname: h\n";

  std::string yaml = t.dataYaml().str();
  CHECK(yaml.find("{low: 0, high: 0.5}")!=std::string::npos);
  CHECK(yaml.find("high: .inf")!=std::string::npos);
  CHECK(yaml.find("0.3333333333333333")!=std::string::npos);            // the shortest exact form
  CHECK(yaml.find("symerror: 4%")!=std::string::npos);
  CHECK(yaml.find("datum:")!=std::string::npos);
  HEPDataTable r;
  r.readDataYaml(String(yaml));
  CHECK(sameTable(r,t,false));
  CHECK(std::isinf(r.independent[0].values[2].high));
  CHECK(!r.dependent[0].values[2].isNumeric() && r.dependent[0].values[2].text=="-");
  CHECK(r.dependent[0].values[1].errors[1].minusMissing && !r.dependent[0].values[1].errors[1].plusMissing);
  CHECK(r.dependent[0].values[1].errors[0].percent);
  CHECK(r.datumBlock.str().find("class: Histogram")!=std::string::npos);
  // without the datum block (a submission to HEPData)
  std::string strict = t.dataYaml(0,false).str();
  CHECK(strict.find("datum")==std::string::npos);
  // significant digits
  std::string three = t.dataYaml(3).str();
  CHECK(three.find("value: 0.333")!=std::string::npos && three.find("0.3333")==std::string::npos);
  // a data file in JSON
  std::string json = work + "/table.json";
  {
  std::ofstream f(json);
  f << R"({"independent_variables": [{"header": {"name": "x"}, "values": [{"low": 0, "high": 1}]}],
          "dependent_variables": [{"header": {"name": "y"}, "values": [{"value": 2.5, "errors": [{"symerror": 0.5}]}]}]})";
  }
  HEPDataSubmission j = HEPDataSubmission::read(String(json));
  CHECK(j.tables.size()==1 && j.tables[0].dependent[0].values[0].value==2.5);
  CHECK(j.tables[0].dependent[0].values[0].errors[0].plus==0.5);
  // the single-YAML-file format: the variables in the documents of submission.yaml
  std::string single = work + "/single.yaml";
  {
  std::ofstream f(single);
  f << "comment: one file\n---\nname: T1\ndescription: first\nkeywords: []\n" << t.dataYaml(0,false).c_str()
    << "---\nname: T2\ndescription: second\nkeywords: []\nindependent_variables: []\n"
    << "dependent_variables:\n- header: {name: y}\n  values:\n  - {value: 1}\n";
  }
  HEPDataSubmission sf = HEPDataSubmission::read(String(single));
  CHECK(sf.tables.size()==2 && sf.comment=="one file");
  CHECK(sf.table("T1")!=nullptr && sameTable(*sf.table("T1"),t,false));
  CHECK(sf.table("T2")!=nullptr && sf.table("T2")->dataFile=="data2.yaml");
  // written as a submission (data files named after their positions)
  HEPDataSubmission w;
  w.comment = "Written by TestHEPData.";
  w.addTable(t);
  HEPDataTable t2(t);
  t2.name = "Second";
  t2.dataFile = String();
  w.addTable(t2);
  CHECK(w.tables[1].dataFile=="data2.yaml");
  w.write(String(work+"/written"));
  HEPDataSubmission wb = HEPDataSubmission::read(String(work+"/written"));
  CHECK(sameSubmission(wb,w));
  CHECK(wb.tables[0].datumBlock.str().find("name: h")!=std::string::npos);
  w.write(String(work+"/strict"),String(),0,false);
  CHECK(HEPDataSubmission::read(String(work+"/strict")).tables[0].datumBlock.isNull());
}

void testProblems(const std::string & work)
{
  HEPDataTable t;
  HEPDataVariable x, y;
  x.name = "x";
  x.values = {HEPDataValue::bin(0.0,1.0), HEPDataValue::bin(1.0,2.0)};
  HEPDataValue lonely;
  lonely.hasLow = true;
  x.values.push_back(lonely);
  y.name = "y";
  HEPDataValue zero = HEPDataValue::number(1.0);
  zero.errors.push_back(HEPDataError::symmetric(0.0,"stat"));
  y.values = {zero, HEPDataValue::number(2.0)};
  t.independent.push_back(x);
  t.dependent.push_back(y);
  t.name = String(std::string(70,'n'));
  std::vector<String> p = t.problems();
  auto has = [&p](const char * words) { for (const String & s : p) if (s.str().find(words)!=std::string::npos) return true; return false; };
  CHECK(has("more than 64"));
  CHECK(has("no description"));
  CHECK(has("no keywords"));
  CHECK(has("only one edge"));
  CHECK(has("has 2 values, not 3"));
  CHECK(has("errors are all zero"));
  HEPDataSubmission s;
  s.addTable(t);
  s.addTable(t);
  CHECK(s.problems().size()>=2*p.size()+1);
  // invalid files
  CHECK(throws([]() { HEPDataSubmission::read("/no/such/file.yaml"); }));
  std::string bad = work + "/bad.yaml";
  { std::ofstream f(bad); f << "independent_variables: [\n"; }
  CHECK(throws([&bad]() { HEPDataSubmission::read(String(bad)); }));
  std::string badError = work + "/badError.yaml";
  { std::ofstream f(badError); f << "independent_variables: []\ndependent_variables:\n- header: {name: y}\n  values:\n  - value: 1\n    errors:\n    - {symerror: abc}\n"; }
  CHECK(throws([&badError]() { HEPDataSubmission::read(String(badError)); }));
  std::string missing = work + "/missing";
  fs::create_directories(missing);
  { std::ofstream f(missing+"/submission.yaml"); f << "name: T\ndescription: d\nkeywords: []\ndata_file: absent.yaml\n"; }
  CHECK(throws([&missing]() { HEPDataSubmission::read(String(missing)); }));
}
}

int main(int argc, char ** argv)
{
  std::string data = argc>1 ? argv[1] : "data";
  std::string work = temporaryDirectory();
  if (work.empty()) { std::printf("FAILED: no temporary directory\n"); return 1; }
  try
    {
    testErrors();
    testAlice(data,work);
    testSample(data,work);
    testRoundTrips(work);
    testProblems(work);
    }
  catch (const Exception & e)
    {
    std::printf("FAILED: exception: %s\n", e.what());
    nFailures++;
    }
  std::error_code ignored;
  fs::remove_all(work,ignored);
  std::printf("TestHEPData: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
