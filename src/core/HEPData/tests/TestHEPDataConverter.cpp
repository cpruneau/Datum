/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestHEPDataConverter.cpp
//! @brief Test program of HEPDataConverter: HEPData tables to Datum objects and back (run by ctest).
//
// TestHEPDataConverter <data directory> -- the ALICE record (tests/data): points for bins of different widths, the
// statistical, systematic and common ("norm") uncertainties; the sample submission: tables without independent
// variables, asymmetric statistical errors, several systematic sources added in quadrature; tables of equal bins as
// Histogram and HistogramSyst (gaps, rank 2), bins with off-center values as points, an unlabelled error as the
// statistical one; export in strict mode and back; lossless round trips of every class (Histogram, HistogramSyst,
// rank 2, double precision, DataPointsVector of rank 1 and 2, DataPointColumns), also through files; several objects
// in one table; titles split into name and units. Prints each failed check and exits with the number of failures.
//
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>
#include "HEPDataConverter.hpp"
#include "Histogram.hpp"
#include "HistogramSyst.hpp"
#include "DataPoint2D.hpp"
#include "DataPoint3D.hpp"
#include "DataPointsVector.hpp"
#include "DataPointColumns.hpp"
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

bool near(double a, double b, double tolerance=1.0E-9) { return std::fabs(a-b) <= tolerance*(std::fabs(a)+std::fabs(b)+1.0E-300); }

template <class T>
bool throws(T f) { try { f(); } catch (const Exception &) { return true; } return false; }

template <class T>
const T * as(const C::Imported & i) { return dynamic_cast<const T *>(i.object.get()); }

//! Same content, exactly (lossless).
bool sameHistogram(const Histogram & a, const Histogram & b)
{
  if (!a.compatibleWith(b) || a.nBinsTotal()!=b.nBinsTotal() || a.rank()!=b.rank()) return false;
  if (!(a.name()==b.name()) || !(a.title()==b.title()) || a.precision()!=b.precision()) return false;
  if (a.hasSystUncertainties()!=b.hasSystUncertainties()) return false;
  if (a.getCommonSystPlus()!=b.getCommonSystPlus() || a.getCommonSystMinus()!=b.getCommonSystMinus()) return false;
  for (unsigned int d=0; d<a.rank(); d++) if (!(a.dimensionTitle(d)==b.dimensionTitle(d))) return false;
  for (unsigned int k=0; k<a.nBinsTotal(); k++)
    {
    if (a.binEntriesAtGlobal(k)!=b.binEntriesAtGlobal(k)) return false;
    if (a.binValueAtGlobal(k)!=b.binValueAtGlobal(k)) return false;
    if (a.binUncertaintyAtGlobal(k)!=b.binUncertaintyAtGlobal(k)) return false;
    if (a.hasSystUncertainties() &&
        (a.binSystPlusAtGlobal(k)!=b.binSystPlusAtGlobal(k) || a.binSystMinusAtGlobal(k)!=b.binSystMinusAtGlobal(k))) return false;
    }
  return true;
}

bool samePoints(const DataPoints & a, const DataPoints & b)
{
  if (a.size()!=b.size() || a.rank()!=b.rank() || !(a.name()==b.name()) || !(a.title()==b.title())) return false;
  if (a.getCommonSystPlus()!=b.getCommonSystPlus() || a.getCommonSystMinus()!=b.getCommonSystMinus()) return false;
  for (unsigned int k=0; k<a.size(); k++)
    {
    std::vector<double> x = a.coordinatesAt(k), y = b.coordinatesAt(k), ex = a.coordinateErrorsAt(k), ey = b.coordinateErrorsAt(k);
    for (unsigned int i=0; i<a.rank(); i++) if (x[i]!=y[i] || ex[i]!=ey[i]) return false;
    if (a.valueAt(k)!=b.valueAt(k) || a.statErrorAt(k)!=b.statErrorAt(k)) return false;
    if (a.sysErrorPlusAt(k)!=b.sysErrorPlusAt(k) || a.sysErrorMinusAt(k)!=b.sysErrorMinusAt(k)) return false;
    }
  return true;
}

