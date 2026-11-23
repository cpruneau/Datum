/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestHistogramSyst.cpp
//! @brief Test program of HistogramSyst and of the systematic uncertainty stubs of Histogram (run by ctest).
//
// TestHistogramSyst -- Histogram: the stubs throw, hasSystUncertainties() is false, the common systematic uncertainty
// follows sums, products, ratios, negative scales and rebinning. HistogramSyst: setters and getters by global index,
// indices, data points; propagation of the upper and lower sides by scale, sums, differences, products, ratios,
// inverse, with plain Histogram operands, through the virtual functions of Histogram and the free operators; empty
// bins; rebinning (linear sums, scaled last bin); float and double precision; copies and clone(); binary round trips
// (stream, file, Folder). Prints each failed check and exits with the number of failures (0: all passed).
//
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "HistogramSyst.hpp"
#include "Folder.hpp"
#include "StringParameter.hpp"
#include "MathExceptions.hpp"

using namespace Datum;
using namespace Datum::MATH;

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

bool near(double a, double b, double tolerance=1.0E-5) { return std::fabs(a-b) <= tolerance*(1.0+std::fabs(b)); }

#define CHECK(c) check((c), #c, __LINE__)

template <class T>
bool throws(T f) { try { f(); } catch (const Exception &) { return true; } return false; }

//! 4 bins in [0, 4): bin k has one entry, value v[k], statistical uncertainty s[k], systematics +p[k] -m[k].
HistogramSyst make(const std::vector<double> & v, const std::vector<double> & p, const std::vector<double> & m,
                   double stat=0.5)
{
  HistogramSyst h("h","h",4,0.0,4.0);
  for (unsigned int k=0; k<4; k++)
    {
    h.setBinAtGlobal(k,1,v[k],stat);
    h.setBinSystAtGlobal(k,p[k],m[k]);
    }
  return h;
}

void testStubs()
{
  Histogram h("h","h",3,0.0,3.0);
  CHECK(!h.hasSystUncertainties());
  CHECK(throws([&h]() { h.binSystPlusAtGlobal(0); }));
  CHECK(throws([&h]() { h.binSystMinusAt(1); }));
  CHECK(throws([&h]() { h.binSystAt(1); }));
  CHECK(throws([&h]() { h.setBinSystAtGlobal(0,0.1,0.2); }));
  CHECK(throws([&h]() { h.setBinSystAt(1,0.1,0.1); }));
  CHECK(h.sysErrorAt(0)==0.0 && h.sysErrorPlusAt(0)==0.0);
  // the common systematic uncertainty of plain histograms
  Histogram a("a","a",3,0.0,3.0), b("b","b",3,0.0,3.0);
  for (unsigned int k=0; k<3; k++) { a.setBinAtGlobal(k,1,10.0,1.0); b.setBinAtGlobal(k,1,5.0,1.0); }
  a.setCommonSyst(0.03,0.01);
  b.setCommonSyst(0.04,0.02);
  Histogram s = a + b;
  CHECK(near(s.getCommonSystPlus(),0.05) && near(s.getCommonSystMinus(),std::sqrt(0.0001+0.0004)));
  Histogram d = a - b;
  CHECK(near(d.getCommonSystPlus(),std::sqrt(0.0009+0.0004)) && near(d.getCommonSystMinus(),std::sqrt(0.0001+0.0016)));
  Histogram r = a / b;
  CHECK(near(r.getCommonSystPlus(),std::sqrt(0.0009+0.0004)));
  Histogram p = a * b;
  CHECK(near(p.getCommonSystPlus(),0.05));
  Histogram n = a * (-1.0);
  CHECK(near(n.getCommonSystPlus(),0.01) && near(n.getCommonSystMinus(),0.03));
  Histogram ss;
  ss.setAsSumOf(a,b);
  CHECK(near(ss.getCommonSystPlus(),0.05));
  Histogram rb = a.rebin(3);
  CHECK(near(rb.getCommonSystPlus(),0.03));
}

