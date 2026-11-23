/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestVariableBins.cpp
//! @brief Test program of the variable bins of Partition and Histogram (run by ctest).
//
// TestVariableBins -- Partition: axes with variable bins (edges, widths, centers, bin of a coordinate, volumes, regular
// axes unchanged), invalid edges, setBinEdges(), compatibility, extraction of axes and ranges; Histogram: filling,
// integrals with the widths of the bins, extract1(), projections, rebinning (merged edges, the last bin not scaled),
// copies, rank 2 with one variable axis; binary records (Histogram version 6 and records of version 5, HistogramSyst,
// Profile, Folder). Prints each failed check and exits with the number of failures (0: all passed).
//
#include <cmath>
#include <cstring>
#include <limits>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "Partition.hpp"
#include "Histogram.hpp"
#include "HistogramSyst.hpp"
#include "Profile.hpp"
#include "Folder.hpp"
#include "BinaryIO.hpp"
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

bool near(double a, double b, double tolerance=1.0E-9) { return std::fabs(a-b) <= tolerance*(1.0+std::fabs(b)); }

#define CHECK(c) check((c), #c, __LINE__)

template <class T>
bool throws(T f) { try { f(); } catch (const Exception &) { return true; } return false; }

template <class T>
T roundTrip(const T & object)
{
  std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
  object.writeBinary(s);
  s.seekg(0);
  T copy;
  copy.readBinary(s);
  return copy;
}

const std::vector<double> kEdges = {1.2, 1.4, 1.5, 2.0, 3.0, 5.3, 20.0};

void testPartition()
{
  Partition p;
  p.addDimension(kEdges,"pt");
  CHECK(p.rank()==1 && p.nBins(0)==6 && p.nBinsTotal()==6 && !p.isRegular(0) && !p.isRegular());
  CHECK(p.minimum(0)==float(1.2) && p.maximum(0)==20.0f);
  CHECK(p.binLowEdge(0,0)==1.2 && p.binHighEdge(0,0)==1.4 && p.binLowEdge(5,0)==5.3 && p.binHighEdge(5,0)==20.0);
  CHECK(near(p.binWidth(2,0),0.5) && near(p.binWidth(5,0),14.7) && near(p.binCenter(4,0),4.15));
  CHECK(p.binEdges(0)==kEdges);
  CHECK(p.binIndex(1.2)==0 && p.binIndex(1.3999)==0 && p.binIndex(1.4)==1 && p.binIndex(4.0)==4 && p.binIndex(19.99)==5);
  CHECK(p.binIndex(1.1999)==-1 && p.binIndex(20.0)==-1 && p.binIndex(std::nan(""))==-1);
  CHECK(throws([&p]() { p.binWidth(6,0); }) && throws([&p]() { p.binLowEdge(6,0); }));
  CHECK(near(p.binVolume(5),14.7));
  // invalid edges
  Partition q;
  CHECK(throws([&q]() { q.addDimension(std::vector<double>{1.0},"x"); }));
  CHECK(throws([&q]() { q.addDimension(std::vector<double>{1.0,1.0,2.0},"x"); }));
  CHECK(throws([&q]() { q.addDimension(std::vector<double>{2.0,1.0},"x"); }));
  CHECK(throws([&q]() { q.addDimension(std::vector<double>{0.0,std::numeric_limits<double>::infinity()},"x"); }));
  CHECK(q.rank()==0);
  // regular axes are unchanged; setBinEdges() makes an axis variable
  Partition r;
  r.addDimension(4,0.0,2.0,"x");
  r.addDimension(kEdges,"y");
  CHECK(r.isRegular(0) && !r.isRegular(1) && r.binEdges(0)==(std::vector<double>{0.0,0.5,1.0,1.5,2.0}));
  CHECK(r.binIndex(0.75,4.0)==1+4*4 && near(r.binVolume(1+4*4),0.5*2.3));
  CHECK(throws([&r]() { r.setBinEdges(0,std::vector<double>{0.0,1.0}); }));
  r.setBinEdges(0,std::vector<double>{0.0,0.1,0.2,1.0,2.0});
  CHECK(!r.isRegular(0) && r.binIndex(0.15,1.3)==1 && r.minimum(0)==0.0f);
  // compatibility: the edges count; a regular axis equals variable bins with its edges
  Partition a, b, c;
  a.addDimension(kEdges,"x");
  std::vector<double> other = kEdges;
  other[3] = 2.5;
  b.addDimension(other,"x");
  CHECK(a.compatibleWith(a) && !a.compatibleWith(b) && a.compatibleWith(b,false));
  c.addDimension(4,0.0,2.0,"x");
  Partition d;
  d.addDimension(std::vector<double>{0.0,0.5,1.0,1.5,2.0},"x");
  CHECK(c.compatibleWith(d) && d.compatibleWith(c));
  // extraction: axes and ranges keep the edges
  Partition e = r.extractPartition(1);
  CHECK(e.rank()==1 && !e.isRegular(0) && e.binEdges(0)==kEdges && e.dimensionTitle(0)=="y");
  Partition f;
  f.addDimension(a,0,2,4);
  CHECK(f.nBins(0)==3 && f.binLowEdge(0,0)==1.5 && f.binHighEdge(2,0)==5.3);
  Partition g;
  g.addDimension(c,0,1,2);
  CHECK(g.isRegular(0) && g.nBins(0)==2 && g.minimum(0)==0.5f && g.maximum(0)==1.5f);
  Partition copy(r), assigned;
  assigned = r;
  CHECK(copy.compatibleWith(r) && !copy.isRegular(0) && assigned.binEdges(1)==kEdges);
  r.clear();
  CHECK(r.rank()==0 && r.isRegular());
}

