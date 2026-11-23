/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestDataPointSyst.cpp
//! @brief Test program of the asymmetric systematic uncertainties of the data points and of the common systematic
//! uncertainty of DataPoints (run by ctest).
//
// TestDataPointSyst -- DataPoint: setters, syst() as the larger side, propagation of the upper and lower sides with the
// swaps (negative scale, difference, inverse, ratio); DataPoint2D, DataPoint3D and DataPointND binary round trips, and
// records of the previous format (one systematic) read with lower = upper; DataPointColumns and DataPointsVector with
// asymmetric points and a common systematic (round trips, sums); Histogram and Profile records with a common
// systematic, and Histogram records of format version 4. Prints each failed check and exits with the number of
// failures (0: all passed).
//
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "BinaryIO.hpp"
#include "DataPoint2D.hpp"
#include "DataPoint3D.hpp"
#include "DataPointND.hpp"
#include "DataPointColumns.hpp"
#include "DataPointsVector.hpp"
#include "Histogram.hpp"
#include "Profile.hpp"
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

//! Write an object as one record to a stream and read it back into another.
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

void testSetters()
{
  DataPoint2D p(1.0,0.1,10.0,0.5,0.3);
  CHECK(near(p.systPlus(),0.3) && near(p.systMinus(),0.3));
  CHECK(!p.hasAsymmetricSyst());
  p.setSyst(0.4,0.2);
  CHECK(p.hasAsymmetricSyst());
  CHECK(near(p.syst(),0.4) && near(p.ySysError(),0.4));
  CHECK(near(p.ySysErrorPlus(),0.4) && near(p.ySysErrorMinus(),0.2));
  p.setSyst(0.1,0.6);
  CHECK(near(p.syst(),0.6));
  CHECK(near(p.errorPlus(),std::sqrt(0.25+0.01)));
  CHECK(near(p.errorMinus(),std::sqrt(0.25+0.36)));
  CHECK(near(p.error(),std::sqrt(0.25+0.36)));
  CHECK(throws([&p]() { p.setSyst(-0.1,0.2); }));
  CHECK(throws([&p]() { p.setSyst(0.1,-0.2); }));
  CHECK(throws([]() { DataPoint2D q; q.setSyst(-1.0); }));
  p.setSyst(0.25);
  CHECK(!p.hasAsymmetricSyst() && near(p.systMinus(),0.25));
  p.clear();
  CHECK(p.systPlus()==0.0f && p.systMinus()==0.0f);
}

void testPropagation()
{
  // scale: a negative factor swaps the sides
  DataPoint2D p(1.0,0.0,10.0,1.0,0.0);
  p.setSyst(0.4,0.2);
  DataPoint2D q(p);
  q.scale(2.0);
  CHECK(near(q.systPlus(),0.8) && near(q.systMinus(),0.4));
  q = p;
  q.scale(-2.0);
  CHECK(near(q.value(),-20.0));
  CHECK(near(q.systPlus(),0.4) && near(q.systMinus(),0.8));
  // unary minus swaps too
  DataPoint2D m = -p;
  CHECK(near(m.systPlus(),0.2) && near(m.systMinus(),0.4));

  // sum: each side in quadrature
  DataPoint2D a(1.0,0.0,10.0,1.0,0.0), b(1.0,0.0,5.0,1.0,0.0);
  a.setSyst(0.3,0.1);
  b.setSyst(0.4,0.2);
  DataPoint2D s = a + b;
  CHECK(near(s.value(),15.0));
  CHECK(near(s.systPlus(),0.5) && near(s.systMinus(),std::sqrt(0.01+0.04)));
  // difference: the sides of the subtrahend swap
  DataPoint2D d = a - b;
  CHECK(near(d.value(),5.0));
  CHECK(near(d.systPlus(),std::sqrt(0.09+0.04)) && near(d.systMinus(),std::sqrt(0.01+0.16)));
  // product: d1 = v2, d2 = v1 (both positive)
  DataPoint2D pr = a * b;
  CHECK(near(pr.systPlus(),std::sqrt(std::pow(5.0*0.3,2)+std::pow(10.0*0.4,2))));
  CHECK(near(pr.systMinus(),std::sqrt(std::pow(5.0*0.1,2)+std::pow(10.0*0.2,2))));
  // ratio: d1 = 1/v2, d2 = -v1/v2^2 (the denominator's sides swap)
  DataPoint2D r = a / b;
  CHECK(near(r.value(),2.0));
  CHECK(near(r.systPlus(), std::sqrt(std::pow(0.3/5.0,2)+std::pow(10.0/25.0*0.2,2))));
  CHECK(near(r.systMinus(),std::sqrt(std::pow(0.1/5.0,2)+std::pow(10.0/25.0*0.4,2))));
  // inverse: d(1/v)/dv < 0, the sides swap
  DataPoint2D i(a);
  i.inverse();
  CHECK(near(i.value(),0.1));
  CHECK(near(i.systPlus(),0.1/100.0) && near(i.systMinus(),0.3/100.0));
  // symmetric uncertainties behave as before
  DataPoint2D e(1.0,0.0,10.0,1.0,0.3), f(1.0,0.0,5.0,1.0,0.4);
  DataPoint2D g = e - f;
  CHECK(!g.hasAsymmetricSyst() && near(g.syst(),0.5));
}