void testAccess()
{
  HistogramSyst h("h","h",4,0.0,4.0);
  CHECK(h.hasSystUncertainties());
  CHECK(h.binSystPlusAtGlobal(2)==0.0);
  h.setBinSystAt(2,0.4,0.1);
  CHECK(near(h.binSystPlusAt(2),0.4) && near(h.binSystMinusAt(2),0.1) && near(h.binSystAt(2),0.4));
  CHECK(h.hasAsymmetricSyst());
  h.setBinSystAtGlobal(1,0.3);
  CHECK(near(h.binSystMinusAtGlobal(1),0.3));
  CHECK(throws([&h]() { h.setBinSystAtGlobal(9,0.1,0.1); }));
  CHECK(throws([&h]() { h.setBinSystAtGlobal(0,-0.1,0.1); }));
  CHECK(throws([&h]() { h.binSystPlusAtGlobal(4); }));
  // through a Histogram reference
  Histogram & base = h;
  CHECK(base.hasSystUncertainties() && near(base.binSystPlusAt(2),0.4));
  base.setBinSystAt(3,0.2,0.2);
  CHECK(near(h.binSystMinusAtGlobal(3),0.2));
  // data points
  h.setBinAtGlobal(2,1,7.0,0.7);
  DataPoint2D p = h.point(2);
  CHECK(near(p.y(),7.0) && near(p.systPlus(),0.4) && near(p.systMinus(),0.1));
  CHECK(near(h.sysErrorAt(2),0.4) && near(h.sysErrorMinusAt(2),0.1));
  // filling and the bin setters keep the systematic uncertainties of the bins
  h.fill(2.5,3.0);
  CHECK(near(h.binValueAtGlobal(2),10.0) && near(h.binSystPlusAtGlobal(2),0.4));
  h.setBinEntriesAtGlobal(2,5);
  CHECK(near(h.binSystPlusAtGlobal(2),0.4));
  // 2-D access
  HistogramSyst h2("h2","h2",3,0.0,3.0,2,0.0,2.0);
  h2.setBinSystAt(1,1,0.5,0.25);
  CHECK(near(h2.binSystPlusAt(1,1),0.5) && near(h2.binSystMinusAt(std::vector<unsigned int>{1,1}),0.25));
  CHECK(near(h2.binSystPlusAtGlobal(1+3*1),0.5));
  h.clearSyst();
  CHECK(!h.hasAsymmetricSyst() && h.binSystPlusAtGlobal(2)==0.0);
}

