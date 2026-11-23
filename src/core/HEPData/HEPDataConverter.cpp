/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
#include "HEPDataConverter.hpp"
#include "Exceptions.hpp"
#include "Histogram.hpp"
#include "HistogramSyst.hpp"
#include "Profile.hpp"
#include "DataPoint2D.hpp"
#include "DataPoint3D.hpp"
#include "DataPointND.hpp"
#include "DataPointsVector.hpp"
#include "DataPointColumns.hpp"
#include "HEPDataBase64.hpp"
#include "BinaryIO.hpp"
#include "FitRecord.hpp"
#include "yaml-cpp/yaml.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <map>
#include <string>

namespace Datum
{

using namespace MATH;

namespace
{
const char * kSource = "HEPDataConverter";

std::string lower(const std::string & s)
{
  std::string l = s;
  std::transform(l.begin(),l.end(),l.begin(),[](unsigned char c){ return char(std::tolower(c)); });
  return l;
}

std::string number(double v)
{
  if (std::isinf(v)) return v>0 ? ".inf" : "-.inf";
  char buffer[64];
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

//////////////////////////////////////////////////////////////////////////
// Errors of a row
//////////////////////////////////////////////////////////////////////////

//! The uncertainties of one row, sorted into statistical, systematic and common ones (absolute sizes).
struct RowErrors
{
  double stat        = 0.0;   //!< statistical (larger side)
  bool   statAsym    = false; //!< the statistical uncertainty was asymmetric
  double systPlus    = 0.0;   //!< systematic, upper side
  double systMinus   = 0.0;   //!< systematic, lower side
  double commonPlus  = 0.0;   //!< common, upper side (absolute)
  double commonMinus = 0.0;   //!< common, lower side (absolute)
  bool   hasCommon   = false; //!< a common uncertainty was given
};

bool isCommon(const HEPDataError & e, const std::vector<String> & labels)
{
  std::string l = lower(e.label.str());
  for (const String & c : labels) if (lower(c.str())==l) return true;
  return false;
}

//! Whether the errors of a variable have no statistical label but one unlabelled error per row (taken as statistical).
bool unlabelledIsStatistical(const HEPDataVariable & y)
{
  bool any = false;
  for (const HEPDataValue & v : y.values)
    {
    for (const HEPDataError & e : v.errors) if (e.isStatistical()) return false;
    if (v.errors.size()>1) return false;
    if (v.errors.size()==1) { if (!v.errors[0].label.isNull()) return false; any = true; }
    }
  return any;
}

RowErrors sortErrors(const HEPDataValue & v, bool unlabelledStat, const std::vector<String> & commonLabels)
{
  RowErrors r;
  double value = v.isNumeric() ? v.value : 0.0;
  double s2p = 0, s2m = 0, y2p = 0, y2m = 0, c2p = 0, c2m = 0;
  for (const HEPDataError & e : v.errors)
    {
    double up = e.upper(value), down = e.lower(value);
    if (e.isStatistical() || (unlabelledStat && e.label.isNull()))
      {
      s2p += up*up; s2m += down*down;
      if (up!=down) r.statAsym = true;
      }
    else if (isCommon(e,commonLabels))
      {
      c2p += up*up; c2m += down*down;
      r.hasCommon = true;
      }
    else { y2p += up*up; y2m += down*down; }
    }
  r.stat        = std::sqrt(std::max(s2p,s2m));
  r.systPlus    = std::sqrt(y2p);
  r.systMinus   = std::sqrt(y2m);
  r.commonPlus  = std::sqrt(c2p);
  r.commonMinus = std::sqrt(c2m);
  return r;
}

//! The common systematic uncertainty of a variable: the relative sizes averaged over the rows with a non-zero value.
void commonOf(const HEPDataVariable & y, const std::vector<RowErrors> & errors, double tolerance,
              double & plus, double & minus, const String & where, std::vector<String> * warnings)
{
  double sp = 0, sm = 0, minP = std::numeric_limits<double>::max(), maxP = 0, minM = minP, maxM = 0;
  unsigned int n = 0;
  for (unsigned int k=0; k<y.values.size(); k++)
    {
    const HEPDataValue & v = y.values[k];
    if (!v.isNumeric() || v.value==0.0 || !errors[k].hasCommon) continue;
    double a = std::fabs(v.value);
    double p = errors[k].commonPlus/a, m = errors[k].commonMinus/a;
    sp += p; sm += m; n++;
    minP = std::min(minP,p); maxP = std::max(maxP,p);
    minM = std::min(minM,m); maxM = std::max(maxM,m);
    }
  plus = minus = 0.0;
  if (n==0) return;
  plus = sp/n;
  minus = sm/n;
  double spread = std::max(plus>0 ? (maxP-minP)/plus : 0.0, minus>0 ? (maxM-minM)/minus : 0.0);
  if (warnings && spread>tolerance)
    {
    char text[256];
    std::snprintf(text,sizeof(text),": the relative common systematic uncertainty varies from %.3g%% to %.3g%%; the average %.3g%% is used",
                  100.0*minP,100.0*maxP,100.0*plus);
    warnings->push_back(where+String(text));
    }
}

//////////////////////////////////////////////////////////////////////////
// Binning of independent variables
//////////////////////////////////////////////////////////////////////////

//! An axis of bins: their edges (regular: equal widths), the bin of each row.
struct Axis
{
  std::vector<double> edges;
  bool regular = true;
  std::vector<unsigned int> bin;
  unsigned int n() const { return (unsigned int) edges.size()-1; }
};

//!
//! Whether the rows of x are bins that do not overlap: the edges of the axis are those of the rows (a gap between two
//! rows is an empty bin); regular if all the bins have the same width. offCenter is set if a row has a value that is
//! not the middle of its bin.
//!
bool binnedAxis(const HEPDataVariable & x, double tolerance, Axis & axis, bool & offCenter)
{
  if (!x.isBinned()) return false;
  std::vector<double> edges;
  for (const HEPDataValue & v : x.values)
    {
    if (std::isinf(v.low) || std::isinf(v.high) || !(v.high>v.low)) return false;
    if (v.hasValue && !v.numeric) return false;
    edges.push_back(v.low);
    edges.push_back(v.high);
    }
  std::sort(edges.begin(),edges.end());
  edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
  axis.bin.clear();
  for (const HEPDataValue & v : x.values)
    {
    std::size_t i = std::lower_bound(edges.begin(),edges.end(),v.low)-edges.begin();
    if (i+1>=edges.size() || edges[i+1]!=v.high) return false;   // the bin overlaps others
    axis.bin.push_back((unsigned int) i);
    double width = v.high-v.low;
    if (v.isNumeric() && std::fabs(v.value-0.5*(v.low+v.high))>tolerance*width) offCenter = true;
    }
  double width = edges[1]-edges[0];
  axis.regular = true;
  for (std::size_t i=1; i+1<edges.size(); i++)
    if (std::fabs((edges[i+1]-edges[i])-width)>tolerance*width) axis.regular = false;
  axis.edges = edges;
  return true;
}

//! Coordinate of a row: its value, else the middle of its bin, else NaN.
void coordinate(const HEPDataValue & v, double & x, double & error)
{
  error = 0.0;
  x = v.center();
  if (v.isBin() && !std::isinf(v.low) && !std::isinf(v.high)) error = 0.5*(v.high-v.low);
}

//////////////////////////////////////////////////////////////////////////
// The datum block
//////////////////////////////////////////////////////////////////////////

//! What the datum block records of one object.
struct Recorded
{
  bool   found = false;
  std::string className, name, title, precision;
  struct RecordedAxis { unsigned int bins = 0; double min = 0, max = 0; std::string title; std::vector<double> edges; };
  std::vector<RecordedAxis> axes;
  std::vector<long> entries;
  bool hasCommon = false;
  double commonPlus = 0, commonMinus = 0;
  std::string fits;   //!< the fits attached, binary (FitList::encodeBinary()) in base64
};

Recorded recorded(const HEPDataTable & table, unsigned int column, const String & where)
{
  Recorded r;
  if (table.datumBlock.isNull()) return r;
  YAML::Node block;
  try { block = YAML::Load(table.datumBlock.str()); }
  catch (const YAML::Exception & e) { throw FileException(where,String("invalid datum block: ")+e.what(),kSource); }
  YAML::Node objects = block["objects"];
  if (!objects || !objects.IsSequence()) return r;
  try
    {
    for (const YAML::Node & o : objects)
      {
      if (!o["column"] || o["column"].as<unsigned int>()!=column) continue;
      r.found = true;
      if (o["class"])     r.className = o["class"].as<std::string>();
      if (o["name"])      r.name      = o["name"].as<std::string>();
      if (o["title"])     r.title     = o["title"].as<std::string>();
      if (o["precision"]) r.precision = o["precision"].as<std::string>();
      if (o["axes"] && o["axes"].IsSequence())
        for (const YAML::Node & a : o["axes"])
          {
          Recorded::RecordedAxis axis;
          axis.bins = a["bins"].as<unsigned int>();
          axis.min  = a["min"].as<double>();
          axis.max  = a["max"].as<double>();
          if (a["title"]) axis.title = a["title"].as<std::string>();
          if (a["edges"] && a["edges"].IsSequence()) for (const YAML::Node & e : a["edges"]) axis.edges.push_back(e.as<double>());
          r.axes.push_back(axis);
          }
      if (o["entries"] && o["entries"].IsSequence())
        for (const YAML::Node & e : o["entries"]) r.entries.push_back(e.as<long>());
      if (o["fits"]) r.fits = o["fits"].as<std::string>();
      if (o["commonSyst"] && o["commonSyst"].IsSequence() && o["commonSyst"].size()==2)
        {
        r.hasCommon = true;
        r.commonPlus  = o["commonSyst"][0].as<double>();
        r.commonMinus = o["commonSyst"][1].as<double>();
        }
      break;
      }
    }
  catch (const YAML::Exception & e) { throw FileException(where,String("invalid datum block: ")+e.what(),kSource); }
  return r;
}

//////////////////////////////////////////////////////////////////////////
// Import
//////////////////////////////////////////////////////////////////////////

String objectName(const HEPDataTable & table, unsigned int column)
{
  std::string base = table.dataFile.isNull() ? table.name.str() : std::filesystem::path(table.dataFile.str()).stem().string();
  if (base.empty()) base = "hepdata";
  for (char & c : base) if (c=='/' || c==' ') c = '_';
  if (table.dependent.size()>1) base += "_" + std::to_string(column);
  return String(base);
}

bool anySyst(const std::vector<RowErrors> & errors)
{
  for (const RowErrors & e : errors) if (e.systPlus!=0.0 || e.systMinus!=0.0) return true;
  return false;
}

std::shared_ptr<DataPoints> makeHistogram(const std::vector<Axis> & axes, const std::vector<String> & axisTitles,
                                          const HEPDataVariable & y, const std::vector<RowErrors> & errors,
                                          const Recorded & record)
{
  Partition partition;
  if (record.found && !record.axes.empty())
    for (const Recorded::RecordedAxis & a : record.axes)
      {
      if (!a.edges.empty()) partition.addDimension(a.edges,String(a.title));
      else                  partition.addDimension(a.bins,float(a.min),float(a.max),String(a.title));
      }
  else
    for (unsigned int a=0; a<axes.size(); a++)
      {
      if (axes[a].regular) partition.addDimension(axes[a].n(),float(axes[a].edges.front()),float(axes[a].edges.back()),axisTitles[a]);
      else                 partition.addDimension(axes[a].edges,axisTitles[a]);
      }
  bool syst = record.found ? record.className=="HistogramSyst" : anySyst(errors);
  std::shared_ptr<Histogram> h(syst ? new HistogramSyst(partition) : new Histogram(partition));
  if (record.precision=="double") h->setPrecision(Histogram::kDouble);
  for (unsigned int r=0; r<y.size(); r++)
    {
    unsigned int k;
    if (record.found && !record.axes.empty()) k = r;   // lossless mode: every bin, in global order
    else
      {
      k = 0;
      unsigned int stride = 1;
      for (unsigned int a=0; a<axes.size(); a++) { k += stride*axes[a].bin[r]; stride *= axes[a].n(); }
      }
    if (k>=h->nBinsTotal()) throw Exception("More rows than bins in a table written in lossless mode",kSource);
    const HEPDataValue & v = y.values[r];
    long entries = (record.found && k<record.entries.size()) ? record.entries[k] : 1;
    if (v.isNumeric() && entries>0) h->setBinAtGlobal(k,entries,v.value,errors[r].stat);
    if (syst && v.isNumeric()) h->setBinSystAtGlobal(k,errors[r].systPlus,errors[r].systMinus);
    }
  return h;
}

std::shared_ptr<DataPoints> makePoints(const HEPDataTable & table, const HEPDataVariable & y,
                                       const std::vector<RowErrors> & errors, const Recorded & record,
                                       const String & where, std::vector<String> * warnings)
{
  unsigned int rank = std::max<unsigned int>(1,(unsigned int) table.independent.size());
  bool categories = false;
  std::shared_ptr<DataPointsVector> points(new DataPointsVector());
  for (unsigned int r=0; r<y.size(); r++)
    {
    const HEPDataValue & v = y.values[r];
    if (!v.isNumeric()) continue;
    std::vector<double> x(rank,0.0), e(rank,0.0);
    if (table.independent.empty()) x[0] = r;
    for (unsigned int a=0; a<table.independent.size(); a++)
      {
      coordinate(table.independent[a].values[r],x[a],e[a]);
      if (std::isnan(x[a])) { x[a] = r; categories = true; }
      }
    const RowErrors & er = errors[r];
    if (rank==1)
      {
      DataPoint2D p(x[0],e[0],v.value,er.stat,0.0);
      p.setSyst(er.systPlus,er.systMinus);
      points->addPoint(p);
      }
    else if (rank==2)
      {
      DataPoint3D p(x[0],e[0],x[1],e[1],v.value,er.stat,0.0);
      p.setSyst(er.systPlus,er.systMinus);
      points->addPoint(p);
      }
    else
      {
      DataPointND p(x,e,v.value,er.stat,0.0);
      p.setSyst(er.systPlus,er.systMinus);
      points->addPoint(p);
      }
    }
  if (categories && warnings)
    warnings->push_back(where+": an independent variable is not a number: the row index is used as coordinate");
  if (record.found && record.className=="DataPointColumns")
    {
    std::shared_ptr<DataPointColumns> columns(new DataPointColumns(rank));
    for (unsigned int k=0; k<points->size(); k++) columns->addPoint((*points)[k]);
    return columns;
    }
  return points;
}
}

void HEPDataConverter::splitTitle(const String & title, String & name, String & units)
{
  std::string t = title.str();
  while (!t.empty() && t.back()==' ') t.pop_back();
  units = String();
  name = String(t);
  if (t.empty() || t.back()!=')') return;
  int depth = 0;
  for (std::size_t i=t.size(); i-->0; )
    {
    if (t[i]==')') depth++;
    else if (t[i]=='(' && --depth==0)
      {
      if (i==0 || t[i-1]!=' ') return;   // f(x): not units
      name = String(t.substr(0,i-1));
      units = String(t.substr(i+1,t.size()-i-2));
      return;
      }
    }
}

String HEPDataConverter::joinTitle(const String & name, const String & units)
{
  if (units.isNull()) return name;
  return String(name.str()+" ("+units.str()+")");
}

std::vector<HEPDataConverter::Imported> HEPDataConverter::toObjects(const HEPDataTable & table, const ImportOptions & options,
                                                                    std::vector<String> * warnings)
{
  String where = table.name.isNull() ? table.dataFile : table.name;
  unsigned int n = table.nRows();
  for (const HEPDataVariable & v : table.independent)
    if (v.size()!=n) throw Exception(String("Table ")+where+": independent variables of different sizes",kSource);
  std::vector<Imported> result;
  for (unsigned int c=0; c<table.dependent.size(); c++)
    {
    const HEPDataVariable & y = table.dependent[c];
    if (!table.independent.empty() && y.size()!=n)
      throw Exception(String("Table ")+where+": dependent variable '"+y.name+"' has not as many values as the independent ones",kSource);
    String column = table.dependent.size()>1 ? where+" ["+y.name+"]" : where;
    bool unlabelledStat = unlabelledIsStatistical(y);
    std::vector<RowErrors> errors;
    bool statAsym = false;
    for (const HEPDataValue & v : y.values)
      {
      errors.push_back(sortErrors(v,unlabelledStat,options.commonLabels));
      if (errors.back().statAsym) statAsym = true;
      }
    if (statAsym && warnings)
      warnings->push_back(column+": asymmetric statistical uncertainties: the larger side is used");
    double commonPlus = 0, commonMinus = 0;
    commonOf(y,errors,options.commonTolerance,commonPlus,commonMinus,column,warnings);
    Recorded record = recorded(table,c,where);

    std::shared_ptr<DataPoints> object;
    std::vector<Axis> axes(table.independent.size());
    bool histogram = record.found ? (record.className=="Histogram" || record.className=="HistogramSyst")
                                  : (options.histograms && !table.independent.empty() && table.independent.size()<=3);
    bool offCenter = false;
    if (histogram && !record.found)
      for (unsigned int a=0; a<table.independent.size() && histogram; a++)
        histogram = binnedAxis(table.independent[a],options.binWidthTolerance,axes[a],offCenter);
    if (histogram && !record.found && offCenter)
      {
      if (options.keepPositions) histogram = false;
      else if (warnings)
        warnings->push_back(column+": the positions given within the bins are not kept by the histogram (bin centers"
                                   " are used); set ImportOptions::keepPositions for data points");
      }
    if (histogram && !record.found)
      {
      std::map<unsigned long long,int> used;   // one row per bin
      for (unsigned int r=0; r<n && histogram; r++)
        {
        unsigned long long k = 0, stride = 1;
        for (unsigned int a=0; a<axes.size(); a++) { k += stride*axes[a].bin[r]; stride *= axes[a].n(); }
        if (used[k]++) histogram = false;
        }
      }
    if (histogram)
      {
      std::vector<String> titles;
      for (const HEPDataVariable & x : table.independent) titles.push_back(joinTitle(x.name,x.units));
      object = makeHistogram(axes,titles,y,errors,record);
      }
    else
      object = makePoints(table,y,errors,record,column,warnings);
    object->setName(record.found && !record.name.empty() ? String(record.name) : objectName(table,c));
    object->setTitle(record.found ? String(record.title) : joinTitle(y.name,y.units));
    if (record.hasCommon) object->setCommonSyst(record.commonPlus,record.commonMinus);   // exact, from the datum block
    else object->setCommonSyst(commonPlus,commonMinus);
    if (!record.fits.empty())
      {
      std::string bytes = HEPDataBase64::decode(record.fits);
      BinaryReader reader(bytes.data(),bytes.size());
      FitList fits;
      unsigned int skipped = fits.decodeBinary(reader);
      for (unsigned int k=0; k<fits.size(); k++) object->addFit(fits.pointer(k));
      if (skipped>0 && warnings)
        warnings->push_back(column+": "+String(std::to_string(skipped))+" fit(s) skipped (their functions are not available)");
      }
    Imported imported;
    imported.object = object;
    imported.table = table.name;
    imported.column = c;
    imported.qualifiers = y.qualifiers;
    result.push_back(imported);
    }
  return result;
}

std::vector<HEPDataConverter::Imported> HEPDataConverter::toObjects(const HEPDataSubmission & submission, const ImportOptions & options,
                                                                    std::vector<String> * warnings)
{
  std::vector<Imported> result;
  for (const HEPDataTable & t : submission.tables)
    {
    std::vector<Imported> objects = toObjects(t,options,warnings);
    result.insert(result.end(),objects.begin(),objects.end());
    }
  return result;
}

//////////////////////////////////////////////////////////////////////////
// Export
//////////////////////////////////////////////////////////////////////////

namespace
{
void addErrors(HEPDataValue & v, double stat, double plus, double minus, const DataPoints & object,
               const HEPDataConverter::ExportOptions & options)
{
  if (stat>0.0) v.errors.push_back(HEPDataError::symmetric(stat,options.statLabel));
  if (plus>0.0 || minus>0.0)
    {
    if (plus==minus) v.errors.push_back(HEPDataError::symmetric(plus,options.systLabel));
    else             v.errors.push_back(HEPDataError::asymmetric(plus,-minus,options.systLabel));
    }
  if (object.hasCommonSyst())
    {
    double p = 100.0*object.getCommonSystPlus(), m = 100.0*object.getCommonSystMinus();
    if (p==m) v.errors.push_back(HEPDataError::symmetric(p,options.commonLabel,true));
    else      v.errors.push_back(HEPDataError::asymmetric(p,-m,options.commonLabel,true));
    }
}

const std::vector<HEPDataQualifier> & qualifiersOf(const HEPDataConverter::ExportOptions & options, unsigned int k)
{
  static const std::vector<HEPDataQualifier> none;
  if (options.qualifiers.empty()) return none;
  if (options.qualifiers.size()==1) return options.qualifiers[0];
  return k<options.qualifiers.size() ? options.qualifiers[k] : none;
}

String xName(const HEPDataConverter::ExportOptions & options, unsigned int a, const String & fallback)
{
  if (a<options.xNames.size()) return options.xNames[a];
  if (!fallback.isNull()) return fallback;
  static const char * names[] = {"x","y","z"};
  return a<3 ? String(names[a]) : String("x"+std::to_string(a));
}

void emitDatumBlock(HEPDataTable & table, const std::vector<const DataPoints *> & objects,
                    const HEPDataConverter::ExportOptions & options)
{
  YAML::Emitter out;
  out << YAML::BeginMap << YAML::Key << "version" << YAML::Value << 1;
  out << YAML::Key << "objects" << YAML::Value << YAML::BeginSeq;
  for (unsigned int c=0; c<objects.size(); c++)
    {
    const DataPoints & o = *objects[c];
    const Histogram * h = dynamic_cast<const Histogram *>(&o);
    std::string className = h ? (h->hasSystUncertainties() ? "HistogramSyst" : "Histogram")
                              : (dynamic_cast<const DataPointColumns *>(&o) ? "DataPointColumns" : "DataPointsVector");
    out << YAML::BeginMap;
    out << YAML::Key << "column" << YAML::Value << c;
    out << YAML::Key << "class" << YAML::Value << className;
    out << YAML::Key << "name" << YAML::Value << o.name().str();
    out << YAML::Key << "title" << YAML::Value << o.title().str();
    if (options.withFits && o.nFits()>0)
      {
      FitList fits;
      for (unsigned int k=0; k<o.nFits(); k++) fits.add(o.fitPointer(k));
      BinaryWriter w;
      fits.encodeBinary(w);
      out << YAML::Key << "fits" << YAML::Value << HEPDataBase64::encode(w.buffer());
      }
    if (o.hasCommonSyst())
      out << YAML::Key << "commonSyst" << YAML::Value << YAML::Flow << YAML::BeginSeq
          << number(o.getCommonSystPlus()) << number(o.getCommonSystMinus()) << YAML::EndSeq;
    if (h)
      {
      out << YAML::Key << "precision" << YAML::Value << (h->precision()==Histogram::kDouble ? "double" : "float");
      out << YAML::Key << "axes" << YAML::Value << YAML::BeginSeq;
      for (unsigned int a=0; a<h->rank(); a++)
        {
        out << YAML::Flow << YAML::BeginMap << YAML::Key << "bins" << YAML::Value << h->nBins(a)
            << YAML::Key << "min" << YAML::Value << number(h->minimum(a))
            << YAML::Key << "max" << YAML::Value << number(h->maximum(a))
            << YAML::Key << "title" << YAML::Value << h->dimensionTitle(a).str();
        if (!h->isRegular(a))
          {
          out << YAML::Key << "edges" << YAML::Value << YAML::Flow << YAML::BeginSeq;
          for (double e : h->binEdges(a)) out << number(e);
          out << YAML::EndSeq;
          }
        out << YAML::EndMap;
        }
      out << YAML::EndSeq;
      out << YAML::Key << "entries" << YAML::Value << YAML::Flow << YAML::BeginSeq;
      for (unsigned int k=0; k<h->nBinsTotal(); k++) out << h->binEntriesAtGlobal(k);
      out << YAML::EndSeq;
      }
    out << YAML::EndMap;
    }
  out << YAML::EndSeq << YAML::EndMap;
  table.datumBlock = String(std::string(out.c_str())+"\n");
}
}

HEPDataTable HEPDataConverter::toTable(const DataPoints & object, const ExportOptions & options)
{
  return toTable(std::vector<const DataPoints *>{&object},options);
}

HEPDataTable HEPDataConverter::toTable(const std::vector<Imported> & objects, ExportOptions options)
{
  std::vector<const DataPoints *> pointers;
  options.qualifiers.clear();
  for (const Imported & i : objects)
    {
    if (!i.object) throw Exception("An imported object is null",kSource);
    pointers.push_back(i.object.get());
    options.qualifiers.push_back(i.qualifiers);
    }
  HEPDataTable t = toTable(pointers,options);
  if (!objects.empty()) t.name = objects[0].table;
  return t;
}

HEPDataTable HEPDataConverter::toTable(const std::vector<const DataPoints *> & objects, const ExportOptions & options)
{
  if (objects.empty()) throw Exception("No object to export",kSource);
  for (const DataPoints * o : objects) if (!o) throw Exception("A null object to export",kSource);
  HEPDataTable table;
  const Histogram * first = dynamic_cast<const Histogram *>(objects[0]);

  if (first)
    {
    std::vector<const Histogram *> h;
    for (const DataPoints * o : objects)
      {
      const Histogram * hh = dynamic_cast<const Histogram *>(o);
      if (!hh) throw Exception("A histogram and data points cannot share a table",kSource);
      if (!hh->compatibleWith(*first)) throw Exception(String("Histogram ")+hh->name()+" has not the binning of "+first->name(),kSource);
      h.push_back(hh);
      }
    unsigned int rank = first->rank();
    bool keep = options.lossless || options.keepEmptyBins;
    std::vector<unsigned int> rows;
    for (unsigned int k=0; k<first->nBinsTotal(); k++)
      {
      bool empty = true;
      for (const Histogram * hh : h) if (hh->binEntriesAtGlobal(k)!=0 || hh->binValueAtGlobal(k)!=0.0) empty = false;
      if (!empty || keep) rows.push_back(k);
      }
    for (unsigned int a=0; a<rank; a++)
      {
      HEPDataVariable x;
      String name, units;
      splitTitle(first->dimensionTitle(a),name,units);
      x.name = xName(options,a,name);
      x.units = a<options.xNames.size() ? String() : units;
      for (unsigned int k : rows)
        {
        unsigned int rest = k, i = 0;
        for (unsigned int b=0; b<=a; b++) { i = rest%first->nBins(b); rest /= first->nBins(b); }
        x.values.push_back(HEPDataValue::bin(first->binLowEdge(i,a),first->binHighEdge(i,a)));
        }
      table.independent.push_back(x);
      }
    for (unsigned int c=0; c<h.size(); c++)
      {
      HEPDataVariable y;
      String name, units;
      splitTitle(h[c]->title(),name,units);
      if (c<options.yNames.size()) { y.name = options.yNames[c]; }
      else { y.name = name.isNull() ? h[c]->name() : name; y.units = units; }
      y.qualifiers = qualifiersOf(options,c);
      bool syst = h[c]->hasSystUncertainties();
      for (unsigned int k : rows)
        {
        bool empty = h[c]->binEntriesAtGlobal(k)==0 && h[c]->binValueAtGlobal(k)==0.0;
        if (empty) { y.values.push_back(HEPDataValue::textValue("-")); continue; }
        HEPDataValue v = HEPDataValue::number(h[c]->binValueAtGlobal(k));
        addErrors(v,h[c]->binUncertaintyAtGlobal(k),syst ? h[c]->binSystPlusAtGlobal(k) : 0.0,
                  syst ? h[c]->binSystMinusAtGlobal(k) : 0.0,*h[c],options);
        y.values.push_back(v);
        }
      table.dependent.push_back(y);
      }
    }
  else
    {
    const DataPoints & p0 = *objects[0];
    unsigned int rank = p0.rank(), n = p0.size();
    for (const DataPoints * o : objects)
      {
      if (dynamic_cast<const Histogram *>(o)) throw Exception("A histogram and data points cannot share a table",kSource);
      if (o->rank()!=rank || o->size()!=n) throw Exception(String("Data points ")+o->name()+" have not the coordinates of "+p0.name(),kSource);
      for (unsigned int k=0; k<n; k++)
        {
        std::vector<double> a = o->coordinatesAt(k), b = p0.coordinatesAt(k);
        for (unsigned int i=0; i<rank; i++)
          if (std::fabs(a[i]-b[i])>1.0E-6*std::max(1.0,std::fabs(b[i])))
            throw Exception(String("Data points ")+o->name()+" have not the coordinates of "+p0.name(),kSource);
        }
      }
    for (unsigned int a=0; a<rank; a++)
      {
      HEPDataVariable x;
      x.name = xName(options,a,String());
      for (unsigned int k=0; k<n; k++)
        {
        double c = p0.coordinatesAt(k)[a], e = p0.coordinateErrorsAt(k)[a];
        HEPDataValue v = HEPDataValue::number(c);
        if (e>0.0) { v.hasLow = v.hasHigh = true; v.low = c-e; v.high = c+e; }
        x.values.push_back(v);
        }
      table.independent.push_back(x);
      }
    for (unsigned int c=0; c<objects.size(); c++)
      {
      const DataPoints & o = *objects[c];
      HEPDataVariable y;
      String name, units;
      splitTitle(o.title(),name,units);
      if (c<options.yNames.size()) y.name = options.yNames[c];
      else { y.name = name.isNull() ? o.name() : name; y.units = units; }
      y.qualifiers = qualifiersOf(options,c);
      for (unsigned int k=0; k<n; k++)
        {
        HEPDataValue v = HEPDataValue::number(o.valueAt(k));
        addErrors(v,o.statErrorAt(k),o.sysErrorPlusAt(k),o.sysErrorMinusAt(k),o,options);
        y.values.push_back(v);
        }
      table.dependent.push_back(y);
      }
    }
  if (options.lossless) emitDatumBlock(table,objects,options);
  return table;
}

} // namespace Datum