void testPointRecords()
{
  // DataPoint2D: asymmetric, symmetric, and an asymmetric point whose lower side is zero
  DataPoint2D p(1.0,0.1,10.0,0.5,0.0);
  p.setSyst(0.4,0.2);
  DataPoint2D c = roundTrip(p);
  CHECK(near(c.x(),1.0) && near(c.y(),10.0) && near(c.yStatError(),0.5));
  CHECK(near(c.systPlus(),0.4) && near(c.systMinus(),0.2));
  p.setSyst(0.3);
  c = roundTrip(p);
  CHECK(!c.hasAsymmetricSyst() && near(c.syst(),0.3));
  p.setSyst(0.3,0.0);
  c = roundTrip(p);
  CHECK(near(c.systPlus(),0.3) && c.systMinus()==0.0f);
  p.setSyst(0.0,0.3);
  c = roundTrip(p);
  CHECK(c.systPlus()==0.0f && near(c.systMinus(),0.3));

  // a point of the previous format: one systematic uncertainty, lower = upper
  {
  BinaryWriter w;
  w.writeUInt8(uint8_t((1u<<2)|(1u<<4)));   // value and syst
  w.writeFloat32(7.0f);
  w.writeFloat32(0.7f);
  DataPoint2D old;
  BinaryReader r(w.buffer().data(),w.buffer().size());
  old.decodeBinary(r);
  CHECK(near(old.y(),7.0) && near(old.systPlus(),0.7) && near(old.systMinus(),0.7));
  }

  // DataPoint3D
  DataPoint3D t(1.0,0.1,2.0,0.2,30.0,1.0,0.0);
  t.setSyst(0.5,0.25);
  DataPoint3D tc = roundTrip(t);
  CHECK(near(tc.z(),30.0) && near(tc.zSysErrorPlus(),0.5) && near(tc.zSysErrorMinus(),0.25));
  CHECK(near(tc.zSysError(),0.5));

  // DataPointND: format 2, and format 1 read with lower = upper
  DataPointND n(std::vector<double>{1.0,2.0,3.0},40.0,2.0,0.0);
  n.setSyst(0.6,0.9);
  DataPointND nc = roundTrip(n);
  CHECK(nc.rank()==3 && near(nc.value(),40.0));
  CHECK(near(nc.systPlus(),0.6) && near(nc.systMinus(),0.9) && near(nc.syst(),0.9));
  {
  BinaryWriter w;
  w.writeVarUInt(3);
  for (int i=0; i<3; i++) { w.writeFloat32(float(i)); w.writeFloat32(0.0f); }
  w.writeFloat32(5.0f);
  w.writeFloat32(0.5f);
  w.writeFloat32(0.25f);
  DataPointND old;
  BinaryReader r(w.buffer().data(),w.buffer().size());
  old.decodeBinary(r,1);
  CHECK(r.remaining()==0);
  CHECK(near(old.value(),5.0) && near(old.systPlus(),0.25) && near(old.systMinus(),0.25));
  }
}