void testArithmetic()
{
  HistogramSyst a = make({10.0,20.0,30.0,40.0},{0.3,0.6,0.9,1.2},{0.1,0.2,0.3,0.4});
  HistogramSyst b = make({ 5.0, 5.0, 5.0, 0.0},{0.4,0.4,0.4,0.4},{0.2,0.2,0.2,0.2});

  // scale: |c|, the sides swap if c<0
  HistogramSyst s = a*2.0;
  CHECK(near(s.binSystPlusAtGlobal(0),0.6) && near(s.binSystMinusAtGlobal(0),0.2));
  s = a*(-2.0);
  CHECK(near(s.binValueAtGlobal(0),-20.0) && near(s.binSystPlusAtGlobal(0),0.2) && near(s.binSystMinusAtGlobal(0),0.6));
  s = -1.0*a;
  CHECK(near(s.binSystPlusAtGlobal(1),0.2));
  s = a/2.0;
  CHECK(near(s.binSystPlusAtGlobal(1),0.3));
  // adding a constant leaves them unchanged
  s = a + 3.0;
  CHECK(near(s.binValueAtGlobal(0),13.0) && near(s.binSystPlusAtGlobal(0),0.3));

  // sum and difference
  HistogramSyst sum = a + b;
  CHECK(near(sum.binValueAtGlobal(0),15.0));
  CHECK(near(sum.binSystPlusAtGlobal(0),0.5) && near(sum.binSystMinusAtGlobal(0),std::sqrt(0.01+0.04)));
  HistogramSyst diff = a - b;
  CHECK(near(diff.binSystPlusAtGlobal(0),std::sqrt(0.09+0.04)) && near(diff.binSystMinusAtGlobal(0),std::sqrt(0.01+0.16)));
  HistogramSyst lin;
  lin.setAsSumOf(a,b,1.0,-2.0);
  CHECK(near(lin.binValueAtGlobal(0),0.0));
  CHECK(near(lin.binSystPlusAtGlobal(0),std::sqrt(0.09+0.16)) && near(lin.binSystMinusAtGlobal(0),std::sqrt(0.01+0.64)));

  // product: d1 = v2, d2 = v1
  HistogramSyst prod = a * b;
  CHECK(near(prod.binValueAtGlobal(1),100.0));
  CHECK(near(prod.binSystPlusAtGlobal(1), std::sqrt(std::pow(5.0*0.6,2)+std::pow(20.0*0.4,2))));
  CHECK(near(prod.binSystMinusAtGlobal(1),std::sqrt(std::pow(5.0*0.2,2)+std::pow(20.0*0.2,2))));

  // ratio: d1 = 1/v2, d2 = -v1/v2^2 (the divisor's sides swap); division by zero: empty bin, no systematics
  HistogramSyst rat = a / b;
  CHECK(near(rat.binValueAtGlobal(0),2.0));
  CHECK(near(rat.binSystPlusAtGlobal(0), std::sqrt(std::pow(0.3/5.0,2)+std::pow(10.0/25.0*0.2,2))));
  CHECK(near(rat.binSystMinusAtGlobal(0),std::sqrt(std::pow(0.1/5.0,2)+std::pow(10.0/25.0*0.4,2))));
  CHECK(rat.binEntriesAtGlobal(3)==0 && rat.binSystPlusAtGlobal(3)==0.0 && rat.binSystMinusAtGlobal(3)==0.0);
  HistogramSyst rr;
  rr.setAsRatioOf(a,b,1.0,2.0);
  CHECK(near(rr.binValueAtGlobal(0),1.0));
  CHECK(near(rr.binSystPlusAtGlobal(0),std::sqrt(std::pow(0.3/10.0,2)+std::pow(10.0/(2.0*25.0)*0.2,2))));

  // inverse: the sides swap
  HistogramSyst inv(a);
  inv.inverse();
  CHECK(near(inv.binValueAtGlobal(0),0.1));
  CHECK(near(inv.binSystPlusAtGlobal(0),0.1/100.0) && near(inv.binSystMinusAtGlobal(0),0.3/100.0));
  HistogramSyst one = 1.0/a;
  CHECK(near(one.binSystPlusAtGlobal(0),0.001));

  // plain Histogram operands have none
  Histogram plain("p","p",4,0.0,4.0);
  for (unsigned int k=0; k<4; k++) plain.setBinAtGlobal(k,1,2.0,0.1);
  HistogramSyst mixed = a + plain;
  CHECK(near(mixed.binValueAtGlobal(0),12.0) && near(mixed.binSystPlusAtGlobal(0),0.3));
  mixed = plain - a;
  CHECK(near(mixed.binSystPlusAtGlobal(0),0.1) && near(mixed.binSystMinusAtGlobal(0),0.3));
  mixed = a / plain;
  CHECK(near(mixed.binSystPlusAtGlobal(0),0.15));
  mixed = plain / a;
  CHECK(near(mixed.binSystPlusAtGlobal(0),2.0/100.0*0.1) && near(mixed.binSystMinusAtGlobal(0),2.0/100.0*0.3));

  // through the virtual functions and compound operators of Histogram
  HistogramSyst c(a);
  Histogram & base = c;
  base += b;
  CHECK(near(c.binSystPlusAtGlobal(0),0.5));
  base.scale(-1.0);
  CHECK(near(c.binSystMinusAtGlobal(0),0.5));
  std::unique_ptr<Histogram> clone(base.clone());
  CHECK(clone->hasSystUncertainties() && near(clone->binSystMinusAtGlobal(0),0.5));
  HistogramSyst v;
  v.setAsSumOf(std::vector<const Histogram *>{&a,&b,&plain},std::vector<double>{1.0,1.0,1.0});
  CHECK(near(v.binValueAtGlobal(0),17.0) && near(v.binSystPlusAtGlobal(0),0.5));
  HistogramSyst pv;
  pv.setAsProductOf(std::vector<const Histogram *>{&a,&b},std::vector<double>{1.0,1.0});
  CHECK(near(pv.binSystPlusAtGlobal(1),prod.binSystPlusAtGlobal(1)));
  HistogramSyst pp;
  pp.setAsProductOf(a,b);
  CHECK(near(pp.binSystMinusAtGlobal(1),prod.binSystMinusAtGlobal(1)));

  // the common systematic uncertainty is combined too
  HistogramSyst ca(a), cb(b);
  ca.setCommonSyst(0.03);
  cb.setCommonSyst(0.04);
  HistogramSyst cs = ca + cb;
  CHECK(near(cs.getCommonSystPlus(),0.05));
  HistogramSyst cr;
  cr.setAsRatioOf(ca,cb);
  CHECK(near(cr.getCommonSystMinus(),0.05));
}

void testRebin()
{
  HistogramSyst h("h","h",5,0.0,5.0);
  for (unsigned int k=0; k<5; k++)
    {
    h.setBinAtGlobal(k,1,10.0,1.0);
    h.setBinSystAtGlobal(k,0.1*(k+1),0.05*(k+1));
    }
  HistogramSyst r = h.rebin(2);   // bins (0,1), (2,3), (4): the last one covers 1 bin out of 2, scaled by 2
  CHECK(r.nBins(0)==3);
  CHECK(near(r.binValueAtGlobal(0),20.0));
  CHECK(near(r.binSystPlusAtGlobal(0),0.3) && near(r.binSystMinusAtGlobal(0),0.15));
  CHECK(near(r.binSystPlusAtGlobal(1),0.7));
  CHECK(near(r.binValueAtGlobal(2),20.0) && near(r.binSystPlusAtGlobal(2),1.0) && near(r.binSystMinusAtGlobal(2),0.5));
  const Histogram & base = h;
  std::unique_ptr<Histogram> rb(base.rebinned(std::vector<unsigned int>{2}));
  CHECK(dynamic_cast<HistogramSyst *>(rb.get())!=nullptr && near(rb->binSystPlusAtGlobal(1),0.7));
  // rank 2
  HistogramSyst h2("h2","h2",2,0.0,2.0,2,0.0,2.0);
  for (unsigned int k=0; k<4; k++) { h2.setBinAtGlobal(k,1,1.0,0.1); h2.setBinSystAtGlobal(k,0.1,0.2); }
  HistogramSyst r2 = h2.rebin(2);
  CHECK(r2.nBinsTotal()==1 && near(r2.binSystPlusAtGlobal(0),0.4) && near(r2.binSystMinusAtGlobal(0),0.8));
}