//! A table with one binned independent variable and one dependent variable.
HEPDataTable binnedTable(const std::vector<double> & low, const std::vector<double> & high, const std::vector<double> & y,
                         const std::vector<double> & stat, const std::vector<double> & sys)
{
  HEPDataTable t;
  t.name = "T";
  t.dataFile = "t.yaml";
  HEPDataVariable x, v;
  x.name = "PT";
  x.units = "GEV";
  v.name = "N";
  for (unsigned int k=0; k<low.size(); k++)
    {
    x.values.push_back(HEPDataValue::bin(low[k],high[k]));
    HEPDataValue d = HEPDataValue::number(y[k]);
    d.errors.push_back(HEPDataError::symmetric(stat[k],"stat"));
    if (!sys.empty()) d.errors.push_back(HEPDataError::symmetric(sys[k],"sys"));
    v.values.push_back(d);
    }
  t.independent.push_back(x);
  t.dependent.push_back(v);
  return t;
}

void testTitles()
{
  String n, u;
  C::splitTitle("PT (GEV/c)",n,u);
  CHECK(n=="PT" && u=="GEV/c");
  C::splitTitle("E d**3sigma/dp**3 (b GEV**-2 c**3)",n,u);
  CHECK(n=="E d**3sigma/dp**3" && u=="b GEV**-2 c**3");
  C::splitTitle("f(x)",n,u);
  CHECK(n=="f(x)" && u.isNull());
  C::splitTitle("a (b (c))",n,u);
  CHECK(n=="a" && u=="b (c)");
  C::splitTitle("plain",n,u);
  CHECK(n=="plain" && u.isNull());
  CHECK(C::joinTitle("PT","GEV/c")=="PT (GEV/c)" && C::joinTitle("PT","")=="PT");
}

void testAlice(const std::string & data)
{
  HEPDataSubmission s = HEPDataSubmission::read(String(data+"/ALICE_OO_pO_pi0"));
  std::vector<String> warnings;
  std::vector<C::Imported> objects = C::toObjects(s,C::ImportOptions(),&warnings);
  for (const String & w : warnings) std::printf("  warning: %s\n", w.c_str());
  CHECK(objects.size()==8);
  // bins of different widths: points, the value of the bin as x, half its width as the x uncertainty
  const DataPointsVector * pp = as<DataPointsVector>(objects[0]);
  CHECK(pp!=nullptr);
  if (!pp) return;
  CHECK(pp->name()=="data1" && pp->title()=="E d**3sigma/dp**3 (b GEV**-2 c**3)");
  CHECK(objects[0].table==s.tables[0].name && objects[0].column==0 && objects[0].qualifiers.size()==3);
  CHECK(pp->size()==27 && pp->rank()==1);
  const DataPoint2D & p0 = pp->pointAs<DataPoint2D>(0);
  CHECK(near(p0.x(),1.2958,1e-7) && near(p0.xError(),0.1,1e-6));
  CHECK(near(p0.y(),1.4166e-03,1e-7) && near(p0.yStatError(),1.17e-05,1e-6));
  CHECK(near(p0.systPlus(),7.07e-05,1e-6) && !p0.hasAsymmetricSyst());          // sys only: norm is common
  const DataPoint2D & p26 = pp->pointAs<DataPoint2D>(26);
  CHECK(near(p26.x(),17.1451,1e-7) && near(p26.xError(),2.5,1e-6));
  // the common systematic uncertainty: norm/value, averaged over the points
  const HEPDataVariable & y = s.tables[0].dependent[0];
  double mean = 0;
  for (const HEPDataValue & v : y.values) mean += v.errors[2].plus/v.value;
  mean /= y.values.size();
  CHECK(pp->hasCommonSyst() && !pp->hasAsymmetricCommonSyst());
  CHECK(near(pp->getCommonSystPlus(),mean,1e-12));
  CHECK(pp->getCommonSystPlus()>0.02 && pp->getCommonSystPlus()<0.03);           // about 2.6%
  // R_OO: no units
  const DataPointsVector * rOO = as<DataPointsVector>(objects[5]);
  CHECK(rOO && rOO->title()=="ROO=SIG(PI0,OO,5.36TeV)/SIG(PI0,PP,5.36TeV)/256" && rOO->name()=="data6");
  CHECK(rOO && near(rOO->pointAs<DataPoint2D>(0).x(),1.3,1e-7) && near(rOO->valueAt(0),0.74009,1e-7));
  // norm as an ordinary systematic uncertainty, if asked: added in quadrature to sys
  C::ImportOptions noCommon;
  noCommon.commonLabels.clear();
  std::vector<C::Imported> q = C::toObjects(s.tables[0],noCommon);
  CHECK(!q[0].object->hasCommonSyst());
  CHECK(near(q[0].object->sysErrorPlusAt(0),std::sqrt(7.07e-05*7.07e-05+3.68e-05*3.68e-05),1e-6));

  // export for HEPData (strict) and import again: the same numbers, norm as a percentage
  C::ExportOptions strict;
  strict.lossless = false;
  HEPDataTable back = C::toTable(objects[0].object ? std::vector<C::Imported>{objects[0]} : std::vector<C::Imported>(),strict);
  CHECK(back.datumBlock.isNull() && back.name==s.tables[0].name);
  CHECK(back.problems().size()==2);                                             // no description, no keywords: to be set
  CHECK(back.dependent[0].qualifiers.size()==3 && back.dependent[0].units=="b GEV**-2 c**3");
  // points have a symmetric x uncertainty: the edges written are x -+ half the width of the bin, not those read
  CHECK(back.independent[0].values[0].isNumeric() && near(back.independent[0].values[0].value,1.2958,1e-7));
  CHECK(near(back.independent[0].values[0].low,1.1958,1e-6) && near(back.independent[0].values[0].high,1.3958,1e-6));
  const HEPDataValue & b0 = back.dependent[0].values[0];
  CHECK(b0.errors.size()==3 && b0.errors[2].label=="norm" && b0.errors[2].percent);
  std::vector<C::Imported> again = C::toObjects(back);
  const DataPointsVector * a = as<DataPointsVector>(again[0]);
  CHECK(a && a->size()==27);
  for (unsigned int k=0; a && k<27; k++)
    {
    CHECK(a->valueAt(k)==pp->valueAt(k) && a->statErrorAt(k)==pp->statErrorAt(k));
    CHECK(a->sysErrorPlusAt(k)==pp->sysErrorPlusAt(k));
    }
  CHECK(a && near(a->getCommonSystPlus(),pp->getCommonSystPlus(),1e-12));
}