void testCommonSyst()
{
  DataPointsVector v;
  CHECK(!v.hasCommonSyst());
  v.setCommonSyst(0.02);
  CHECK(v.hasCommonSyst() && !v.hasAsymmetricCommonSyst());
  CHECK(near(v.getCommonSystPlus(),0.02) && near(v.getCommonSystMinus(),0.02));
  v.setCommonSyst(0.03,0.01);
  CHECK(v.hasAsymmetricCommonSyst() && near(v.getCommonSyst(),0.03));
  CHECK(throws([&v]() { v.setCommonSyst(-0.01,0.0); }));
  CHECK(near(v.getCommonSystPlus(),0.03));   // unchanged after the failed call
  // copies keep it
  DataPointsVector w(v);
  CHECK(near(w.getCommonSystPlus(),0.03) && near(w.getCommonSystMinus(),0.01));
  DataPointsVector x;
  x = v;
  CHECK(near(x.getCommonSystMinus(),0.01));
  v.clearCommonSyst();
  CHECK(!v.hasCommonSyst());
}

void testDataPointsVector()
{
  DataPointsVector v;
  for (int k=0; k<4; k++)
    {
    DataPoint2D p(k+0.5,0.5,10.0*(k+1),1.0,0.0);
    p.setSyst(0.1*(k+1),0.05*(k+1));
    v.addPoint(p);
    }
  v.setCommonSyst(0.04,0.02);
  DataPointsVector c = roundTrip(v);
  CHECK(c.size()==4);
  CHECK(near(c.sysErrorPlusAt(2),0.3) && near(c.sysErrorMinusAt(2),0.15) && near(c.sysErrorAt(2),0.3));
  CHECK(near(c.getCommonSystPlus(),0.04) && near(c.getCommonSystMinus(),0.02));
  // sums: points in quadrature, common in quadrature; differences swap the sides of the second operand
  DataPointsVector b(v);
  b.setCommonSyst(0.03,0.0);
  DataPointsVector s = v + b;
  CHECK(near(s.getCommonSystPlus(),0.05) && near(s.getCommonSystMinus(),0.02));
  DataPointsVector d = v - b;
  CHECK(near(d.getCommonSystPlus(),0.04) && near(d.getCommonSystMinus(),std::sqrt(0.0004+0.0009)));
  CHECK(near(d.sysErrorPlusAt(0),std::sqrt(0.01+0.0025)));

  // rank 3 points (DataPointND) with asymmetric systematics
  std::vector<DataPointND> points;
  for (int k=0; k<3; k++)
    {
    DataPointND p(std::vector<double>{1.0*k,2.0*k,3.0*k},5.0+k,0.5,0.0);
    p.setSyst(0.2,0.1+k);
    points.push_back(p);
    }
  DataPointsVector nd(points);
  DataPointsVector ndc = roundTrip(nd);
  CHECK(ndc.size()==3 && ndc.rank()==3);
  CHECK(near(ndc.sysErrorPlusAt(2),0.2) && near(ndc.sysErrorMinusAt(2),2.1));

  // a record of format version 3 (rank 1): no common systematic at the end
  {
  DataPointsVector u;
  u.addPoint(DataPoint2D(1.0,0.0,2.0,0.1,0.2));
  BinaryWriter w;
  u.encodeBinary(w);
  std::string payload = w.buffer().substr(0,w.buffer().size()-1);   // without the common systematic flag
  BinaryReader r(payload.data(),payload.size());
  DataPointsVector old;
  old.setCommonSyst(0.5);
  old.decodeBinary(r,"",3);
  CHECK(r.remaining()==0 && old.size()==1 && near(old.sysErrorAt(0),0.2));
  }
}

