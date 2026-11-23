/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestHEPDataFunctions.cpp
//! @brief Test program of the HEPData tables of functions and fits (run by ctest).
//
// TestHEPDataFunctions -- tables of functions: sampled points, values at bin centers and averages over bins, the
// parameters as qualifiers, the function read back exactly from the datum block (also through files), none in strict
// mode; tables of fits: the range of the fit, the uncertainty band from the covariance (checked against a direct
// computation for a straight line), the parameters with their errors, chi2/ndf, the fit read back; the fits attached
// to a histogram kept by its lossless table and attached again; fitTables(); errors (functions of two variables, empty
// ranges, bad edges). Prints each failed check and exits with the number of failures (0: all passed).
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
#include "Fitter.hpp"
#include "Normal.hpp"
#include "Linear.hpp"
#include "Exponential.hpp"
#include "Normal2D.hpp"
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

const HEPDataQualifier * qualifier(const HEPDataTable & t, const char * name)
{
  return t.dependent.empty() ? nullptr : t.dependent[0].qualifier(name);
}

//! A histogram of a Gaussian peak on a flat background, with Poisson-like uncertainties (deterministic).
Histogram peak()
{
  Histogram h("peak","counts",40,-4.0,4.0);
  h.setDimensionTitle(0,"x");
  for (unsigned int k=0; k<40; k++)
    {
    double x = h.binCenter(k,0);
    double v = 800.0*std::exp(-0.5*(x-0.3)*(x-0.3)/(0.7*0.7)) + 10.0 + 3.0*std::sin(7.0*x);
    h.setBinAtGlobal(k,1,v,std::sqrt(v));
    }
  return h;
}

void testFunctionTables(const std::string & work)
{
  Exponential e(2.0,-0.5);
  e.setName("decay");
  C::FunctionOptions o;
  o.nPoints = 11;
  o.xName = "t";
  o.xUnits = "NS";
  HEPDataTable t = C::functionTable(e,0.0,10.0,o);
  CHECK(t.nRows()==11 && t.independent[0].name=="t" && t.independent[0].units=="NS");
  CHECK(t.independent[0].values[10].value==10.0 && !t.independent[0].values[0].isBin());
  bool values = true;
  for (unsigned int i=0; i<11; i++) values = values && t.dependent[0].values[i].value==e.evaluateAt(i*1.0);
  CHECK(values);
  CHECK(t.dependent[0].values[0].errors.empty());
  CHECK(t.dependent[0].name=="decay");
  CHECK(qualifier(t,"FUNCTION")!=nullptr && qualifier(t,"FUNCTION")->value=="Exponential");
  CHECK(t.dependent[0].qualifiers.size()==e.nParameters()+1);
  CHECK(!t.datumBlock.isNull() && t.problems().size()==1);                      // no keywords: to be set
  // read back exactly
  std::shared_ptr<ParameterizedFunction> f = C::toFunction(t);
  CHECK(f && dynamic_cast<Exponential *>(f.get())!=nullptr && f->name()=="decay");
  CHECK(f && f->parameterValues()==e.parameterValues() && f->evaluateAt(3.7)==e.evaluateAt(3.7));
  CHECK(C::toFit(t)==nullptr);
  // through files
  HEPDataSubmission s;
  t.setKeyword("observables",{"TEST"});
  s.addTable(t);
  s.write(String(work+"/functions"));
  HEPDataSubmission r = HEPDataSubmission::read(String(work+"/functions"));
  std::shared_ptr<ParameterizedFunction> fr = C::toFunction(r.tables[0]);
  CHECK(fr && fr->evaluateAt(1.25)==e.evaluateAt(1.25));
  // strict mode: no datum block, nothing to read back
  C::FunctionOptions strict = o;
  strict.lossless = false;
  HEPDataTable st = C::functionTable(e,0.0,10.0,strict);
  CHECK(st.datumBlock.isNull() && C::toFunction(st)==nullptr);
  // bins: values at the centers, or averages
  Linear line(1.0,2.0);
  C::FunctionOptions bins;
  bins.edges = {0.0,1.0,3.0,6.0};
  HEPDataTable lt = C::functionTable(line,0.0,0.0,bins);                        // the range is that of the edges
  CHECK(lt.nRows()==3 && lt.independent[0].values[1].isBin() && lt.independent[0].values[1].high==3.0);
  CHECK(near(lt.dependent[0].values[2].value,1.0+2.0*4.5,1e-9));                 // a line: average = value at center
  Normal g(1.0,0.0,1.0);
  HEPDataTable gt = C::functionTable(g,0.0,0.0,bins);
  CHECK(near(gt.dependent[0].values[0].value,g.integralInRange(0.0,1.0),1e-6));
  CHECK(gt.dependent[0].values[0].value < g.evaluateAt(0.5) + 1e-3 || gt.dependent[0].values[0].value > 0);
  bins.averages = false;
  HEPDataTable gc = C::functionTable(g,0.0,0.0,bins);
  CHECK(gc.dependent[0].values[1].value==g.evaluateAt(2.0));
  // errors
  Normal2D n2;
  CHECK(throws([&]() { C::functionTable(n2,0.0,1.0); }));
  CHECK(throws([&]() { C::functionTable(e,1.0,1.0); }));
  C::FunctionOptions bad;
  bad.edges = {0.0,2.0,1.0};
  CHECK(throws([&]() { C::functionTable(e,0.0,1.0,bad); }));
}