void testSample(const std::string & data)
{
  HEPDataSubmission s = HEPDataSubmission::read(String(data+"/TestHEPSubmission"));
  std::vector<String> warnings;
  std::vector<C::Imported> t1 = C::toObjects(*s.table("Table 1"),C::ImportOptions(),&warnings);
  CHECK(t1.size()==3 && t1[0].object->name()=="data1_0" && t1[2].object->name()=="data1_2");
  CHECK(t1[0].object->title()=="SIG(fiducial) (FB)");
  bool asymmetric = false;
  for (const String & w : warnings) if (w.str().find("asymmetric statistical")!=std::string::npos) asymmetric = true;
  CHECK(asymmetric);
  const DataPoints & o = *t1[0].object;
  // (points are stored in single precision)
  CHECK(o.size()==1 && o.coordinatesAt(0)[0]==0.0 && near(o.valueAt(0),25.4,1e-7));
  CHECK(near(o.statErrorAt(0),3.3,1e-7));                                       // the larger side
  CHECK(near(o.sysErrorPlusAt(0),std::sqrt(1.2*1.2+1.0),1e-7) && near(o.sysErrorMinusAt(0),std::sqrt(1.0+1.0),1e-7));
  CHECK(!o.hasCommonSyst());                                                    // sys,lumi is not a common label here
  C::ImportOptions lumi;
  lumi.commonLabels.push_back("sys,lumi");
  std::vector<C::Imported> l = C::toObjects(*s.table("Table 1"),lumi);
  CHECK(near(l[0].object->getCommonSystPlus(),1.0/25.4) && near(l[0].object->sysErrorPlusAt(0),1.2,1e-7));
  // bins of different widths
  std::vector<C::Imported> t3 = C::toObjects(*s.table("Table 3"));
  CHECK(as<DataPointsVector>(t3[0])!=nullptr && t3[0].object->size()==4);
  CHECK(t3[0].object->coordinatesAt(3)[0]==400.0 && t3[0].object->coordinateErrorsAt(3)[0]==200.0);
}