void testDataPointColumns()
{
  DataPointColumns c;
  c.setColumns({{1.0,2.0,3.0}},{},{10.0,20.0,30.0},{1.0,2.0,3.0},{0.3,0.6,0.9},{0.1,0.2,0.3});
  CHECK(near(c.sysErrorPlusAt(1),0.6) && near(c.sysErrorMinusAt(1),0.2) && near(c.sysErrorAt(1),0.6));
  DataPoint2D p = c.point(2);
  CHECK(near(p.systPlus(),0.9) && near(p.systMinus(),0.3));
  c.setCommonSyst(0.05,0.03);
  DataPointColumns r = roundTrip(c);
  CHECK(r.size()==3 && near(r.sysErrorMinusAt(2),0.3) && near(r.sysErrorPlusAt(0),0.3));
  CHECK(near(r.getCommonSystPlus(),0.05) && near(r.getCommonSystMinus(),0.03));
  // symmetric columns: no lower column written, lower = upper when read
  DataPointColumns s;
  s.setColumns({{1.0,2.0}},{},{1.0,2.0},{},{0.1,0.2});
  DataPointColumns sr = roundTrip(s);
  CHECK(near(sr.sysErrorMinusAt(1),0.2) && !sr.hasCommonSyst());
  // points added one by one keep both sides; sorting moves them together
  DataPointColumns a;
  DataPoint2D q(5.0,0.0,1.0,0.1,0.0);
  q.setSyst(0.4,0.1);
  a.addPoint(q);
  a.addPoint(DataPoint2D(2.0,0.0,2.0,0.1,0.3));
  a.sortByX(0);
  CHECK(near(a.sysErrorPlusAt(0),0.3) && near(a.sysErrorPlusAt(1),0.4) && near(a.sysErrorMinusAt(1),0.1));
  // differences: points and common systematics
  DataPointColumns b(c);
  DataPointColumns d = c - b;
  CHECK(near(d.sysErrorPlusAt(0),std::sqrt(0.09+0.01)));
  CHECK(near(d.getCommonSystPlus(),std::sqrt(0.0025+0.0009)));
  // the table of the data pane shows both sides
  std::vector<String> cells = c.rowValues(0);
  CHECK(std::string(cells.back().c_str()).find('-')!=std::string::npos);
}

void testHistogramRecords()
{
  Histogram h("h","title",5,0.0,5.0);
  for (int i=0; i<5; i++) h.fill(i+0.5,double(i+1));
  h.setCommonSyst(0.025,0.015);
  Histogram c = roundTrip(h);
  CHECK(near(c.binValueAt(2),3.0));
  CHECK(near(c.getCommonSystPlus(),0.025) && near(c.getCommonSystMinus(),0.015));
  Histogram copy(h);
  CHECK(near(copy.getCommonSystPlus(),0.025));
  // format version 4: no common systematic
  {
  Histogram u("u","u",3,0.0,3.0);
  u.fill(1.5);
  BinaryWriter w;
  u.encodeBinary(w);
  std::string payload = w.buffer().substr(0,w.buffer().size()-1);
  BinaryReader r(payload.data(),payload.size());
  Histogram old;
  old.setCommonSyst(0.5);
  old.decodeBinary(r,"u",4);
  CHECK(r.remaining()==0 && near(old.binValueAt(1),1.0) && !old.hasCommonSyst());
  }
  // profiles carry it in their histogram part
  Profile p("p","p",3,0.0,3.0);
  p.fill(0.5,2.0);
  p.fill(1.5,4.0);
  p.setCommonSyst(0.1);
  Profile pc = roundTrip(p);
  CHECK(near(pc.binValueAt(1),4.0) && near(pc.getCommonSyst(),0.1));
}
}

int main()
{
  testSetters();
  testPropagation();
  testPointRecords();
  testCommonSyst();
  testDataPointsVector();
  testDataPointColumns();
  testHistogramRecords();
  std::printf("TestDataPointSyst: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