void testFitTables(const std::string & work)
{
  // a straight line fitted to points: the band is sqrt(var(c0) + 2x cov(c0,c1) + x^2 var(c1))
  Histogram data("data","y",10,0.0,10.0);
  for (unsigned int k=0; k<10; k++) data.setBinAtGlobal(k,1,1.0+0.5*data.binCenter(k,0)+0.2*((k%3)-1.0),0.3);
  Linear line(0.0,1.0);
  FitRecord lf = Fitter::fitAndAttach(line,data);
  CHECK(lf.function!=nullptr && lf.result.validErrors && lf.result.freeParameters.size()==2);
  C::FunctionOptions o;
  o.nPoints = 5;
  HEPDataTable t = C::fitTable(lf,o);
  CHECK(t.nRows()==5 && t.independent[0].values[0].value==lf.low[0] && t.independent[0].values[4].value==lf.high[0]);
  const FitResult & r = lf.result;
  bool bandOk = true;
  for (unsigned int i=0; i<5; i++)
    {
    double x = t.independent[0].values[i].value;
    double expected = std::sqrt(r.covariance(0,0)+2*x*r.covariance(0,1)+x*x*r.covariance(1,1));
    const HEPDataValue & v = t.dependent[0].values[i];
    bandOk = bandOk && v.errors.size()==1 && v.errors[0].label=="fit" && near(v.errors[0].plus,expected,1e-5);
    bandOk = bandOk && near(v.value,lf.function->evaluateAt(x),1e-12);
    }
  CHECK(bandOk);
  const HEPDataQualifier * c0 = qualifier(t,lf.parameterName(0).c_str());
  CHECK(c0!=nullptr && c0->value.str().find("+-")!=std::string::npos);
  CHECK(qualifier(t,"CHI2/NDF")!=nullptr && qualifier(t,"CHI2/NDF")->value.str().find("/8")!=std::string::npos);
  CHECK(qualifier(t,"FUNCTION")!=nullptr && qualifier(t,"FUNCTION")->value=="Linear");
  // the fit read back
  std::shared_ptr<const FitRecord> back = C::toFit(t);
  CHECK(back && back->function && back->label==lf.label);
  CHECK(back && back->result.values==lf.result.values && back->result.errors==lf.result.errors);
  CHECK(back && back->result.chiSquare==lf.result.chiSquare && back->low==lf.low && back->high==lf.high);
  CHECK(back && back->function->evaluateAt(4.4)==lf.function->evaluateAt(4.4));
  std::shared_ptr<ParameterizedFunction> f = C::toFunction(t);
  CHECK(f && f->parameterValues()==lf.function->parameterValues());
  C::FunctionOptions noBand = o;
  noBand.band = false;
  CHECK(C::fitTable(lf,noBand).dependent[0].values[2].errors.empty());

  // a Gaussian fitted to a histogram: the fit kept by the lossless table of the histogram
  Histogram h = peak();
  Normal g(500.0,0.0,1.0);
  FitRecord gf = Fitter::fitAndAttach(g,h);
  CHECK(h.nFits()==1 && near(gf.function->parameter(1).value(),0.3,0.05));
  HEPDataTable ht = C::toTable(h);
  std::vector<C::Imported> objects = C::toObjects(ht);
  CHECK(objects.size()==1 && objects[0].object->nFits()==1);
  if (objects.size()==1 && objects[0].object->nFits()==1)
    {
    const FitRecord & kept = objects[0].object->fit(0);
    CHECK(kept.result.values==gf.result.values && kept.result.chiSquare==gf.result.chiSquare && kept.label==gf.label);
    CHECK(kept.function->evaluateAt(0.25)==gf.function->evaluateAt(0.25));
    }
  C::ExportOptions withoutFits;
  withoutFits.withFits = false;
  CHECK(C::toObjects(C::toTable(h,withoutFits))[0].object->nFits()==0);
  // the tables of the fits of an object
  std::vector<HEPDataTable> fits = C::fitTables(h);
  CHECK(fits.size()==1 && fits[0].name.str().find("peak fit 0")==0);
  CHECK(fits[0].nRows()==100 && C::toFit(fits[0])!=nullptr);
  // the histogram and its fit in a submission, through files
  HEPDataSubmission s;
  ht.name = "peak";
  ht.description = "A peak.";
  ht.setKeyword("observables",{"N"});
  s.addTable(ht);
  fits[0].setKeyword("observables",{"N"});
  s.addTable(fits[0]);
  s.write(String(work+"/fits"));
  HEPDataSubmission read = HEPDataSubmission::read(String(work+"/fits"));
  CHECK(read.tables.size()==2 && C::toObjects(read.tables[0])[0].object->nFits()==1);
  std::shared_ptr<const FitRecord> rf = C::toFit(read.tables[1]);
  CHECK(rf && rf->result.values==gf.result.values);
  CHECK(read.problems().empty());
  // strict: the fit table is valid for HEPData, without datum block
  C::FunctionOptions strict;
  strict.lossless = false;
  HEPDataTable sf = C::fitTable(gf,strict);
  CHECK(sf.datumBlock.isNull() && C::toFit(sf)==nullptr && sf.dependent[0].values[0].errors.size()==1);
}
}

int main()
{
  std::setvbuf(stdout,nullptr,_IONBF,0);
  std::string pattern = (fs::temp_directory_path()/"datum_testfunctions_XXXXXX").string();
  std::vector<char> name(pattern.begin(),pattern.end());
  name.push_back('\0');
  if (!::mkdtemp(name.data())) { std::printf("FAILED: no temporary directory\n"); return 1; }
  std::string work(name.data());
  try
    {
    testFunctionTables(work);
    testFitTables(work);
    }
  catch (const Exception & e)
    {
    std::printf("FAILED: exception: %s\n", e.what());
    nFailures++;
    }
  std::error_code ignored;
  fs::remove_all(work,ignored);
  std::printf("TestHEPDataFunctions: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