void testHistogram()
{
  Histogram h("h","spectrum",kEdges);
  CHECK(h.nBins(0)==6 && !h.isRegular(0) && h.dimensionTitle(0)=="x");
  h.fill(1.3);
  h.fill(1.45,2.0);
  h.fill(10.0,3.0);
  h.fill(25.0);                                    // outside: not counted
  CHECK(h.binValueAtGlobal(0)==1.0 && h.binValueAtGlobal(1)==2.0 && h.binValueAtGlobal(5)==3.0);
  CHECK(h.binEntriesAtGlobal(5)==1);
  CHECK(near(h.integral(),6.0) && near(h.integral(false),1.0*0.2+2.0*0.1+3.0*14.7));
  CHECK(near(h.integral(0,1,false),1.0*0.2+2.0*0.1) && near(h.integral(0,1),3.0));
  // extraction of a range of bins
  Histogram x = h.extract1(1,5);
  CHECK(x.nBins(0)==5 && x.binLowEdge(0,0)==1.4 && x.binHighEdge(4,0)==20.0 && x.binValueAtGlobal(4)==3.0);
  // rebinning: the edges of the merged bins; the last, incomplete one is not extended nor scaled
  Histogram r2 = h.rebin(2);
  CHECK(r2.nBins(0)==3 && !r2.isRegular(0));
  CHECK(r2.binEdges(0)==(std::vector<double>{1.2,1.5,3.0,20.0}));
  CHECK(r2.binValueAtGlobal(0)==3.0 && r2.binValueAtGlobal(2)==3.0);
  Histogram r4 = h.rebin(4);
  CHECK(r4.binEdges(0)==(std::vector<double>{1.2,3.0,20.0}) && r4.binValueAtGlobal(1)==3.0);   // 2 bins merged, not scaled by 2
  std::unique_ptr<Histogram> rb(h.rebinned(std::vector<unsigned int>{3}));
  CHECK(rb->binEdges(0)==(std::vector<double>{1.2,2.0,20.0}));
  // arithmetic needs the same edges
  Histogram same(h), different("d","d",6,1.2,20.0);
  same += h;
  CHECK(same.binValueAtGlobal(5)==6.0);
  CHECK(throws([&]() { different += h; }));
  // rank 2: a regular and a variable axis
  Partition p;
  p.addDimension(2,-1.0,1.0,"eta");
  p.addDimension(kEdges,"pt");
  Histogram h2(p);
  h2.fill(0.5,4.0,2.0);
  CHECK(h2.binValueAt(1u,4u)==2.0 && near(h2.integral(false),2.0*1.0*2.3));
  Histogram py = h2.project2DOnto1D(1);
  CHECK(!py.isRegular(0) && py.binEdges(0)==kEdges && py.binValueAtGlobal(4)==2.0);
  // HistogramSyst: rebinning sums the systematics linearly, without scaling the last bin
  HistogramSyst s("s","s",kEdges);
  for (unsigned int k=0; k<6; k++) { s.setBinAtGlobal(k,1,1.0,0.1); s.setBinSystAtGlobal(k,0.1*(k+1),0.05); }
  HistogramSyst sr = s.rebin(4);
  CHECK(sr.nBins(0)==2 && near(sr.binSystPlusAtGlobal(1),0.5+0.6,1e-6) && near(sr.binValueAtGlobal(1),2.0));
}