void testHistogramImport()
{
  // equal bins: a Histogram; with systematics: a HistogramSyst
  HEPDataTable t = binnedTable({0,1,2,3},{1,2,3,4},{10,20,30,40},{1,2,3,4},{});
  std::vector<C::Imported> o = C::toObjects(t);
  const Histogram * h = as<Histogram>(o[0]);
  CHECK(h && !h->hasSystUncertainties() && h->nBins(0)==4 && h->minimum(0)==0.0 && h->maximum(0)==4.0);
  CHECK(h && h->binValueAtGlobal(2)==30.0 && h->binUncertaintyAtGlobal(2)==3.0 && h->binEntriesAtGlobal(2)==1);
  CHECK(h && h->dimensionTitle(0)=="PT (GEV)" && h->name()=="t" && h->title()=="N");
  HEPDataTable ts = binnedTable({0,1,2,3},{1,2,3,4},{10,20,30,40},{1,2,3,4},{0.5,0.5,0.5,0.5});
  ts.dependent[0].values[1].errors[1] = HEPDataError::asymmetric(0.7,-0.2,"sys");
  std::vector<C::Imported> os = C::toObjects(ts);
  const HistogramSyst * hs = as<HistogramSyst>(os[0]);
  CHECK(hs && near(hs->binSystPlusAtGlobal(1),0.7,1e-7) && near(hs->binSystMinusAtGlobal(1),0.2,1e-7) && hs->binSystPlusAtGlobal(0)==0.5);   // float
  // a gap (an omitted empty bin) and a non-numeric value: empty bins
  HEPDataTable g = binnedTable({0,1,3},{1,2,4},{10,20,40},{1,2,4},{});
  g.dependent[0].values[1] = HEPDataValue::textValue("-");
  std::vector<C::Imported> og = C::toObjects(g);
  const Histogram * hg = as<Histogram>(og[0]);
  CHECK(hg && hg->nBins(0)==4 && hg->binEntriesAtGlobal(1)==0 && hg->binEntriesAtGlobal(2)==0 && hg->binValueAtGlobal(3)==40.0);
  // values away from the middles of the bins: points (the positions are kept)
  HEPDataTable off = binnedTable({0,1},{1,2},{1,2},{0.1,0.1},{});
  off.independent[0].values[0].hasValue = true;
  off.independent[0].values[0].value = 0.3;
  CHECK(as<DataPointsVector>(C::toObjects(off)[0])!=nullptr);
  C::ImportOptions noHistograms;
  noHistograms.histograms = false;
  CHECK(as<DataPointsVector>(C::toObjects(t,noHistograms)[0])!=nullptr);
  // an unlabelled error alone: statistical
  HEPDataTable u = binnedTable({0,1},{1,2},{5,6},{0.5,0.6},{});
  for (HEPDataValue & v : u.dependent[0].values) v.errors[0].label = String();
  std::vector<C::Imported> ou = C::toObjects(u);
  const Histogram * hu = as<Histogram>(ou[0]);
  CHECK(hu && near(hu->binUncertaintyAtGlobal(1),0.6,1e-7));
  // two independent variables on a grid: rank 2
  HEPDataTable t2;
  t2.name = "grid";
  HEPDataVariable x0, x1, z;
  x0.name = "x"; x1.name = "y"; z.name = "z";
  for (unsigned int j=0; j<2; j++)
    for (unsigned int i=0; i<3; i++)
      {
      x0.values.push_back(HEPDataValue::bin(i,i+1.0));
      x1.values.push_back(HEPDataValue::bin(10.0+5*j,15.0+5*j));
      HEPDataValue v = HEPDataValue::number(i+10.0*j);
      v.errors.push_back(HEPDataError::symmetric(0.1,"stat"));
      z.values.push_back(v);
      }
  t2.independent = {x0,x1};
  t2.dependent = {z};
  std::vector<C::Imported> o2 = C::toObjects(t2);
  const Histogram * h2 = as<Histogram>(o2[0]);
  CHECK(h2 && h2->rank()==2 && h2->nBins(0)==3 && h2->nBins(1)==2);
  CHECK(h2 && h2->binValueAt(2,1)==12.0 && h2->minimum(1)==10.0 && h2->maximum(1)==20.0 && h2->name()=="grid");
  // a grid that is not regular in y: points of rank 2
  t2.independent[1].values[5] = HEPDataValue::bin(15.0,30.0);
  std::vector<C::Imported> op2 = C::toObjects(t2);
  const DataPointsVector * p2 = as<DataPointsVector>(op2[0]);
  CHECK(p2 && p2->rank()==2 && p2->size()==6);
  // variables of different sizes
  t.dependent[0].values.pop_back();
  CHECK(throws([&t]() { C::toObjects(t); }));
}