void testPrecisionAndCopies()
{
  HistogramSyst h = make({1.0,2.0,3.0,4.0},{1.0/3.0,0.2,0.3,0.4},{0.1,0.2,0.3,0.4});
  CHECK(std::fabs(h.binSystPlusAtGlobal(0)-1.0/3.0) > 1.0E-12);   // float
  h.setPrecision(Histogram::kDouble);
  h.setBinSystAtGlobal(0,1.0/3.0,0.1);
  CHECK(std::fabs(h.binSystPlusAtGlobal(0)-1.0/3.0) < 1.0E-15);    // double
  CHECK(near(h.binSystPlusAtGlobal(3),0.4));
  HistogramSyst c(h);
  CHECK(c.precision()==Histogram::kDouble && std::fabs(c.binSystPlusAtGlobal(0)-1.0/3.0) < 1.0E-15);
  HistogramSyst d;
  d = h;
  CHECK(d.nBinsTotal()==4 && near(d.binSystMinusAtGlobal(2),0.3));
  HistogramSyst fromPlain(Histogram("p","p",3,0.0,3.0));
  CHECK(fromPlain.nBinsTotal()==3 && fromPlain.binSystPlusAtGlobal(2)==0.0);
  h.reset();
  CHECK(h.binSystPlusAtGlobal(1)==0.0 && h.binValueAtGlobal(1)==0.0);
}

void testBinary()
{
  HistogramSyst h = make({10.0,0.0,30.0,40.0},{0.3,0.0,0.9,1.2},{0.1,0.0,0.3,0.4});
  h.setBinAtGlobal(1,0,0.0,0.0);
  h.setCommonSyst(0.02,0.01);
  std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
  h.writeBinary(s);
  s.seekg(0);
  HistogramSyst r;
  CHECK(r.readBinary(s));
  CHECK(r.nBinsTotal()==4 && near(r.binValueAtGlobal(2),30.0));
  CHECK(near(r.binSystPlusAtGlobal(2),0.9) && near(r.binSystMinusAtGlobal(3),0.4) && r.binSystPlusAtGlobal(1)==0.0);
  CHECK(near(r.getCommonSystPlus(),0.02) && near(r.getCommonSystMinus(),0.01));
  // symmetric, double precision
  HistogramSyst d("d","d",3,0.0,3.0);
  d.setPrecision(Histogram::kDouble);
  d.setBinAtGlobal(0,1,1.0,0.1);
  d.setBinSystAtGlobal(0,1.0/3.0);
  std::stringstream s2(std::ios::in | std::ios::out | std::ios::binary);
  d.writeBinary(s2);
  s2.seekg(0);
  HistogramSyst dr;
  dr.readBinary(s2);
  CHECK(dr.precision()==Histogram::kDouble && std::fabs(dr.binSystMinusAtGlobal(0)-1.0/3.0) < 1.0E-15);
  // no systematics at all: one flag byte
  HistogramSyst e("e","e",3,0.0,3.0);
  BinaryWriter we, wh;
  e.encodeBinary(we);
  Histogram(e).encodeBinary(wh);
  CHECK(we.buffer().size()==wh.buffer().size()+1);
  // in a folder
  Folder folder("top");
  folder.add(new HistogramSyst(h),"hs");
  folder.add(new StringParameter("S",String("text")),"S");   // a class of Base in the same file: distinct tags
  std::stringstream fs(std::ios::in | std::ios::out | std::ios::binary);
  folder.writeBinary(fs);
  fs.seekg(0);
  Folder read;
  CHECK(read.readBinary(fs));
  const HistogramSyst * hs = read.get<HistogramSyst>("hs");
  CHECK(hs!=nullptr && hs->hasSystUncertainties() && near(hs->binSystPlusAtGlobal(3),1.2));
  const StringParameter * sp = read.get<StringParameter>("S");
  CHECK(sp!=nullptr);
  CHECK(std::string(HistogramSyst::binaryTag())!=StringParameter::binaryTag());
}
}

int main()
{
  testStubs();
  testAccess();
  testArithmetic();
  testRebin();
  testPrecisionAndCopies();
  testBinary();
  std::printf("TestHistogramSyst: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