void testRecords()
{
  Histogram h("h","spectrum",kEdges);
  h.setDimensionTitle(0,"PT (GEV/c)");
  for (unsigned int k=0; k<6; k++) h.setBinAtGlobal(k,k+1,10.0*k,1.0);
  h.setCommonSyst(0.02);
  Histogram r = roundTrip(h);
  CHECK(!r.isRegular(0) && r.binEdges(0)==kEdges && r.compatibleWith(h) && r.binValueAtGlobal(4)==40.0);
  CHECK(r.binEntriesAtGlobal(4)==5 && near(r.getCommonSystPlus(),0.02) && r.dimensionTitle(0)=="PT (GEV/c)");
  // regular histograms: one more byte (no variable axis), read as before
  Histogram regular("g","g",5,0.0,1.0);
  regular.fill(0.5);
  BinaryWriter w;
  regular.encodeBinary(w);
  std::string payload = w.buffer().substr(0,w.buffer().size()-1);   // a record of version 5
  BinaryReader reader(payload.data(),payload.size());
  Histogram old;
  old.decodeBinary(reader,"g",5);
  CHECK(reader.remaining()==0 && old.isRegular(0) && old.binValueAtGlobal(2)==1.0);
  CHECK(roundTrip(regular).isRegular(0));
  // HistogramSyst and Profile
  HistogramSyst s("s","s",kEdges);
  s.setBinAtGlobal(2,1,5.0,0.5);
  s.setBinSystAtGlobal(2,0.3,0.2);
  HistogramSyst sr = roundTrip(s);
  CHECK(!sr.isRegular(0) && sr.binEdges(0)==kEdges && near(sr.binSystMinusAtGlobal(2),0.2,1e-6));
  Partition pp;
  pp.addDimension(kEdges,"pt");
  Profile p(pp);
  p.fill(1.3,2.0);
  p.fill(1.35,4.0);
  Profile pr = roundTrip(p);
  CHECK(!pr.isRegular(0) && pr.binEdges(0)==kEdges && near(pr.binValueAtGlobal(0),3.0));
  // in a folder
  Folder folder("top");
  folder.add(new Histogram(h),"h");
  folder.add(new HistogramSyst(s),"s");
  std::stringstream fs(std::ios::in | std::ios::out | std::ios::binary);
  folder.writeBinary(fs);
  fs.seekg(0);
  Folder read;
  CHECK(read.readBinary(fs));
  const Histogram * hh = read.get<Histogram>("h");
  const HistogramSyst * hs = read.get<HistogramSyst>("s");
  CHECK(hh && hh->binEdges(0)==kEdges && hs && hs->binEdges(0)==kEdges);
  // corrupted edges are refused
  BinaryWriter bad;
  h.encodeBinary(bad);
  std::string b = bad.buffer();
  double wrong = 1.0e6;                                             // the second edge above the third
  std::memcpy(&b[b.size()-6*8],&wrong,8);
  BinaryReader badReader(b.data(),b.size());
  Histogram broken;
  CHECK(throws([&]() { broken.decodeBinary(badReader,"h",6); }));
}
}

int main()
{
  testPartition();
  testHistogram();
  testRecords();
  std::printf("TestVariableBins: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