void testStrictExport()
{
  HistogramSyst h("spectrum","dN/dpT (GEV**-1)",5,0.0,5.0);
  h.setDimensionTitle(0,"PT (GEV)");
  for (unsigned int k=0; k<5; k++)
    if (k!=3) { h.setBinAtGlobal(k,10*(k+1),100.0/(k+1),2.0); h.setBinSystAtGlobal(k,1.0,k==1 ? 0.5 : 1.0); }
  h.setCommonSyst(0.03);
  C::ExportOptions strict;
  strict.lossless = false;
  HEPDataTable t = C::toTable(h,strict);
  CHECK(t.datumBlock.isNull());
  CHECK(t.nRows()==4);                                                           // the empty bin is omitted
  CHECK(t.independent[0].name=="PT" && t.independent[0].units=="GEV");
  CHECK(t.dependent[0].name=="dN/dpT" && t.dependent[0].units=="GEV**-1");
  CHECK(t.independent[0].values[3].low==4.0 && t.independent[0].values[3].high==5.0);
  const HEPDataValue & v1 = t.dependent[0].values[1];
  CHECK(v1.value==50.0 && v1.errors.size()==3 && v1.errors[0].label=="stat" && near(v1.errors[0].plus,2.0,1e-7));   // 20 entries x float(0.1)
  CHECK(v1.errors[1].kind==HEPDataError::kAsymmetric && v1.errors[1].plus==1.0 && v1.errors[1].minus==-0.5);
  CHECK(v1.errors[2].label=="norm" && v1.errors[2].percent && near(v1.errors[2].plus,3.0));
  CHECK(t.dependent[0].values[0].errors[1].kind==HEPDataError::kSymmetric);
  strict.keepEmptyBins = true;
  HEPDataTable e = C::toTable(h,strict);
  CHECK(e.nRows()==5 && !e.dependent[0].values[3].isNumeric() && e.dependent[0].values[3].text=="-");
  // back: the same bins (one entry each: HEPData has no numbers of entries)
  std::vector<C::Imported> ob = C::toObjects(t);
  const HistogramSyst * b = as<HistogramSyst>(ob[0]);
  CHECK(b && b->nBins(0)==5 && b->binEntriesAtGlobal(3)==0 && b->binEntriesAtGlobal(1)==1);
  CHECK(b && b->binValueAtGlobal(1)==50.0 && near(b->binUncertaintyAtGlobal(1),2.0,1e-7) && b->binSystMinusAtGlobal(1)==0.5);
  CHECK(b && near(b->getCommonSystPlus(),0.03));
  // labels of the options
  strict.statLabel = "stat,total";
  strict.commonLabel = "sys,lumi";
  HEPDataTable l = C::toTable(h,strict);
  CHECK(l.dependent[0].values[0].errors[0].label=="stat,total" && l.dependent[0].values[0].errors[2].label=="sys,lumi");
  // several histograms in one table; different binnings or a histogram with points are refused
  HistogramSyst h2(h);
  h2.setName("other");
  h2.setTitle("other (GEV**-1)");
  h2.scale(2.0);
  C::ExportOptions o;
  o.lossless = false;
  o.qualifiers = {{{String("SQRT(S)"),String("5020"),String("GEV")}}};
  HEPDataTable two = C::toTable(std::vector<const DataPoints *>{&h,&h2},o);
  CHECK(two.dependent.size()==2 && two.dependent[1].name=="other" && two.dependent[1].values[0].value==200.0);
  CHECK(two.dependent[0].qualifiers.size()==1 && two.dependent[1].qualifiers.size()==1);
  Histogram other("x","x",4,0.0,5.0);
  CHECK(throws([&]() { C::toTable(std::vector<const DataPoints *>{&h,&other},o); }));
  DataPointsVector points;
  points.addPoint(DataPoint2D(1.0,0.0,2.0,0.1,0.0));
  CHECK(throws([&]() { C::toTable(std::vector<const DataPoints *>{&h,&points},o); }));
}

