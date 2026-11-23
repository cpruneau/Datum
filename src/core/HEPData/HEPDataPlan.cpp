/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file HEPDataPlan.cpp
//! @brief HEPDataPreviewNode, HEPDataPreview, HEPDataImportPlan, HEPDataExportPlan.
#include "HEPDataPlan.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <sstream>
#include "Folder.hpp"
#include "Exceptions.hpp"
#include "Histogram.hpp"
#include "HistogramSyst.hpp"
#include "Profile.hpp"
#include "DataPointsVector.hpp"
#include "DataPointColumns.hpp"
#include "FitRecord.hpp"
#include "ParameterizedFunction.hpp"
#include "FunctionRegistry.hpp"

namespace fs = std::filesystem;

namespace Datum
{
using namespace MATH;

namespace
{
const char * kSource = "HEPDataPlan";

std::string trim(const std::string & s)
{
  std::size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a==std::string::npos ? std::string() : s.substr(a,b-a+1);
}

std::string lower(std::string s)
{
  for (char & c : s) c = (char) std::tolower((unsigned char) c);
  return s;
}

bool endsWith(const std::string & s, const std::string & end)
{
  return s.size()>=end.size() && lower(s.substr(s.size()-end.size()))==end;
}

//! The text shortened to a number of characters (on one line), with "..." if cut.
String shortened(const String & text, std::size_t length=240)
{
  std::string s = text.str();
  for (char & c : s) if (c=='\n' || c=='\r' || c=='\t') c = ' ';
  s = trim(s);
  if (s.size()>length) s = s.substr(0,length-3)+"...";
  return String(s);
}

//! "name [units]", or name.
std::string variableHeader(const HEPDataVariable & v)
{
  std::string s = v.name.isNull() ? std::string("(no name)") : v.name.str();
  if (!v.units.isNull()) s += " ["+v.units.str()+"]";
  return s;
}

//! Values separated by commas, trimmed, the empty ones dropped.
std::vector<String> split(const String & text, char separator=',')
{
  std::vector<String> parts;
  std::stringstream in(text.str());
  std::string part;
  while (std::getline(in,part,separator)) { part = trim(part); if (!part.empty()) parts.push_back(String(part)); }
  return parts;
}

std::string errorText(const HEPDataError & e, double value)
{
  std::string label = e.label.isNull() ? std::string() : " ("+e.label.str()+")";
  if (e.kind==HEPDataError::kSymmetric)
    return std::string("\xC2\xB1")+HEPDataPreview::number(e.upper(value)).str()+label;
  return "+"+HEPDataPreview::number(e.upper(value)).str()+"/-"+HEPDataPreview::number(e.lower(value)).str()+label;
}

std::string valueText(const HEPDataValue & v)
{
  std::string s;
  if (v.isBin()) s = "["+HEPDataPreview::number(v.low).str()+", "+HEPDataPreview::number(v.high).str()+"]";
  if (v.hasValue)
    {
    std::string value = v.numeric ? HEPDataPreview::number(v.value).str() : v.text.str();
    s = s.empty() ? value : s+" at "+value;
    }
  if (s.empty()) s = "(none)";
  for (const HEPDataError & e : v.errors) s += " "+errorText(e,v.value);
  return s;
}

//! The range and binning of an independent variable, as text.
std::string variableText(const HEPDataVariable & v)
{
  std::string s = variableHeader(v)+": ";
  unsigned int n = v.size();
  if (n==0) return s+"no rows";
  double low = INFINITY, high = -INFINITY;
  unsigned int nonNumeric = 0;
  if (v.isBinned())
    {
    double width = v.values[0].high-v.values[0].low;
    bool equal = true;
    for (const HEPDataValue & x : v.values)
      {
      low = std::min(low,x.low);
      high = std::max(high,x.high);
      if (std::fabs((x.high-x.low)-width) > 1.0E-6*std::fabs(width)) equal = false;
      }
    s += std::to_string(n)+(n==1 ? " bin" : " bins")+" from "+HEPDataPreview::number(low).str()+" to "+HEPDataPreview::number(high).str();
    s += equal ? " (equal widths)" : " (variable widths)";
    return s;
    }
  for (const HEPDataValue & x : v.values)
    {
    if (!x.isNumeric() && !x.isBin()) { nonNumeric++; continue; }
    double c = x.center();
    if (std::isnan(c)) { nonNumeric++; continue; }
    low = std::min(low,c);
    high = std::max(high,c);
    }
  s += std::to_string(n)+(n==1 ? " value" : " values");
  if (nonNumeric<n) s += " from "+HEPDataPreview::number(low).str()+" to "+HEPDataPreview::number(high).str();
  if (nonNumeric>0) s += ", "+std::to_string(nonNumeric)+" not numbers";
  return s;
}

//! The errors of a dependent variable: labels, symmetric or asymmetric, percentages.
std::string errorsText(const HEPDataVariable & v)
{
  std::vector<String> labels = v.errorLabels();
  if (labels.empty()) return "no uncertainties";
  std::string s;
  for (const String & label : labels)
    {
    bool symmetric = false, asymmetric = false, percent = false;
    for (const HEPDataValue & x : v.values)
      for (const HEPDataError & e : x.errors)
        {
        if (!(e.label==label)) continue;
        (e.kind==HEPDataError::kSymmetric ? symmetric : asymmetric) = true;
        percent = percent || e.percent;
        }
    if (!s.empty()) s += ", ";
    s += label.isNull() ? std::string("(unlabelled)") : label.str();
    std::string kind = symmetric && asymmetric ? "symmetric and asymmetric" : (asymmetric ? "asymmetric" : "symmetric");
    s += " ("+kind+(percent ? ", %" : "")+")";
    }
  return "uncertainties "+s;
}

//! Add a copy of an object to a folder (with its own class, for the folder to save it as such).
void addCopy(Folder & folder, const DataPoints & object, const String & name)
{
  if (auto h = dynamic_cast<const HistogramSyst *>(&object))         folder.add(new HistogramSyst(*h),name);
  else if (auto p = dynamic_cast<const Profile *>(&object))          folder.add(new Profile(*p),name);
  else if (auto g = dynamic_cast<const Histogram *>(&object))        folder.add(new Histogram(*g),name);
  else if (auto v = dynamic_cast<const DataPointsVector *>(&object)) folder.add(new DataPointsVector(*v),name);
  else if (auto c = dynamic_cast<const DataPointColumns *>(&object)) folder.add(new DataPointColumns(*c),name);
  else throw Exception(String("Cannot add an object of class ")+object.className()+" to a folder",kSource);
}

//! A name usable in a folder: no '/', not empty.
String entryName(const String & name)
{
  std::string s = trim(name.str());
  for (char & c : s) if (c=='/') c = '_';
  return String(s.empty() ? std::string("object") : s);
}

//! A name not in a set (adding " (2)", " (3)", ... or "_2", ...), then put in the set.
String uniqueIn(std::set<std::string> & names, const String & name, const char * separator)
{
  std::string candidate = name.str();
  for (int k=2; names.count(candidate); k++)
    candidate = name.str()+(separator[0]=='(' ? " ("+std::to_string(k)+")" : std::string(separator)+std::to_string(k));
  names.insert(candidate);
  return String(candidate);
}

String functionClass(const ParameterizedFunction & f)
{
  try { return FunctionRegistry::typeName(f); } catch (const Exception &) { }
  return f.registryName().isNull() ? f.className() : f.registryName();
}
}

//////////////////////////////////////////////////////////////////////////
// HEPDataPreviewNode
//////////////////////////////////////////////////////////////////////////

HEPDataPreviewNode & HEPDataPreviewNode::add(const String & text, Kind kind, bool open)
{
  HEPDataPreviewNode n;
  n.text = text;
  n.kind = kind;
  n.open = open;
  children.push_back(n);
  return children.back();
}

unsigned int HEPDataPreviewNode::count() const
{
  unsigned int n = 1;
  for (const HEPDataPreviewNode & c : children) n += c.count();
  return n;
}

unsigned int HEPDataPreviewNode::count(Kind k) const
{
  unsigned int n = kind==k ? 1 : 0;
  for (const HEPDataPreviewNode & c : children) n += c.count(k);
  return n;
}

const HEPDataPreviewNode * HEPDataPreviewNode::find(const String & part) const
{
  if (text.str().find(part.str())!=std::string::npos) return this;
  for (const HEPDataPreviewNode & c : children) if (const HEPDataPreviewNode * f = c.find(part)) return f;
  return nullptr;
}

String HEPDataPreviewNode::toText(unsigned int depth) const
{
  static const char * prefixes[] = { "", "=> ", "warning: ", "error: " };
  std::string s = std::string(2*depth,' ')+"- "+prefixes[kind]+text.str()+"\n";
  for (const HEPDataPreviewNode & c : children) s += c.toText(depth+1).str();
  return String(s);
}

//////////////////////////////////////////////////////////////////////////
// HEPDataPreview
//////////////////////////////////////////////////////////////////////////

String HEPDataPreview::number(double x)
{
  if (std::isinf(x)) return String(x>0 ? "inf" : "-inf");
  char text[32];
  std::snprintf(text,sizeof(text),"%.6g",x);
  return String(text);
}

HEPDataPreviewNode HEPDataPreview::table(const HEPDataTable & t, unsigned int maxRows)
{
  HEPDataPreviewNode n;
  unsigned int rows = t.nRows();
  n.text = String("Table \""+(t.name.isNull() ? std::string("(no name)") : t.name.str())+"\""
                  +(t.dataFile.isNull() ? std::string() : " - "+t.dataFile.str())
                  +", "+std::to_string(rows)+(rows==1 ? " row" : " rows"));
  if (!t.description.isNull()) n.add(String("Description: "+shortened(t.description).str()));
  if (!t.location.isNull()) n.add(String("Location: "+shortened(t.location).str()));
  if (!t.keywords.empty())
    {
    HEPDataPreviewNode & k = n.add("Keywords");
    for (const HEPDataKeyword & keyword : t.keywords)
      {
      std::string values;
      for (const String & v : keyword.values) values += (values.empty() ? "" : ", ")+v.str();
      k.add(String(keyword.name.str()+": "+(values.empty() ? std::string("(none)") : values)));
      }
    }
  if (!t.licenseName.isNull())
    n.add(String("Licence: "+t.licenseName.str()+(t.licenseUrl.isNull() ? std::string() : " ("+t.licenseUrl.str()+")")));
  if (!t.resources.empty())
    {
    HEPDataPreviewNode & r = n.add("Additional resources");
    for (const HEPDataResource & resource : t.resources)
      r.add(String(resource.location.str()+(resource.description.isNull() ? std::string() : ": "+shortened(resource.description,120).str())));
    }
  for (const HEPDataVariable & v : t.independent) n.add(String("Independent variable "+variableText(v)));
  for (const HEPDataVariable & v : t.dependent)
    {
    HEPDataPreviewNode & d = n.add(String("Dependent variable "+variableHeader(v)+": "+errorsText(v)));
    for (const HEPDataQualifier & q : v.qualifiers)
      d.add(String("Qualifier "+q.name.str()+" = "+q.value.str()+(q.units.isNull() ? std::string() : " "+q.units.str())));
    }
  if (!t.datumBlock.isNull())
    n.add("Datum block: what HEPData cannot hold (class, binning, entries, fits, functions), read back exactly");
  if (maxRows>0 && rows>0)
    {
    unsigned int shown = std::min(rows,maxRows);
    HEPDataPreviewNode & r = n.add(String(shown<rows ? "First "+std::to_string(shown)+" rows" : std::string("Rows")));
    for (unsigned int i=0; i<shown; i++)
      {
      std::string s;
      for (const HEPDataVariable & v : t.independent)
        if (i<v.size()) s += (s.empty() ? "" : "; ")+(v.name.isNull() ? std::string("x") : v.name.str())+" = "+valueText(v.values[i]);
      for (const HEPDataVariable & v : t.dependent)
        if (i<v.size()) s += (s.empty() ? "" : "; ")+(v.name.isNull() ? std::string("y") : v.name.str())+" = "+valueText(v.values[i]);
      r.add(String(s));
      }
    }
  return n;
}

void HEPDataPreview::header(const HEPDataSubmission & s, HEPDataPreviewNode & n)
{
  const HEPDataPublication & p = s.publication;
  if (!p.isEmpty())
    {
    HEPDataPreviewNode & h = n.add("Publication (comment lines of submission.yaml)",HEPDataPreviewNode::kItem,true);
    const std::pair<const char *, const String *> fields[] = { {"Collaboration",&p.collaboration}, {"Title",&p.title},
      {"INSPIRE",&p.inspire}, {"arXiv",&p.arXiv}, {"DOI",&p.doi}, {"Journal",&p.journal}, {"Contact",&p.contact} };
    for (const auto & f : fields) if (!f.second->isNull()) h.add(String(std::string(f.first)+": "+f.second->str()));
    for (const String & note : p.notes) h.add(String("# "+note.str()));
    }
  if (!s.comment.isNull()) n.add(String("Comment: "+shortened(s.comment,400).str()));
  if (!s.resources.empty())
    {
    HEPDataPreviewNode & r = n.add("Additional resources of the submission");
    for (const HEPDataResource & resource : s.resources)
      r.add(String(resource.location.str()+(resource.description.isNull() ? std::string() : ": "+shortened(resource.description,120).str())));
    }
}

HEPDataPreviewNode HEPDataPreview::submission(const HEPDataSubmission & s, const String & title, unsigned int maxRows)
{
  HEPDataPreviewNode n;
  n.text = title;
  n.open = true;
  header(s,n);
  for (const HEPDataTable & t : s.tables) n.children.push_back(table(t,maxRows));
  return n;
}

String HEPDataPreview::describe(const DataPoints & o)
{
  std::string s;
  const Histogram * h = dynamic_cast<const Histogram *>(&o);
  if (dynamic_cast<const HistogramSyst *>(&o)) s = "HistogramSyst";
  else if (dynamic_cast<const Profile *>(&o))  s = "Profile";
  else if (h)                                  s = "Histogram";
  else                                         s = "DataPointsVector";
  if (!h && !dynamic_cast<const DataPointsVector *>(&o)) s = o.className().str();
  s += " \""+o.name().str()+"\"";
  if (!o.title().isNull() && !(o.title()==o.name())) s += " ("+o.title().str()+")";
  if (h)
    {
    s += ", "+std::to_string(h->rank())+"D: ";
    for (unsigned int k=0; k<h->rank(); k++)
      {
      if (k>0) s += " x ";
      String axis = h->dimensionTitle(k);
      s += std::to_string(h->nBins(k))+(h->isRegular(k) ? "" : " variable")+" bins";
      if (!axis.isNull()) s += " in "+axis.str();
      s += " ["+number(h->minimum(k)).str()+", "+number(h->maximum(k)).str()+"]";
      }
    }
  else
    s += ", "+std::to_string(o.size())+" points"+(o.rank()>1 ? " of rank "+std::to_string(o.rank()) : std::string());
  bool stat = false, syst = false, asymmetric = false;
  for (unsigned int k=0; k<o.size(); k++)
    {
    if (o.statErrorAt(k)>0.0) stat = true;
    double plus = o.sysErrorPlusAt(k), minus = o.sysErrorMinusAt(k);
    if (plus>0.0 || minus>0.0) syst = true;
    if (plus!=minus) asymmetric = true;
    if (stat && syst && asymmetric) break;
    }
  std::string errors;
  if (stat) errors = "stat";
  if (syst) errors += std::string(errors.empty() ? "" : " + ")+(asymmetric ? "asymmetric syst" : "syst");
  if (o.hasCommonSyst())
    {
    std::string common = o.hasAsymmetricCommonSyst()
      ? "+"+number(100.0*o.getCommonSystPlus()).str()+"/-"+number(100.0*o.getCommonSystMinus()).str()+"%"
      : number(100.0*o.getCommonSyst()).str()+"%";
    errors += std::string(errors.empty() ? "" : " + ")+"common syst "+common;
    }
  s += "; "+(errors.empty() ? std::string("no uncertainties") : errors);
  if (o.nFits()>0) s += "; "+std::to_string(o.nFits())+(o.nFits()==1 ? " fit" : " fits");
  return String(s);
}

String HEPDataPreview::describe(const ParameterizedFunction & f)
{
  std::string s = "Function "+functionClass(f).str()+" \""+f.name().str()+"\" (";
  for (unsigned int k=0; k<f.nParameters(); k++)
    {
    if (k>0) s += ", ";
    s += f.parameter(k).name().str()+" = "+number(f.parameter(k).value()).str();
    }
  return String(s+")");
}

//////////////////////////////////////////////////////////////////////////
// HEPDataImportPlan
//////////////////////////////////////////////////////////////////////////

std::vector<HEPDataImportPlan::Source> HEPDataImportPlan::scan(const String & path)
{
  std::vector<Source> sources;
  fs::path p(path.str());
  std::error_code error;
  auto stem = [](const fs::path & f)
    {
    std::string name = f.filename().string();
    for (const char * end : {".tar.gz",".tgz",".tar",".zip",".yaml",".yml",".json"})
      if (endsWith(name,end)) return name.substr(0,name.size()-std::string(end).size());
    return name;
    };
  auto isArchive = [](const std::string & name) { return endsWith(name,".tar.gz") || endsWith(name,".tgz") || endsWith(name,".tar") || endsWith(name,".zip"); };
  auto isYaml = [](const std::string & name) { return endsWith(name,".yaml") || endsWith(name,".yml") || endsWith(name,".json"); };
  if (!fs::is_directory(p,error))
    {
    Source s;
    s.path = path;
    std::string name = p.filename().string();
    s.kind = isArchive(name) ? "archive" : (lower(name)=="submission.yaml" ? "submission.yaml" : "data files");
    s.folder = String(s.kind=="submission.yaml" ? p.parent_path().filename().string() : stem(p));
    if (s.kind=="data files") s.files.push_back(path);
    sources.push_back(s);
    return sources;
    }
  if (fs::exists(p/"submission.yaml",error))
    {
    Source s;
    s.path = path;
    s.kind = "submission";
    s.folder = String(fs::absolute(p,error).lexically_normal().filename().string());
    if (s.folder.isNull()) s.folder = String(fs::absolute(p,error).parent_path().filename().string());
    sources.push_back(s);
    return sources;
    }
  std::vector<fs::path> entries;
  for (const fs::directory_entry & e : fs::directory_iterator(p,error)) entries.push_back(e.path());
  std::sort(entries.begin(),entries.end());
  Source loose;
  loose.path = path;
  loose.kind = "data files";
  loose.folder = String(fs::absolute(p,error).lexically_normal().filename().string());
  for (const fs::path & e : entries)
    {
    std::string name = e.filename().string();
    if (name.empty() || name[0]=='.') continue;
    if (fs::is_directory(e,error))
      {
      if (!fs::exists(e/"submission.yaml",error)) continue;
      Source s;
      s.path = String(e.string());
      s.kind = "submission";
      s.folder = String(name);
      sources.push_back(s);
      }
    else if (isArchive(name))
      {
      Source s;
      s.path = String(e.string());
      s.kind = "archive";
      s.folder = String(stem(e));
      sources.push_back(s);
      }
    else if (isYaml(name))
      loose.files.push_back(String(e.string()));
    }
  if (!loose.files.empty()) sources.push_back(loose);
  return sources;
}

unsigned int HEPDataImportPlan::prepare(const String & path, const HEPDataConverter::ImportOptions & options)
{
  _path = path;
  _sources = scan(path);
  for (Source & s : _sources)
    {
    // read
    if (s.kind=="data files")
      for (const String & file : s.files)
        {
        try
          {
          HEPDataSubmission one = HEPDataSubmission::read(file);
          for (const HEPDataTable & t : one.tables) s.submission.tables.push_back(t);
          }
        catch (const std::exception & e)
          { s.errors.push_back(String(fs::path(file.str()).filename().string()+": "+e.what())); }
        }
    else
      {
      try { s.submission = HEPDataSubmission::read(s.path); }
      catch (const std::exception & e) { s.errors.push_back(String(e.what())); continue; }
      }
    // convert, table by table
    std::set<std::string> names;
    for (const HEPDataTable & t : s.submission.tables)
      {
      try
        {
        std::shared_ptr<ParameterizedFunction> f = HEPDataConverter::toFunction(t);
        if (f)
          {
          Object o;
          o.table = t.name;
          o.function = f;
          String name = f->name().isNull() || f->name()==String("Function") ? t.name : f->name();
          o.name = uniqueIn(names,entryName(name),"_");
          s.objects.push_back(o);
          continue;
          }
        std::vector<String> warnings;
        for (const HEPDataConverter::Imported & i : HEPDataConverter::toObjects(t,options,&warnings))
          {
          Object o;
          o.table = t.name;
          o.points = i.object;
          o.name = uniqueIn(names,entryName(i.object->name()),"_");
          s.objects.push_back(o);
          }
        for (const String & w : warnings)                          // each starting with the name of the table
          {
          bool prefixed = t.name.isNull() || w.str().compare(0,t.name.str().size()+1,t.name.str()+":")==0;
          s.warnings.push_back(prefixed ? w : String(t.name.str()+": "+w.str()));
          }
        }
      catch (const std::exception & e)
        {
        s.errors.push_back(String((t.name.isNull() ? std::string("a table") : t.name.str())+": "+e.what()));
        }
      }
    }
  return nObjects();
}

unsigned int HEPDataImportPlan::nObjects() const
{
  unsigned int n = 0;
  for (const Source & s : _sources) n += (unsigned int) s.objects.size();
  return n;
}

unsigned int HEPDataImportPlan::nErrors() const
{
  unsigned int n = 0;
  for (const Source & s : _sources) n += (unsigned int) s.errors.size();
  return n;
}

String HEPDataImportPlan::name() const
{
  fs::path p(_path.str());
  std::error_code error;
  std::string n = fs::absolute(p,error).lexically_normal().filename().string();
  if (n.empty()) n = fs::absolute(p,error).parent_path().filename().string();
  for (const char * end : {".tar.gz",".tgz",".tar",".zip",".yaml",".yml",".json"})
    if (endsWith(n,end)) { n = n.substr(0,n.size()-std::string(end).size()); break; }
  return String(n.empty() ? std::string("HEPData") : n);
}

HEPDataPreviewNode HEPDataImportPlan::preview(unsigned int maxRows, const String & fileName) const
{
  HEPDataPreviewNode root;
  root.text = String("Import from "+_path.str());
  root.open = true;
  unsigned int n = nObjects();
  bool folders = _sources.size()>1;
  if (_sources.empty())
    root.add("No HEPData submission, archive or YAML data file found here",HEPDataPreviewNode::kError);
  else if (n==0)
    root.add("Nothing to import: no table could be converted",HEPDataPreviewNode::kError);
  else
    root.add(String("OK makes a data file \""+(fileName.isNull() ? name() : fileName).str()+"\" with "+std::to_string(n)+(n==1 ? " object" : " objects")
                    +(folders ? " in "+std::to_string(_sources.size())+" folders (one per submission)" : std::string())),
             HEPDataPreviewNode::kAction);
  for (const Source & s : _sources)
    {
    std::string what = s.kind=="data files" ? std::to_string(s.files.size())+" loose data "+(s.files.size()==1 ? "file" : "files")
                                            : s.kind.str();
    HEPDataPreviewNode & node = root.add(String("\""+s.folder.str()+"\" - "+what+": "+s.path.str()),HEPDataPreviewNode::kItem,true);
    if (folders && !s.objects.empty())
      node.add(String("Objects go in the folder \""+entryName(s.folder).str()+"\""),HEPDataPreviewNode::kAction);
    for (const String & e : s.errors) node.add(e,HEPDataPreviewNode::kError);
    HEPDataPreview::header(s.submission,node);
    for (const HEPDataTable & t : s.submission.tables)
      {
      HEPDataPreviewNode table = HEPDataPreview::table(t,maxRows);
      table.open = true;
      std::vector<HEPDataPreviewNode> actions;
      for (const Object & o : s.objects)
        {
        if (!(o.table==t.name)) continue;
        HEPDataPreviewNode a;
        a.kind = HEPDataPreviewNode::kAction;
        std::string what = o.points ? HEPDataPreview::describe(*o.points).str() : HEPDataPreview::describe(*o.function).str();
        a.text = String("makes "+what+(o.points && !(o.points->name()==o.name) ? ", named \""+o.name.str()+"\"" : std::string()));
        actions.push_back(a);
        }
      bool failed = false;
      for (const String & e : s.errors) if (!t.name.isNull() && e.str().compare(0,t.name.str().size()+1,t.name.str()+":")==0) failed = true;
      if (actions.empty() && !failed)
        {
        HEPDataPreviewNode a;
        a.kind = HEPDataPreviewNode::kWarning;
        a.text = "makes nothing (no dependent variable)";
        actions.push_back(a);
        }
      for (const String & w : s.warnings)
        if (!t.name.isNull() && w.str().compare(0,t.name.str().size()+1,t.name.str()+":")==0)
          {
          HEPDataPreviewNode a;
          a.kind = HEPDataPreviewNode::kWarning;
          a.text = String(trim(w.str().substr(t.name.str().size()+1)));
          actions.push_back(a);
          }
      table.children.insert(table.children.begin(),actions.begin(),actions.end());
      node.children.push_back(table);
      }
    }
  return root;
}

unsigned int HEPDataImportPlan::addTo(Folder & folder) const
{
  unsigned int n = 0;
  bool folders = _sources.size()>1;
  for (const Source & s : _sources)
    {
    if (s.objects.empty()) continue;
    Folder * target = &folder;
    if (folders) target = folder.addFolder(folder.uniqueName(entryName(s.folder)));
    for (const Object & o : s.objects)
      {
      String name = target->uniqueName(o.name);
      if (o.points) addCopy(*target,*o.points,name);
      else
        {
        ParameterizedFunction * f = o.function->clone();
        target->add<ParameterizedFunction>(f,name);
        }
      n++;
      }
    }
  return n;
}

//////////////////////////////////////////////////////////////////////////
// HEPDataExportPlan
//////////////////////////////////////////////////////////////////////////

std::vector<HEPDataExportPlan::Item> HEPDataExportPlan::itemsOf(const Folder & folder, const String & source,
                                                                 std::shared_ptr<const void> owner)
{
  std::vector<Item> items;
  for (const String & path : folder.paths())
    {
    Item item;
    item.source = source;
    item.path = path;
    item.owner = owner;
    if (const DataPoints * p = folder.get<DataPoints>(path))
      {
      bool empty = true;                                          // no value and no uncertainty anywhere
      for (unsigned int k=0; k<p->size() && empty; k++) empty = p->valueAt(k)==0.0 && p->statErrorAt(k)==0.0 && p->sysErrorAt(k)==0.0;
      if (empty) continue;
      item.points = p;
      item.tableName = p->name().isNull() ? path : p->name();
      }
    else if (const ParameterizedFunction * f = folder.get<ParameterizedFunction>(path))
      {
      if (f->rank()!=1) continue;
      item.function = f;
      item.tableName = f->name().isNull() ? path : f->name();
      }
    else continue;
    items.push_back(item);
    }
  return items;
}

std::vector<HEPDataQualifier> HEPDataExportPlan::parseQualifiers(const String & text)
{
  std::vector<HEPDataQualifier> qualifiers;
  for (const String & part : split(text,';'))
    {
    std::string s = part.str();
    std::size_t equal = s.find('=');
    if (equal==std::string::npos) continue;
    HEPDataQualifier q;
    q.name = String(trim(s.substr(0,equal)));
    std::string value = trim(s.substr(equal+1));
    std::size_t open = value.rfind('[');
    if (open!=std::string::npos && !value.empty() && value.back()==']')
      {
      q.units = String(trim(value.substr(open+1,value.size()-open-2)));
      value = trim(value.substr(0,open));
      }
    q.value = String(value);
    if (q.name.isNull() || q.value.isNull()) continue;
    qualifiers.push_back(q);
    }
  return qualifiers;
}

unsigned int HEPDataExportPlan::nIncluded() const
{
  unsigned int n = 0;
  for (const Item & i : items) if (i.include && (i.points || i.function)) n++;
  return n;
}

namespace
{
//! A field of an item, or of the defaults if empty.
const String & either(const String & a, const String & b) { return a.isNull() ? b : a; }

//! Set the metadata of a table from the fields of an item (and of the defaults).
void setInfo(HEPDataTable & t, const HEPDataExportPlan::TableInfo & item, const HEPDataExportPlan::TableInfo & defaults,
             const String & description)
{
  t.description = either(item.description,either(description,defaults.description));
  t.location = either(item.location,defaults.location);
  const std::pair<const char *, String HEPDataExportPlan::TableInfo::*> keywords[] = {
    {"observables",&HEPDataExportPlan::TableInfo::observables}, {"reactions",&HEPDataExportPlan::TableInfo::reactions},
    {"cmenergies",&HEPDataExportPlan::TableInfo::cmenergies}, {"phrases",&HEPDataExportPlan::TableInfo::phrases} };
  for (const auto & k : keywords)
    {
    std::vector<String> values = split(either(item.*(k.second),defaults.*(k.second)));
    if (!values.empty()) t.setKeyword(k.first,values);
    }
  t.licenseName = either(item.licenseName,defaults.licenseName);
  t.licenseUrl = either(item.licenseUrl,defaults.licenseUrl);
}
}

HEPDataSubmission HEPDataExportPlan::build(std::vector<String> * errors, std::vector<String> * origins) const
{
  HEPDataSubmission s;
  s.publication = publication;
  String summary = publicationInComment ? publication.summary() : String();
  if (!summary.isNull() && !comment.isNull()) s.comment = String(summary.str()+"\n\n"+comment.str());
  else s.comment = summary.isNull() ? comment : summary;
  s.resources = resources;
  std::set<std::string> names;
  for (const Item & item : items)
    {
    if (!item.include || (!item.points && !item.function)) continue;
    String where = item.source.isNull() ? item.path : String(item.source.str()+": "+item.path.str());
    try
      {
      std::vector<HEPDataQualifier> qualifiers = parseQualifiers(either(item.info.qualifiers,defaults.qualifiers));
      HEPDataTable t;
      String description;
      if (item.points)
        {
        HEPDataConverter::ExportOptions o;
        o.lossless = lossless;
        o.withFits = item.fits;
        if (!qualifiers.empty()) o.qualifiers = {qualifiers};
        t = HEPDataConverter::toTable(*item.points,o);
        description = item.points->title().isNull() ? item.points->name() : item.points->title();
        }
      else
        {
        HEPDataConverter::FunctionOptions o;
        o.nPoints = item.nPoints;
        o.lossless = lossless;
        t = HEPDataConverter::functionTable(*item.function,item.low,item.high,o);
        for (const HEPDataQualifier & q : qualifiers) t.dependent[0].qualifiers.push_back(q);
        description = String("Function "+functionClass(*item.function).str()+" \""+item.function->name().str()+"\"");
        }
      if (!item.xName.isNull() && !t.independent.empty()) { t.independent[0].name = item.xName; t.independent[0].units = item.xUnits; }
      if (!item.yName.isNull() && !t.dependent.empty())   { t.dependent[0].name = item.yName;   t.dependent[0].units = item.yUnits; }
      t.name = uniqueIn(names,item.tableName.isNull() ? item.path : item.tableName,"(");
      setInfo(t,item.info,defaults,description);
      t.dataFile = String();
      s.addTable(t);
      if (origins)
        origins->push_back(String("From "+where.str()+" - "+(item.points ? HEPDataPreview::describe(*item.points).str()
                                                                         : HEPDataPreview::describe(*item.function).str())));
      if (item.points && item.fits)
        {
        HEPDataConverter::FunctionOptions o;
        o.lossless = lossless;
        std::vector<HEPDataTable> fits = HEPDataConverter::fitTables(*item.points,o);
        for (unsigned int k=0; k<fits.size(); k++)
          {
          HEPDataTable & f = fits[k];
          const FitRecord & fit = item.points->fit(k);
          f.name = uniqueIn(names,String(t.name.str()+" fit"+(fits.size()>1 ? " "+std::to_string(k+1) : std::string())),"(");
          String what = String("Fit"+(fit.label.isNull() ? std::string() : " \""+fit.label.str()+"\"")+" of table \""+t.name.str()+"\"");
          HEPDataExportPlan::TableInfo info = item.info;
          info.description = String();
          setInfo(f,info,defaults,what);
          for (const HEPDataQualifier & q : qualifiers) f.dependent[0].qualifiers.push_back(q);
          f.dataFile = String();
          s.addTable(f);
          if (origins) origins->push_back(String("From fit "+std::to_string(k)+(fit.label.isNull() ? std::string() : " \""+fit.label.str()+"\"")
                                                 +" of "+where.str()));
          }
        }
      }
    catch (const std::exception & e)
      {
      if (errors) errors->push_back(String(where.str()+": "+e.what()));
      }
    }
  return s;
}

std::vector<String> HEPDataExportPlan::files() const
{
  std::vector<String> f;
  if (directory.isNull()) return f;
  HEPDataSubmission s = build();
  fs::path d(directory.str());
  f.push_back(directory);
  f.push_back(String((d/"submission.yaml").string()));
  for (const HEPDataTable & t : s.tables) f.push_back(String((d/t.dataFile.str()).string()));
  if (!archive.isNull()) f.push_back(archive);
  return f;
}

HEPDataPreviewNode HEPDataExportPlan::preview(unsigned int maxRows) const
{
  HEPDataPreviewNode root;
  root.text = String("Export to "+(directory.isNull() ? std::string("(no directory chosen)") : directory.str()));
  root.open = true;
  std::vector<String> errors, origins;
  HEPDataSubmission s = build(&errors,&origins);
  unsigned int n = (unsigned int) s.tables.size();
  bool ok = true;
  if (directory.isNull()) { root.add("Choose the directory to write to",HEPDataPreviewNode::kError); ok = false; }
  if (nIncluded()==0) { root.add("No object chosen",HEPDataPreviewNode::kError); ok = false; }
  else if (n==0) { root.add("None of the objects chosen can be written",HEPDataPreviewNode::kError); ok = false; }
  if (ok)
    root.add(String("OK writes submission.yaml and "+std::to_string(n)+(n==1 ? " data file" : " data files")+" in "
                    +directory.str()+(archive.isNull() ? std::string() : ", and the archive "+archive.str())),
             HEPDataPreviewNode::kAction);
  root.add(lossless ? "Lossless: the data files keep a datum block, from which Datum reads the objects back exactly "
                      "(hepdata.net ignores it)"
                    : "Strict: plain HEPData files, for a submission to hepdata.net",
           HEPDataPreviewNode::kItem);
  std::error_code error;
  if (!directory.isNull() && fs::exists(fs::path(directory.str())/"submission.yaml",error))
    root.add(String("A submission is already in "+directory.str()+": its submission.yaml and data files of the same "
                    "names are replaced"),HEPDataPreviewNode::kWarning);
  if (!archive.isNull() && fs::exists(fs::path(archive.str()),error))
    root.add(String("The archive "+archive.str()+" is replaced"),HEPDataPreviewNode::kWarning);
  for (const String & e : errors) root.add(String("Left out: "+e.str()),HEPDataPreviewNode::kError);
  std::vector<String> problems = s.problems();
  if (!problems.empty())
    {
    HEPDataPreviewNode & p = root.add(String(std::to_string(problems.size())+" problem"+(problems.size()==1 ? "" : "s")
                                             +" that hepdata.net would reject"),HEPDataPreviewNode::kWarning,true);
    for (const String & problem : problems) p.add(problem,HEPDataPreviewNode::kWarning);
    }
  if (ok)
    {
    HEPDataPreviewNode & files = root.add("Files");
    files.add("submission.yaml");
    for (const HEPDataTable & t : s.tables) files.add(t.dataFile);
    if (!archive.isNull()) files.add(archive);
    }
  HEPDataPreviewNode & yaml = root.add("submission.yaml",HEPDataPreviewNode::kItem,true);
  HEPDataPreview::header(s,yaml);
  if (s.publication.isEmpty() && s.comment.isNull()) yaml.add("No publication and no comment");
  for (const HEPDataTable & t : s.tables) yaml.children.push_back(HEPDataPreview::table(t,maxRows));
  for (unsigned int k=0; k<s.tables.size() && k<origins.size(); k++)
    {
    HEPDataPreviewNode & node = yaml.children[yaml.children.size()-s.tables.size()+k];
    node.children.insert(node.children.begin(),HEPDataPreviewNode{origins[k],HEPDataPreviewNode::kItem,false,{}});
    }
  return root;
}

HEPDataSubmission HEPDataExportPlan::write() const
{
  if (directory.isNull()) throw Exception("No directory to write the HEPData submission to",kSource);
  HEPDataSubmission s = build();
  if (s.tables.empty()) throw Exception("Nothing to write: no object can be written",kSource);
  s.write(directory,archive,digits,lossless);
  return s;
}

} // namespace Datum
