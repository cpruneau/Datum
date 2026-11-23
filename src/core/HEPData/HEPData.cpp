/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//
// HEPDataTable and HEPDataSubmission: the data model and its YAML reading and writing (yaml-cpp, used only here).
//
#include "HEPDataTable.hpp"
#include "HEPDataSubmission.hpp"
#include "Exceptions.hpp"
#include "yaml-cpp/yaml.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace Datum
{

namespace
{
//////////////////////////////////////////////////////////////////////////
// Numbers and text
//////////////////////////////////////////////////////////////////////////

std::string trim(const std::string & s)
{
  std::size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
  return b==std::string::npos ? std::string() : s.substr(b,e-b+1);
}

//! Parse a number as YAML writes it (also .inf, -.inf, .nan); false if the text is not a number.
bool parseNumber(const std::string & text, double & value)
{
  std::string t = trim(text);
  if (t.empty()) return false;
  std::string l = t;
  std::transform(l.begin(),l.end(),l.begin(),[](unsigned char c){ return char(std::tolower(c)); });
  if (l==".inf" || l=="+.inf" || l=="inf" || l=="+inf") { value = std::numeric_limits<double>::infinity(); return true; }
  if (l=="-.inf" || l=="-inf") { value = -std::numeric_limits<double>::infinity(); return true; }
  if (l==".nan" || l=="nan") { value = std::numeric_limits<double>::quiet_NaN(); return true; }
  char * end = nullptr;
  value = std::strtod(t.c_str(),&end);
  return end && *end=='\0';
}

//! A number as text: the shortest form that reads back exactly (digits 0), or with that many significant digits.
std::string formatNumber(double v, unsigned int digits)
{
  if (std::isinf(v)) return v>0 ? ".inf" : "-.inf";
  if (std::isnan(v)) return ".nan";
  char buffer[64];
  if (digits>0)
    {
    std::snprintf(buffer,sizeof(buffer),"%.*g",int(std::min(digits,17u)),v);
    return buffer;
    }
  // shortest exact form; integers and numbers up to 1e15 without an exponent (10, not 1e+01)
  int minimum = 1;
  double a = std::fabs(v);
  if (a>=1.0 && a<1.0e15) minimum = int(std::floor(std::log10(a)))+1;
  for (int p=minimum; p<=17; p++)
    {
    std::snprintf(buffer,sizeof(buffer),"%.*g",p,v);
    if (std::strtod(buffer,nullptr)==v) break;
    }
  return buffer;
}

std::string scalarText(const YAML::Node & n) { return n && n.IsScalar() ? n.Scalar() : std::string(); }

String stringOf(const YAML::Node & n)
{
  if (!n || n.IsNull()) return String();
  if (n.IsScalar()) return String(n.Scalar());
  return String(YAML::Dump(n));
}

[[noreturn]] void invalid(const String & where, const std::string & message)
{
  throw FileException(where,String(message),"HEPData");
}

//////////////////////////////////////////////////////////////////////////
// Reading
//////////////////////////////////////////////////////////////////////////

//! A side of an asymmetric error, or a symmetric error: number, percentage, or '' (missing).
void readErrorNumber(const YAML::Node & n, double & value, bool & missing, bool & percent, const String & where)
{
  missing = false;
  if (!n || n.IsNull()) { missing = true; value = 0.0; return; }
  if (!n.IsScalar()) invalid(where,"an error is not a number");
  std::string t = trim(n.Scalar());
  if (t.empty()) { missing = true; value = 0.0; return; }
  if (t.back()=='%') { percent = true; t = trim(t.substr(0,t.size()-1)); }
  if (!parseNumber(t,value)) invalid(where,"an error is not a number: "+n.Scalar());
}

HEPDataError readError(const YAML::Node & n, const String & where)
{
  if (!n.IsMap()) invalid(where,"an error is not a map");
  HEPDataError e;
  e.label = stringOf(n["label"]);
  if (n["symerror"])
    {
    bool missing = false;
    e.kind = HEPDataError::kSymmetric;
    readErrorNumber(n["symerror"],e.plus,missing,e.percent,where);
    if (missing) invalid(where,"a symmetric error without a value");
    e.minus = -e.plus;
    }
  else if (n["asymerror"])
    {
    YAML::Node a = n["asymerror"];
    if (!a.IsMap()) invalid(where,"an asymmetric error is not a map with plus and minus");
    e.kind = HEPDataError::kAsymmetric;
    bool pPercent = false, mPercent = false;
    readErrorNumber(a["plus"],e.plus,e.plusMissing,pPercent,where);
    readErrorNumber(a["minus"],e.minus,e.minusMissing,mPercent,where);
    e.percent = pPercent || mPercent;
    }
  else invalid(where,"an error with neither symerror nor asymerror");
  return e;
}

HEPDataValue readValue(const YAML::Node & n, bool dependent, const String & where)
{
  HEPDataValue v;
  if (!n.IsMap()) invalid(where,"a value is not a map");
  if (n["value"])
    {
    v.hasValue = true;
    std::string t = scalarText(n["value"]);
    if (parseNumber(t,v.value)) v.numeric = true;
    else { v.numeric = false; v.text = String(n["value"].IsScalar() ? t : YAML::Dump(n["value"])); }
    }
  if (n["low"])
    {
    if (!parseNumber(scalarText(n["low"]),v.low)) invalid(where,"a lower edge is not a number");
    v.hasLow = true;
    }
  if (n["high"])
    {
    if (!parseNumber(scalarText(n["high"]),v.high)) invalid(where,"an upper edge is not a number");
    v.hasHigh = true;
    }
  if (dependent && n["errors"])
    {
    YAML::Node errors = n["errors"];
    if (!errors.IsSequence() && !errors.IsNull()) invalid(where,"errors is not a list");
    if (errors.IsSequence()) for (const YAML::Node & e : errors) v.errors.push_back(readError(e,where));
    }
  return v;
}

HEPDataVariable readVariable(const YAML::Node & n, bool dependent, const String & where)
{
  if (!n.IsMap()) invalid(where,"a variable is not a map");
  HEPDataVariable variable;
  YAML::Node header = n["header"];
  if (header && header.IsMap())
    {
    variable.name  = stringOf(header["name"]);
    variable.units = stringOf(header["units"]);
    }
  if (n["qualifiers"] && n["qualifiers"].IsSequence())
    for (const YAML::Node & q : n["qualifiers"])
      {
      HEPDataQualifier qualifier;
      qualifier.name  = stringOf(q["name"]);
      qualifier.value = stringOf(q["value"]);
      qualifier.units = stringOf(q["units"]);
      variable.qualifiers.push_back(qualifier);
      }
  YAML::Node values = n["values"];
  if (values && values.IsSequence())
    for (const YAML::Node & v : values) variable.values.push_back(readValue(v,dependent,where));
  return variable;
}

void readVariables(const YAML::Node & document, HEPDataTable & table, const String & where)
{
  table.independent.clear();
  table.dependent.clear();
  table.datumBlock = String();
  YAML::Node x = document["independent_variables"], y = document["dependent_variables"];
  if (x && !x.IsSequence() && !x.IsNull()) invalid(where,"independent_variables is not a list");
  if (y && !y.IsSequence() && !y.IsNull()) invalid(where,"dependent_variables is not a list");
  if (x && x.IsSequence()) for (const YAML::Node & v : x) table.independent.push_back(readVariable(v,false,where));
  if (y && y.IsSequence()) for (const YAML::Node & v : y) table.dependent.push_back(readVariable(v,true,where));
  if (document["datum"]) table.datumBlock = String(YAML::Dump(document["datum"]));
}

HEPDataResource readResource(const YAML::Node & n)
{
  HEPDataResource r;
  r.location    = stringOf(n["location"]);
  r.description = stringOf(n["description"]);
  r.type        = stringOf(n["type"]);
  if (n["license"] && n["license"].IsMap())
    {
    r.licenseName        = stringOf(n["license"]["name"]);
    r.licenseUrl         = stringOf(n["license"]["url"]);
    r.licenseDescription = stringOf(n["license"]["description"]);
    }
  return r;
}

std::vector<HEPDataResource> readResources(const YAML::Node & n)
{
  std::vector<HEPDataResource> resources;
  if (n && n.IsSequence()) for (const YAML::Node & r : n) if (r.IsMap()) resources.push_back(readResource(r));
  return resources;
}

std::string readText(const String & fileName)
{
  std::ifstream in(fileName.c_str(),std::ios::binary);
  if (!in) invalid(fileName,"cannot open the file");
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

YAML::Node loadOne(const std::string & text, const String & where)
{
  try { return YAML::Load(text); }
  catch (const YAML::Exception & e) { invalid(where,std::string("invalid YAML: ")+e.what()); }
}

//////////////////////////////////////////////////////////////////////////
// Writing
//////////////////////////////////////////////////////////////////////////

void emitString(YAML::Emitter & out, const String & s)
{
  if (s.str().find('\n')!=std::string::npos) out << YAML::Literal;
  out << s.str();
}

void emitErrorNumber(YAML::Emitter & out, double v, bool missing, bool percent, unsigned int digits)
{
  if (missing) { out << std::string(""); return; }
  out << (formatNumber(v,digits) + (percent ? "%" : ""));
}

void emitValue(YAML::Emitter & out, const HEPDataValue & v, bool dependent, unsigned int digits)
{
  if (!dependent)
    {
    out << YAML::Flow << YAML::BeginMap;
    if (v.hasValue) { out << YAML::Key << "value" << YAML::Value; if (v.numeric) out << formatNumber(v.value,digits); else out << v.text.str(); }
    if (v.hasLow)  out << YAML::Key << "low"  << YAML::Value << formatNumber(v.low,digits);
    if (v.hasHigh) out << YAML::Key << "high" << YAML::Value << formatNumber(v.high,digits);
    out << YAML::EndMap;
    return;
    }
  out << YAML::BeginMap;
  out << YAML::Key << "value" << YAML::Value;
  if (!v.hasValue) out << std::string("-");
  else if (v.numeric) out << formatNumber(v.value,digits);
  else out << v.text.str();
  if (!v.errors.empty())
    {
    out << YAML::Key << "errors" << YAML::Value << YAML::BeginSeq;
    for (const HEPDataError & e : v.errors)
      {
      out << YAML::Flow << YAML::BeginMap;
      if (e.kind==HEPDataError::kSymmetric)
        {
        out << YAML::Key << "symerror" << YAML::Value;
        emitErrorNumber(out,e.plus,false,e.percent,digits);
        }
      else
        {
        out << YAML::Key << "asymerror" << YAML::Value << YAML::Flow << YAML::BeginMap;
        out << YAML::Key << "plus"  << YAML::Value; emitErrorNumber(out,e.plus,e.plusMissing,e.percent,digits);
        out << YAML::Key << "minus" << YAML::Value; emitErrorNumber(out,e.minus,e.minusMissing,e.percent,digits);
        out << YAML::EndMap;
        }
      if (!e.label.isNull()) out << YAML::Key << "label" << YAML::Value << e.label.str();
      out << YAML::EndMap;
      }
    out << YAML::EndSeq;
    }
  out << YAML::EndMap;
}

void emitVariable(YAML::Emitter & out, const HEPDataVariable & variable, bool dependent, unsigned int digits)
{
  out << YAML::BeginMap;
  out << YAML::Key << "header" << YAML::Value << YAML::Flow << YAML::BeginMap
      << YAML::Key << "name" << YAML::Value << variable.name.str();
  if (!variable.units.isNull()) out << YAML::Key << "units" << YAML::Value << variable.units.str();
  out << YAML::EndMap;
  if (dependent && !variable.qualifiers.empty())
    {
    out << YAML::Key << "qualifiers" << YAML::Value << YAML::BeginSeq;
    for (const HEPDataQualifier & q : variable.qualifiers)
      {
      out << YAML::Flow << YAML::BeginMap << YAML::Key << "name" << YAML::Value << q.name.str();
      if (!q.units.isNull()) out << YAML::Key << "units" << YAML::Value << q.units.str();
      out << YAML::Key << "value" << YAML::Value << q.value.str() << YAML::EndMap;
      }
    out << YAML::EndSeq;
    }
  out << YAML::Key << "values" << YAML::Value << YAML::BeginSeq;
  for (const HEPDataValue & v : variable.values) emitValue(out,v,dependent,digits);
  out << YAML::EndSeq;
  out << YAML::EndMap;
}

void emitLicense(YAML::Emitter & out, const char * key, const String & name, const String & url, const String & description)
{
  if (name.isNull() && url.isNull() && description.isNull()) return;
  out << YAML::Key << key << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "name" << YAML::Value << name.str();
  out << YAML::Key << "url" << YAML::Value << url.str();
  if (!description.isNull()) out << YAML::Key << "description" << YAML::Value << description.str();
  out << YAML::EndMap;
}

void emitResources(YAML::Emitter & out, const std::vector<HEPDataResource> & resources)
{
  if (resources.empty()) return;
  out << YAML::Key << "additional_resources" << YAML::Value << YAML::BeginSeq;
  for (const HEPDataResource & r : resources)
    {
    out << YAML::BeginMap << YAML::Key << "location" << YAML::Value << r.location.str()
        << YAML::Key << "description" << YAML::Value << r.description.str();
    if (!r.type.isNull()) out << YAML::Key << "type" << YAML::Value << r.type.str();
    emitLicense(out,"license",r.licenseName,r.licenseUrl,r.licenseDescription);
    out << YAML::EndMap;
    }
  out << YAML::EndSeq;
}

void writeText(const String & fileName, const std::string & text)
{
  std::ofstream out(fileName.c_str(),std::ios::binary);
  if (!out) invalid(fileName,"cannot open the file for writing");
  out << text;
  if (!out) invalid(fileName,"error writing the file");
}

//////////////////////////////////////////////////////////////////////////
// Archives (tar, unzip, zip)
//////////////////////////////////////////////////////////////////////////

std::string shellQuoted(const std::string & s)
{
  std::string q = "'";
  for (char c : s) { if (c=='\'') q += "'\\''"; else q += c; }
  return q + "'";
}

bool endsWith(const std::string & s, const std::string & end)
{
  if (s.size()<end.size()) return false;
  std::string tail = s.substr(s.size()-end.size());
  std::transform(tail.begin(),tail.end(),tail.begin(),[](unsigned char c){ return char(std::tolower(c)); });
  return tail==end;
}

bool isArchive(const std::string & s)
{
  return endsWith(s,".tar.gz") || endsWith(s,".tgz") || endsWith(s,".tar") || endsWith(s,".zip");
}

//! A temporary directory, removed with its content when destroyed.
struct TemporaryDirectory
{
  fs::path path;
  TemporaryDirectory()
  {
    std::string pattern = (fs::temp_directory_path()/"datum_hepdata_XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end());
    name.push_back('\0');
    if (!::mkdtemp(name.data())) invalid(String(pattern),"cannot create a temporary directory");
    path = fs::path(name.data());
  }
  ~TemporaryDirectory() { std::error_code ignored; fs::remove_all(path,ignored); }
};

void extract(const std::string & archive, const fs::path & directory)
{
  std::string command = "tar -xf " + shellQuoted(archive) + " -C " + shellQuoted(directory.string()) + " 2>/dev/null";
  if (std::system(command.c_str())==0) return;
  if (endsWith(archive,".zip"))
    {
    command = "unzip -q -o " + shellQuoted(archive) + " -d " + shellQuoted(directory.string()) + " 2>/dev/null";
    if (std::system(command.c_str())==0) return;
    }
  invalid(String(archive),"cannot extract the archive (tar or unzip failed)");
}

//! The directory holding submission.yaml: the directory itself, or its single sub-directory (archives of hepdata.net).
fs::path submissionDirectory(const fs::path & directory)
{
  for (const char * name : {"submission.yaml","submission.yml"})
    if (fs::is_regular_file(directory/name)) return directory;
  std::vector<fs::path> subdirectories;
  for (const auto & entry : fs::directory_iterator(directory))
    if (entry.is_directory() && entry.path().filename().string()!="__MACOSX") subdirectories.push_back(entry.path());
  if (subdirectories.size()==1) return submissionDirectory(subdirectories[0]);
  invalid(String(directory.string()),"no submission.yaml in the directory");
}
}

//////////////////////////////////////////////////////////////////////////
// HEPDataError, HEPDataValue, HEPDataVariable
//////////////////////////////////////////////////////////////////////////

HEPDataError HEPDataError::symmetric(double error, const String & label, bool percent)
{
  HEPDataError e;
  e.kind = kSymmetric;
  e.plus = error;
  e.minus = -error;
  e.label = label;
  e.percent = percent;
  return e;
}

HEPDataError HEPDataError::asymmetric(double plus, double minus, const String & label, bool percent)
{
  HEPDataError e;
  e.kind = kAsymmetric;
  e.plus = plus;
  e.minus = minus;
  e.label = label;
  e.percent = percent;
  return e;
}

double HEPDataError::upper(double value) const
{
  double scale = percent ? std::fabs(value)/100.0 : 1.0;
  if (kind==kSymmetric) return std::fabs(plus)*scale;
  double p = plusMissing ? 0.0 : plus, m = minusMissing ? 0.0 : minus;
  return std::max(0.0,std::max(p,m))*scale;
}

double HEPDataError::lower(double value) const
{
  double scale = percent ? std::fabs(value)/100.0 : 1.0;
  if (kind==kSymmetric) return std::fabs(plus)*scale;
  double p = plusMissing ? 0.0 : plus, m = minusMissing ? 0.0 : minus;
  return -std::min(0.0,std::min(p,m))*scale;
}

bool HEPDataError::isStatistical() const
{
  std::string l = label.str();
  std::size_t comma = l.find(',');
  if (comma!=std::string::npos) l = l.substr(0,comma);
  l = trim(l);
  std::transform(l.begin(),l.end(),l.begin(),[](unsigned char c){ return char(std::tolower(c)); });
  return l=="stat" || l=="stat." || l=="statistical";
}

HEPDataValue HEPDataValue::number(double v)
{
  HEPDataValue x;
  x.hasValue = true;
  x.value = v;
  return x;
}

HEPDataValue HEPDataValue::bin(double low, double high)
{
  HEPDataValue x;
  x.hasLow = x.hasHigh = true;
  x.low = low;
  x.high = high;
  return x;
}

HEPDataValue HEPDataValue::textValue(const String & text)
{
  HEPDataValue x;
  x.hasValue = true;
  x.numeric = false;
  x.text = text;
  return x;
}

double HEPDataValue::center() const
{
  if (isNumeric()) return value;
  if (isBin())
    {
    if (std::isinf(low) && std::isinf(high)) return std::numeric_limits<double>::quiet_NaN();
    if (std::isinf(low))  return high;
    if (std::isinf(high)) return low;
    return 0.5*(low+high);
    }
  return std::numeric_limits<double>::quiet_NaN();
}

bool HEPDataVariable::isBinned() const
{
  if (values.empty()) return false;
  for (const HEPDataValue & v : values) if (!v.isBin()) return false;
  return true;
}

const HEPDataQualifier * HEPDataVariable::qualifier(const String & n) const
{
  for (const HEPDataQualifier & q : qualifiers) if (q.name==n) return &q;
  return nullptr;
}

std::vector<String> HEPDataVariable::errorLabels() const
{
  std::vector<String> labels;
  for (const HEPDataValue & v : values)
    for (const HEPDataError & e : v.errors)
      if (std::find(labels.begin(),labels.end(),e.label)==labels.end()) labels.push_back(e.label);
  return labels;
}

//////////////////////////////////////////////////////////////////////////
// HEPDataTable
//////////////////////////////////////////////////////////////////////////

unsigned int HEPDataTable::nRows() const
{
  if (!independent.empty()) return independent[0].size();
  if (!dependent.empty())   return dependent[0].size();
  return 0;
}

std::vector<String> HEPDataTable::keyword(const String & n) const
{
  for (const HEPDataKeyword & k : keywords) if (k.name==n) return k.values;
  return std::vector<String>();
}

void HEPDataTable::setKeyword(const String & n, const std::vector<String> & values)
{
  for (HEPDataKeyword & k : keywords) if (k.name==n) { k.values = values; return; }
  HEPDataKeyword k;
  k.name = n;
  k.values = values;
  keywords.push_back(k);
}

std::vector<String> HEPDataTable::problems() const
{
  std::vector<String> p;
  if (name.isNull()) p.push_back("no name");
  if (name.length()>64) p.push_back("the name has more than 64 characters");
  if (description.isNull()) p.push_back("no description");
  if (keywords.empty()) p.push_back("no keywords");
  if (location.length()>256) p.push_back("the location has more than 256 characters");
  if (!independent.empty() && dependent.empty()) p.push_back("independent variables without dependent variables");
  unsigned int n = nRows();
  for (const HEPDataVariable & v : independent)
    {
    if (v.size()!=n) p.push_back(String("independent variable '")+v.name+"' has "+String(std::to_string(v.size()))+" values, not "+String(std::to_string(n)));
    for (const HEPDataValue & x : v.values)
      if (x.hasLow!=x.hasHigh) { p.push_back(String("independent variable '")+v.name+"' has a value with only one edge"); break; }
    }
  for (const HEPDataVariable & v : dependent)
    {
    if (v.size()!=n) p.push_back(String("dependent variable '")+v.name+"' has "+String(std::to_string(v.size()))+" values, not "+String(std::to_string(n)));
    unsigned int zero = 0, oneSided = 0;
    for (const HEPDataValue & x : v.values)
      {
      if (x.isNumeric() && !x.errors.empty())
        {
        bool allZero = true;
        for (const HEPDataError & e : x.errors) if (e.upper(x.value)!=0.0 || e.lower(x.value)!=0.0) allZero = false;
        if (allZero) zero++;
        }
      for (const HEPDataError & e : x.errors) if (e.kind==HEPDataError::kAsymmetric && e.plusMissing && e.minusMissing) oneSided++;
      }
    if (zero>0) p.push_back(String("dependent variable '")+v.name+"' has "+String(std::to_string(zero))+" value(s) whose errors are all zero");
    if (oneSided>0) p.push_back(String("dependent variable '")+v.name+"' has "+String(std::to_string(oneSided))+" asymmetric error(s) missing on both sides");
    }
  return p;
}

void HEPDataTable::readDataYaml(const String & text)
{
  YAML::Node document = loadOne(text.str(),name.isNull() ? String("data") : name);
  if (!document.IsMap()) invalid(name,"a data file is not a map");
  readVariables(document,*this,name.isNull() ? String("data") : name);
}

void HEPDataTable::readDataFile(const String & fileName)
{
  YAML::Node document = loadOne(readText(fileName),fileName);
  if (!document.IsMap()) invalid(fileName,"a data file is not a map");
  readVariables(document,*this,fileName);
}

String HEPDataTable::dataYaml(unsigned int digits, bool withDatumBlock) const
{
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "independent_variables" << YAML::Value;
  if (independent.empty()) out << YAML::Flow;
  out << YAML::BeginSeq;
  for (const HEPDataVariable & v : independent) emitVariable(out,v,false,digits);
  out << YAML::EndSeq;
  out << YAML::Key << "dependent_variables" << YAML::Value;
  if (dependent.empty()) out << YAML::Flow;
  out << YAML::BeginSeq;
  for (const HEPDataVariable & v : dependent) emitVariable(out,v,true,digits);
  out << YAML::EndSeq;
  if (withDatumBlock && !datumBlock.isNull())
    out << YAML::Key << "datum" << YAML::Value << loadOne(datumBlock.str(),name);
  out << YAML::EndMap;
  if (!out.good()) invalid(name,std::string("cannot write the data file: ")+out.GetLastError());
  return String(std::string(out.c_str())+"\n");
}

void HEPDataTable::writeDataFile(const String & fileName, unsigned int digits, bool withDatumBlock) const
{
  writeText(fileName,dataYaml(digits,withDatumBlock).str());
}

//////////////////////////////////////////////////////////////////////////
// HEPDataPublication
//////////////////////////////////////////////////////////////////////////

namespace
{
struct PublicationKey { const char * name; String HEPDataPublication::* field; };

const std::vector<PublicationKey> & publicationKeys()
{
  static const std::vector<PublicationKey> keys = {
    {"Collaboration",&HEPDataPublication::collaboration}, {"Title",&HEPDataPublication::title},
    {"INSPIRE",&HEPDataPublication::inspire}, {"arXiv",&HEPDataPublication::arXiv}, {"DOI",&HEPDataPublication::doi},
    {"Journal",&HEPDataPublication::journal}, {"Contact",&HEPDataPublication::contact} };
  return keys;
}

std::string lowerCase(std::string s)
{
  for (char & c : s) c = (char) std::tolower((unsigned char) c);
  return s;
}

std::string trimmedText(const std::string & s)
{
  std::size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a==std::string::npos ? std::string() : s.substr(a,b-a+1);
}

//! The field of a key read in a header line (other names used by collaborations accepted), or nullptr.
String HEPDataPublication::* publicationField(const std::string & key)
{
  std::string k = lowerCase(trimmedText(key));
  if (k=="inspirehep" || k=="inspire id" || k=="inspire-hep" || k=="inspire record") k = "inspire";
  if (k=="reference" || k=="journal reference") k = "journal";
  if (k=="email" || k=="e-mail" || k=="contacts") k = "contact";
  for (const PublicationKey & p : publicationKeys()) if (lowerCase(p.name)==k) return p.field;
  return nullptr;
}
}

bool HEPDataPublication::isEmpty() const
{
  for (const PublicationKey & p : publicationKeys()) if (!(this->*p.field).isNull()) return false;
  return notes.empty();
}

String HEPDataPublication::headerLines() const
{
  std::string text;
  for (const PublicationKey & p : publicationKeys())
    {
    std::string v = trimmedText((this->*p.field).str());
    for (char & c : v) if (c=='\n' || c=='\r') c = ' ';
    if (!v.empty()) text += std::string("# ")+p.name+": "+v+"\n";
    }
  for (const String & n : notes)
    {
    std::string v = n.str();
    for (char & c : v) if (c=='\n' || c=='\r') c = ' ';
    text += (v.empty() ? std::string("#") : "# "+v)+"\n";
    }
  return String(text);
}

HEPDataPublication HEPDataPublication::fromHeader(const String & text)
{
  HEPDataPublication p;
  std::istringstream in(text.str());
  std::string line;
  while (std::getline(in,line))
    {
    std::string t = trimmedText(line);
    if (t.empty()) continue;
    if (t[0]!='#') break;                                     // the comment lines at the top only
    std::string body = trimmedText(t.substr(1));
    std::size_t colon = body.find(':');
    String HEPDataPublication::* field = colon==std::string::npos ? nullptr : publicationField(body.substr(0,colon));
    if (field && (p.*field).isNull()) p.*field = String(trimmedText(body.substr(colon+1)));
    else p.notes.push_back(String(body));
    }
  return p;
}

String HEPDataPublication::summary() const
{
  std::string s;
  auto append = [&s](const std::string & part) { if (part.empty()) return; if (!s.empty()) s += ", "; s += part; };
  append(collaboration.isNull() ? std::string() : collaboration.str()+" Collaboration");
  append(title.str());
  append(journal.str());
  append(arXiv.isNull() ? std::string() : "arXiv:"+arXiv.str());
  append(doi.isNull() ? std::string() : "doi:"+doi.str());
  if (!inspire.isNull()) s += (s.empty() ? std::string() : std::string(" "))+"(INSPIRE "+inspire.str()+")";
  if (!s.empty()) s += ".";
  return String(s);
}

//////////////////////////////////////////////////////////////////////////
// HEPDataSubmission
//////////////////////////////////////////////////////////////////////////

HEPDataSubmission HEPDataSubmission::readYaml(const String & text, const String & directory)
{
  std::vector<YAML::Node> documents;
  try { documents = YAML::LoadAll(text.str()); }
  catch (const YAML::Exception & e) { invalid(String("submission.yaml"),std::string("invalid YAML: ")+e.what()); }
  HEPDataSubmission s;
  s.publication = HEPDataPublication::fromHeader(text);
  for (const YAML::Node & d : documents)
    {
    if (!d || d.IsNull()) continue;
    if (!d.IsMap()) invalid(String("submission.yaml"),"a document is not a map");
    bool isTable = d["name"] || d["data_file"] || d["independent_variables"] || d["dependent_variables"];
    if (!isTable)
      {
      if (d["comment"]) s.comment = stringOf(d["comment"]);
      std::vector<HEPDataResource> r = readResources(d["additional_resources"]);
      s.resources.insert(s.resources.end(),r.begin(),r.end());
      continue;
      }
    HEPDataTable t;
    t.name        = stringOf(d["name"]);
    t.description = stringOf(d["description"]);
    t.location    = stringOf(d["location"]);
    t.dataFile    = stringOf(d["data_file"]);
    if (d["keywords"] && d["keywords"].IsSequence())
      for (const YAML::Node & k : d["keywords"])
        {
        if (!k.IsMap()) continue;
        HEPDataKeyword keyword;
        keyword.name = stringOf(k["name"]);
        if (k["values"] && k["values"].IsSequence()) for (const YAML::Node & v : k["values"]) keyword.values.push_back(stringOf(v));
        else if (k["values"]) keyword.values.push_back(stringOf(k["values"]));
        t.keywords.push_back(keyword);
        }
    if (d["data_license"] && d["data_license"].IsMap())
      {
      t.licenseName        = stringOf(d["data_license"]["name"]);
      t.licenseUrl         = stringOf(d["data_license"]["url"]);
      t.licenseDescription = stringOf(d["data_license"]["description"]);
      }
    t.resources = readResources(d["additional_resources"]);
    String where = t.name.isNull() ? String("submission.yaml") : t.name;
    if (d["independent_variables"] || d["dependent_variables"])        // the single-YAML-file format
      readVariables(d,t,where);
    else if (!t.dataFile.isNull())
      {
      if (directory.isNull()) invalid(where,"the data file "+t.dataFile.str()+" cannot be read: no directory given");
      t.readDataFile(String((fs::path(directory.str())/t.dataFile.str()).string()));
      }
    s.tables.push_back(t);
    }
  for (unsigned int k=0; k<s.tables.size(); k++)
    if (s.tables[k].dataFile.isNull()) s.tables[k].dataFile = String("data"+std::to_string(k+1)+".yaml");
  return s;
}

HEPDataSubmission HEPDataSubmission::read(const String & path)
{
  fs::path p(path.str());
  if (!fs::exists(p)) invalid(path,"no such file or directory");
  if (fs::is_directory(p))
    {
    fs::path directory = submissionDirectory(p);
    fs::path file = fs::is_regular_file(directory/"submission.yaml") ? directory/"submission.yaml" : directory/"submission.yml";
    return readYaml(String(readText(String(file.string()))),String(directory.string()));
    }
  if (isArchive(p.string()))
    {
    TemporaryDirectory temporary;
    extract(fs::absolute(p).string(),temporary.path);
    return read(String(temporary.path.string()));
    }
  std::string text = readText(path);
  std::string fileName = p.filename().string();
  if (fileName=="submission.yaml" || fileName=="submission.yml")
    return readYaml(String(text),String(p.parent_path().string()));
  YAML::Node document;
  bool single = false;
  try
    {
    std::vector<YAML::Node> documents = YAML::LoadAll(text);
    single = documents.size()==1 && documents[0].IsMap() && !documents[0]["name"] && !documents[0]["data_file"]
             && (documents[0]["independent_variables"] || documents[0]["dependent_variables"]);
    if (single) document = documents[0];
    }
  catch (const YAML::Exception & e) { invalid(path,std::string("invalid YAML: ")+e.what()); }
  if (!single) return readYaml(String(text),String(p.parent_path().string()));   // a submission in one file
  HEPDataSubmission s;
  HEPDataTable t;
  t.name = String(p.stem().string());
  t.dataFile = String(fileName);
  readVariables(document,t,path);
  s.tables.push_back(t);
  return s;
}

const HEPDataTable * HEPDataSubmission::table(const String & n) const
{
  for (const HEPDataTable & t : tables) if (t.name==n) return &t;
  return nullptr;
}

HEPDataTable * HEPDataSubmission::table(const String & n)
{
  for (HEPDataTable & t : tables) if (t.name==n) return &t;
  return nullptr;
}

HEPDataTable & HEPDataSubmission::addTable(const HEPDataTable & t)
{
  tables.push_back(t);
  if (tables.back().dataFile.isNull()) tables.back().dataFile = String("data"+std::to_string(tables.size())+".yaml");
  return tables.back();
}

std::vector<String> HEPDataSubmission::problems() const
{
  std::vector<String> p;
  std::set<std::string> names, files;
  for (const HEPDataTable & t : tables)
    {
    for (const String & q : t.problems()) p.push_back(String("Table '")+t.name+"': "+q);
    if (!names.insert(t.name.str()).second) p.push_back(String("two tables are named '")+t.name+"'");
    if (!files.insert(t.dataFile.str()).second) p.push_back(String("two tables have the data file '")+t.dataFile+"'");
    }
  return p;
}

String HEPDataSubmission::submissionYaml() const
{
  YAML::Emitter out;
  String header = publication.headerLines();
  if (!comment.isNull() || !resources.empty())
    {
    out << YAML::BeginDoc << YAML::BeginMap;
    if (!comment.isNull()) { out << YAML::Key << "comment" << YAML::Value; emitString(out,comment); }
    emitResources(out,resources);
    out << YAML::EndMap;
    }
  for (const HEPDataTable & t : tables)
    {
    out << YAML::BeginDoc << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << t.name.str();
    if (!t.location.isNull()) out << YAML::Key << "location" << YAML::Value << t.location.str();
    out << YAML::Key << "description" << YAML::Value; emitString(out,t.description);
    out << YAML::Key << "keywords" << YAML::Value;
    if (t.keywords.empty()) out << YAML::Flow;
    out << YAML::BeginSeq;
    for (const HEPDataKeyword & k : t.keywords)
      {
      out << YAML::Flow << YAML::BeginMap << YAML::Key << "name" << YAML::Value << k.name.str()
          << YAML::Key << "values" << YAML::Value << YAML::Flow << YAML::BeginSeq;
      for (const String & v : k.values) out << v.str();
      out << YAML::EndSeq << YAML::EndMap;
      }
    out << YAML::EndSeq;
    out << YAML::Key << "data_file" << YAML::Value << t.dataFile.str();
    emitLicense(out,"data_license",t.licenseName,t.licenseUrl,t.licenseDescription);
    emitResources(out,t.resources);
    out << YAML::EndMap;
    }
  if (!out.good()) invalid(String("submission.yaml"),std::string("cannot write submission.yaml: ")+out.GetLastError());
  return String(header.str()+std::string(out.c_str())+"\n");
}

void HEPDataSubmission::write(const String & directory, const String & archive, unsigned int digits, bool withDatumBlock) const
{
  fs::path d(directory.str());
  std::error_code error;
  fs::create_directories(d,error);
  if (!fs::is_directory(d)) invalid(directory,"cannot create the directory");
  writeText(String((d/"submission.yaml").string()),submissionYaml().str());
  for (unsigned int k=0; k<tables.size(); k++)
    {
    String file = tables[k].dataFile.isNull() ? String("data"+std::to_string(k+1)+".yaml") : tables[k].dataFile;
    tables[k].writeDataFile(String((d/file.str()).string()),digits,withDatumBlock);
    }
  if (archive.isNull()) return;
  std::string a = fs::absolute(fs::path(archive.str())).string();
  std::string command;
  if (endsWith(a,".zip"))
    command = "cd " + shellQuoted(fs::absolute(d).string()) + " && zip -q -r " + shellQuoted(a) + " . 2>/dev/null";
  else if (endsWith(a,".tar.gz") || endsWith(a,".tgz"))
    command = "tar -czf " + shellQuoted(a) + " -C " + shellQuoted(fs::absolute(d).string()) + " . 2>/dev/null";
  else
    invalid(archive,"unknown archive type (.tar.gz, .tgz or .zip)");
  if (std::system(command.c_str())!=0) invalid(archive,"cannot make the archive (tar or zip failed)");
}

} // namespace Datum