void testLossless(const std::string & work)
{
  // Histogram filled with weights, double precision
  Histogram h("h","counts (1/GEV)",6,-1.5,1.5);
  h.setPrecision(Histogram::kDouble);
  h.setDimensionTitle(0,"eta");
  for (int i=0; i<200; i++) h.fill(-1.4+0.0137*i,1.0+0.001*i);
  h.setCommonSyst(0.021,0.017);
  // HistogramSyst, float, asymmetric systematics, an empty bin
  HistogramSyst s("s","ratio",4,0.0,2.0);
  s.setDimensionTitle(0,"PT (GEV/c)");
  for (unsigned int k=0; k<4; k++) if (k!=2) { s.setBinAtGlobal(k,3,1.0/(k+3),0.01*(k+1)); s.setBinSystAtGlobal(k,0.02,0.01*k); }
  // rank 2
  HistogramSyst r2("r2","z",3,0.0,3.0,2,-1.0,1.0);
  r2.setDimensionTitle(0,"x");
  r2.setDimensionTitle(1,"y");
  for (unsigned int k=0; k<6; k++) { r2.setBinAtGlobal(k,k+1,k*1.5+0.25,0.1); r2.setBinSystAtGlobal(k,0.3); }
  // points: rank 1 and rank 2, and columns
  DataPointsVector p;
  p.setName("p");
  p.setTitle("y vs x");
  for (int k=0; k<5; k++) { DataPoint2D q(0.5*k,0.1*k,3.0+k,0.2,0.0); q.setSyst(0.3,0.1*k); p.addPoint(q); }
  p.setCommonSyst(0.05);
  DataPointsVector p3;
  p3.setName("p3");
  p3.setTitle("z vs x,y");
  for (int k=0; k<4; k++) { DataPoint3D q(k,0.5,2.0*k,0.0,k*k+0.5,0.1,0.2); p3.addPoint(q); }
  DataPointColumns col;
  col.setName("col");
  col.setTitle("columns (MB)");
  col.setColumns({{1.0,2.0,3.0}},{},{10.0,20.0,30.0},{1.0,2.0,3.0},{0.5,0.6,0.7},{0.1,0.2,0.3});

  HEPDataSubmission sub;
  std::vector<const DataPoints *> all = {&h,&s,&r2,&p,&p3,&col};
  for (const DataPoints * o : all)
    {
    HEPDataTable t = C::toTable(*o);
    CHECK(!t.datumBlock.isNull());
    t.name = o->name();
    t.description = "A test table.";
    t.setKeyword("observables",{"TEST"});
    t.dataFile = String(o->name().str()+".yaml");
    sub.addTable(t);
    // directly
    std::vector<C::Imported> back = C::toObjects(t);
    CHECK(back.size()==1);
    const Histogram * a = dynamic_cast<const Histogram *>(o);
    if (a)
      {
      const Histogram * b = as<Histogram>(back[0]);
      CHECK(b && sameHistogram(*a,*b));
      CHECK(b && (dynamic_cast<const HistogramSyst *>(b)!=nullptr)==a->hasSystUncertainties());
      }
    else
      {
      CHECK(samePoints(*o,*back[0].object));
      CHECK((dynamic_cast<const DataPointColumns *>(o)!=nullptr)==(as<DataPointColumns>(back[0])!=nullptr));
      }
    }
  // through files (and the datum block only in lossless mode)
  sub.write(String(work+"/lossless"));
  HEPDataSubmission read = HEPDataSubmission::read(String(work+"/lossless"));
  std::vector<C::Imported> objects = C::toObjects(read);
  CHECK(objects.size()==all.size());
  for (unsigned int i=0; i<objects.size() && i<all.size(); i++)
    {
    const Histogram * a = dynamic_cast<const Histogram *>(all[i]);
    const Histogram * b = as<Histogram>(objects[i]);
    if (a) CHECK(b && sameHistogram(*a,*b));
    else   CHECK(samePoints(*all[i],*objects[i].object));
    }
  const Histogram * hb = as<Histogram>(objects[0]);
  CHECK(hb && hb->binEntriesAtGlobal(3)==h.binEntriesAtGlobal(3) && hb->binEntriesAtGlobal(3)>1);
  // the same objects written for HEPData: valid tables
  sub.write(String(work+"/strict"),String(),0,false);
  HEPDataSubmission strict = HEPDataSubmission::read(String(work+"/strict"));
  CHECK(strict.tables[0].datumBlock.isNull());
}
}

int main(int argc, char ** argv)
{
  std::setvbuf(stdout,nullptr,_IONBF,0);
  std::string data = argc>1 ? argv[1] : "data";
  std::string pattern = (fs::temp_directory_path()/"datum_testconverter_XXXXXX").string();
  std::vector<char> name(pattern.begin(),pattern.end());
  name.push_back('\0');
  if (!::mkdtemp(name.data())) { std::printf("FAILED: no temporary directory\n"); return 1; }
  std::string work(name.data());
  try
    {
    testTitles(); std::fflush(stdout);
    testAlice(data); std::fflush(stdout);
    testSample(data); std::fflush(stdout);
    testHistogramImport(); std::fflush(stdout);
    testStrictExport(); std::fflush(stdout);
    testLossless(work); std::fflush(stdout);
    }
  catch (const Exception & e)
    {
    std::printf("FAILED: exception: %s\n", e.what());
    nFailures++;
    }
  std::error_code ignored;
  fs::remove_all(work,ignored);
  std::printf("TestHEPDataConverter: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
