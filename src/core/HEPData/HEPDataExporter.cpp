/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file HEPDataExporter.cpp
//! @brief HEPDataExporter: task writing histograms, points, fits and functions of Datum files as a HEPData submission.
#include "HEPDataExporter.hpp"
#include <cstdlib>
#include <filesystem>
#include <fnmatch.h>
#include <iostream>
#include <sstream>
#include "DatumFile.hpp"
#include "FileSystem.hpp"
#include "NameHelpers.hpp"
#include "PrintHelpers.hpp"
#include "Exceptions.hpp"

namespace fs = std::filesystem;

namespace Datum
{

namespace
{
//! Words separated by spaces.
std::vector<String> wordsOf(const String & text)
{
  std::vector<String> words;
  std::istringstream in(text.str());
  std::string w;
  while (in >> w) words.push_back(String(w));
  return words;
}

bool matchesAny(const String & path, const std::vector<String> & patterns)
{
  for (const String & p : patterns) if (::fnmatch(p.str().c_str(),path.str().c_str(),0)==0) return true;
  return false;
}

//! A path of the configuration: variables expanded; a relative path is in the histogram directory of the job
//! ($DATUM_HISTOS_EXPORT_PATH, set by bin/Datum), as the input and output files of jobs, or the current directory.
String resolved(const String & path)
{
  String p = FileSystem::expand(path);
  if (p.isNull() || fs::path(p.str()).is_absolute()) return p;
  const char * base = std::getenv("DATUM_HISTOS_EXPORT_PATH");
  if (!base || !*base) return p;
  return String((fs::path(base)/p.str()).lexically_normal().string());
}

//! A number of the configuration given as a string (NONE: the default). @throws Exception if not a number
double numberOf(const String & text, double otherwise, const String & key)
{
  if (text.isNull()) return otherwise;
  try
    {
    std::size_t used = 0;
    double x = std::stod(text.str(),&used);
    if (used==text.str().size()) return x;
    }
  catch (const std::exception &) { }
  throw Exception(String("not a number: ")+key+" = "+text,"HEPDataExporter");
}
}

HEPDataExporter::HEPDataExporter()
:
Task()
{
appendClassName("HEPDataExporter");
setMinimumReportLevel(Object::kInfo);
}

String HEPDataExporter::text(const String & key) const
{
String v = _configuration.valueString(createKey(name(),key),true,"NONE");
std::string s = v.str();
std::size_t a = s.find_first_not_of(" \t\n"), b = s.find_last_not_of(" \t\n");
s = a==std::string::npos ? std::string() : s.substr(a,b-a+1);
return s=="NONE" ? String() : String(s);
}

void HEPDataExporter::setDefaultConfiguration()
{
Task::setDefaultConfiguration();
const String & taskName = name();
auto add = [&](const char * key, auto value) { _configuration.addProperty(createKey(taskName,key),value); };
add("HISTOGRAM:IMPORT:PATH",      String("NONE"));
add("HISTOGRAM:IMPORT:FILE_NAME", String("NONE"));
add("HEPDATA:EXPORT:PATH",        String("NONE"));
add("HEPDATA:EXPORT:ARCHIVE",     String("NONE"));
add("MODE",                       String("LOSSLESS"));
add("DIGITS",                     0);
add("FITS",                       true);
add("OBJECTS",                    String("*"));
add("EXCLUDE",                    String("NONE"));
add("FUNCTION:LOW",               0.0);
add("FUNCTION:HIGH",              1.0);
add("FUNCTION:POINTS",            100);
for (const char * k : {"OBSERVABLES","REACTIONS","CMENERGIES","PHRASES","QUALIFIERS","LOCATION","LICENSE","LICENSE_URL"})
  _configuration.addProperty(createKey(taskName,"TABLES",k),String("NONE"));
for (const char * k : {"COLLABORATION","TITLE","INSPIRE","ARXIV","DOI","JOURNAL","CONTACT"})
  _configuration.addProperty(createKey(taskName,"PUBLICATION",k),String("NONE"));
add("PUBLICATION:IN_COMMENT",     true);
add("COMMENT",                    String("NONE"));
// the tables listed: their number, and the first one, as an example of the keys of a table (TABLE_2, TABLE_3, ...
// are added the same way)
add("TABLE:N",                    0);
for (const char * k : {"PATH","FILE","NAME","DESCRIPTION","LOCATION","OBSERVABLES","REACTIONS","CMENERGIES","PHRASES",
                       "QUALIFIERS","X_NAME","X_UNITS","Y_NAME","Y_UNITS","LOW","HIGH","POINTS"})
  _configuration.addProperty(createKey(taskName,"TABLE",1,k),String("NONE"));
add("PREVIEW_ONLY",               false);
add("PREVIEW_ROWS",               3);
add("FAIL_ON_PROBLEMS",           false);
}

HEPDataExportPlan HEPDataExporter::makePlan()
{
const String & taskName = name();
// the files
_files.clear();
_fileNames.clear();
String path = text("HISTOGRAM:IMPORT:PATH");
std::vector<String> names = wordsOf(text("HISTOGRAM:IMPORT:FILE_NAME"));
if (names.empty()) throw ConfigurationException(taskName,"No Datum file to read: set HISTOGRAM:IMPORT:FILE_NAME",__FUNCTION__);
String directory = resolved(path.isNull() ? String(".") : path);
if (!directory.isNull() && !directory.endsWith("/")) directory = directory+String("/");
for (const String & n : names)
  {
  _files.push_back(std::shared_ptr<DatumFile>(DatumFile::openOldDatumFile(directory,n)));
  _fileNames.push_back(n);
  }

HEPDataExportPlan plan;
// the submission
plan.directory = resolved(text("HEPDATA:EXPORT:PATH"));
if (plan.directory.isNull()) throw ConfigurationException(taskName,"No directory to write to: set HEPDATA:EXPORT:PATH",__FUNCTION__);
String archive = FileSystem::expand(text("HEPDATA:EXPORT:ARCHIVE"));
if (!archive.isNull() && fs::path(archive.str()).parent_path().empty())   // a name: next to the directory
  archive = String((fs::path(plan.directory.str()).lexically_normal().parent_path()/archive.str()).string());
else archive = resolved(archive);
plan.archive = archive;
String mode = text("MODE").toUpper();
if (mode!="LOSSLESS" && mode!="STRICT")
  throw ConfigurationException(taskName,String("Unknown MODE ")+mode+" (LOSSLESS or STRICT)",__FUNCTION__);
plan.lossless = mode=="LOSSLESS";
int digits = _configuration.valueInt(createKey(taskName,"DIGITS"),true,0);
plan.digits = digits>0 ? unsigned(digits) : 0u;
bool fits = _configuration.valueBool(createKey(taskName,"FITS"),true,true);
// the publication, the comment, the defaults of the tables
HEPDataPublication & p = plan.publication;
p.collaboration = text("PUBLICATION:COLLABORATION");
p.title         = text("PUBLICATION:TITLE");
p.inspire       = text("PUBLICATION:INSPIRE");
p.arXiv         = text("PUBLICATION:ARXIV");
p.doi           = text("PUBLICATION:DOI");
p.journal       = text("PUBLICATION:JOURNAL");
p.contact       = text("PUBLICATION:CONTACT");
plan.publicationInComment = _configuration.valueBool(createKey(taskName,"PUBLICATION:IN_COMMENT"),true,true);
plan.comment = text("COMMENT");
HEPDataExportPlan::TableInfo & d = plan.defaults;
d.observables = text("TABLES:OBSERVABLES");
d.reactions   = text("TABLES:REACTIONS");
d.cmenergies  = text("TABLES:CMENERGIES");
d.phrases     = text("TABLES:PHRASES");
d.qualifiers  = text("TABLES:QUALIFIERS");
d.location    = text("TABLES:LOCATION");
d.licenseName = text("TABLES:LICENSE");
d.licenseUrl  = text("TABLES:LICENSE_URL");
double low  = _configuration.valueDouble(createKey(taskName,"FUNCTION:LOW"),true,0.0);
double high = _configuration.valueDouble(createKey(taskName,"FUNCTION:HIGH"),true,1.0);
int points  = _configuration.valueInt(createKey(taskName,"FUNCTION:POINTS"),true,100);

// the objects of the files
std::vector<std::vector<HEPDataExportPlan::Item>> available;
for (std::size_t k=0; k<_files.size(); k++)
  {
  std::vector<HEPDataExportPlan::Item> items = HEPDataExportPlan::itemsOf(_files[k]->folder(),_fileNames[k],_files[k]);
  for (HEPDataExportPlan::Item & item : items)
    {
    item.fits = fits;
    item.low = low;
    item.high = high;
    item.nPoints = points>1 ? unsigned(points) : 2u;
    }
  available.push_back(items);
  }
// the tables listed, or all the objects matching
auto tableKey = [&](unsigned int k, const char * field) { return String(String("TABLE_")+String(std::to_string(k))+":"+field); };
int nListed = _configuration.valueInt(createKey(taskName,"TABLE:N"),true,0);
if (nListed>0 || !text(tableKey(1,"PATH")).isNull())
  {
  for (unsigned int k=1; nListed<=0 || k<=unsigned(nListed); k++)
    {
    String objectPath = text(tableKey(k,"PATH"));
    if (objectPath.isNull() && nListed<=0) break;
    if (objectPath.isNull())
      throw ConfigurationException(taskName,String("TABLE:N is ")+String(std::to_string(nListed))+" but TABLE_"
                                   +String(std::to_string(k))+":PATH is not set",__FUNCTION__);
    String file = text(tableKey(k,"FILE"));
    const HEPDataExportPlan::Item * found = nullptr;
    for (std::size_t f=0; f<available.size() && !found; f++)
      {
      if (!file.isNull() && !(_fileNames[f]==file)) continue;
      for (const HEPDataExportPlan::Item & item : available[f]) if (item.path==objectPath) { found = &item; break; }
      }
    if (!found)
      throw ConfigurationException(taskName,String("TABLE_")+String(std::to_string(k))+": no histogram, points or function "
                                   +objectPath+(file.isNull() ? String(" in the files read") : String(" in ")+file),__FUNCTION__);
    HEPDataExportPlan::Item item = *found;
    if (!text(tableKey(k,"NAME")).isNull()) item.tableName = text(tableKey(k,"NAME"));
    HEPDataExportPlan::TableInfo & i = item.info;
    i.description = text(tableKey(k,"DESCRIPTION"));
    i.location    = text(tableKey(k,"LOCATION"));
    i.observables = text(tableKey(k,"OBSERVABLES"));
    i.reactions   = text(tableKey(k,"REACTIONS"));
    i.cmenergies  = text(tableKey(k,"CMENERGIES"));
    i.phrases     = text(tableKey(k,"PHRASES"));
    i.qualifiers  = text(tableKey(k,"QUALIFIERS"));
    item.xName  = text(tableKey(k,"X_NAME"));
    item.xUnits = text(tableKey(k,"X_UNITS"));
    item.yName  = text(tableKey(k,"Y_NAME"));
    item.yUnits = text(tableKey(k,"Y_UNITS"));
    item.low  = numberOf(text(tableKey(k,"LOW")),item.low,tableKey(k,"LOW"));
    item.high = numberOf(text(tableKey(k,"HIGH")),item.high,tableKey(k,"HIGH"));
    item.nPoints = unsigned(std::max(2.0,numberOf(text(tableKey(k,"POINTS")),item.nPoints,tableKey(k,"POINTS"))));
    plan.items.push_back(item);
    }
  }
else
  {
  std::vector<String> objects = wordsOf(text("OBJECTS"));
  if (objects.empty()) objects.push_back("*");
  std::vector<String> excluded = wordsOf(text("EXCLUDE"));
  for (const auto & items : available)
    for (const HEPDataExportPlan::Item & item : items)
      if (matchesAny(item.path,objects) && !matchesAny(item.path,excluded)) plan.items.push_back(item);
  }
return plan;
}

void HEPDataExporter::execute()
{
const String & taskName = name();
_written = HEPDataSubmission();
HEPDataExportPlan plan = makePlan();
int rows = _configuration.valueInt(createKey(taskName,"PREVIEW_ROWS"),true,3);
_preview = plan.preview(rows>0 ? unsigned(rows) : 0u);
bool previewOnly = _configuration.valueBool(createKey(taskName,"PREVIEW_ONLY"),true,false);
bool strict = _configuration.valueBool(createKey(taskName,"FAIL_ON_PROBLEMS"),true,false);
if (reportInfo(__FUNCTION__))
  {
  printCR();
  std::cout << _preview.toText().str();
  }
std::vector<String> errors;
HEPDataSubmission s = plan.build(&errors);
std::vector<String> problems = s.problems();
if (strict && (!errors.empty() || !problems.empty()))
  {
  String what = !errors.empty() ? String("an object cannot be written: ")+errors[0]
                                : String("hepdata.net would reject the submission: ")+problems[0];
  throw Exception(what+String(errors.size()+problems.size()>1 ? " (and more, see the preview)" : ""),__FUNCTION__);
  }
if ((!errors.empty() || !problems.empty()) && reportWarning(__FUNCTION__))
  {
  for (const String & e : errors) std::cout << "Left out: " << e.str() << std::endl;
  if (!problems.empty())
    std::cout << problems.size() << " problem" << (problems.size()==1 ? "" : "s")
              << " that hepdata.net would reject (see the preview)" << std::endl;
  }
if (previewOnly)
  {
  if (reportInfo(__FUNCTION__)) printString("PREVIEW_ONLY: nothing written");
  return;
  }
_written = plan.write();
if (reportInfo(__FUNCTION__))
  {
  printValue("HEPData tables written",int(_written.tables.size()));
  printValue("HEPData directory",plan.directory);
  if (!plan.archive.isNull()) printValue("HEPData archive",plan.archive);
  }
}

} // namespace Datum
