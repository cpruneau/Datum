/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestTableConversion.cpp
//! @brief Test program of TableConversion and of the covariance column and the storage of DataPointColumns (ctest).
//
// TestTableConversion -- the covariance of x and y in DataPointColumns (set, kept by points, copies, sorting, removal,
// invalid values refused); the records of DataPointColumns (format version 4: all-zero columns and a lower systematic
// equal to the upper one left out; read back exactly); tables to data points (roles by name and by number, rows with
// missing values, invalid covariances) and data points and histograms to tables. Prints each failed check and exits
// with the number of failures (0: all passed).
//
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "DataPointColumns.hpp"
#include "DataPointsVector.hpp"
#include "DataPoint2D.hpp"
#include "Histogram.hpp"
#include "MathExceptions.hpp"
#include "Table2D.hpp"
#include "TableConversion.hpp"

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
#define CHECK(condition, what) check((condition), (what), __LINE__)

bool near(double a, double b, double tolerance=1e-6) { return std::fabs(a-b) <= tolerance*std::max(1.0, std::fabs(b)); }

template <class T> std::string record(const T & object) { std::ostringstream out; object.writeBinary(out); return out.str(); }

template <class T> T readBack(const std::string & bytes)
{
  T object;
  std::istringstream in(bytes);
  object.readBinary(in);
  return object;
}

bool sameColumns(const DataPointColumns & a, const DataPointColumns & b)
{
  if (a.rank()!=b.rank() || a.size()!=b.size()) return false;
  for (unsigned int i=0; i<a.rank(); i++)
    if (a.xColumn(i)!=b.xColumn(i) || a.xErrorColumn(i)!=b.xErrorColumn(i)) return false;
  return a.valueColumn()==b.valueColumn() && a.statColumn()==b.statColumn() && a.systColumn()==b.systColumn()
         && a.systMinusColumn()==b.systMinusColumn() && a.covarianceColumn()==b.covarianceColumn();
}

void testCovariance()
{
  DataPointColumns points({1, 2, 3}, {0.1, 0.2, 0.3}, {10, 20, 30}, {1, 2, 3}, {});
  CHECK(!points.hasCovariance() && points.covarianceAt(1)==0.0, "no covariance at first");
  points.setCovarianceColumn({0.05, -0.2, 0.0});
  CHECK(points.hasCovariance() && near(points.covarianceAt(0), 0.05) && near(points.covarianceAt(1), -0.2), "covariance set");
  DataPoint2D p = points.point(1);
  CHECK(near(p.xyCovariance(), -0.2) && near(p.xError(), 0.2) && near(p.stat(), 2.0), "point keeps covariance and uncertainties");
  bool refused = false;
  try { points.setCovarianceColumn({0.05, 5.0, 0.0}); } catch (const MathException &) { refused = true; }
  CHECK(refused && near(points.covarianceAt(1), -0.2), "invalid covariance refused, column unchanged");
  points.addPoint(DataPoint2D(4, 0.4, 40, 4, 0));          // no covariance: 0
  CHECK(points.size()==4 && points.covarianceAt(3)==0.0, "point added without covariance");
  DataPoint2D q(0.5, 0.5, 5, 1, 0);
  q.setErrorMatrix(0.25, 0.3, 1.0);
  points.addPoint(q);
  CHECK(near(points.covarianceAt(4), 0.3), "point added with its covariance");
  points.sortByX();
  CHECK(near(points.covarianceAt(0), 0.3) && near(points.valueAt(0), 5), "covariance follows sorting");
  points.removePoint(0);
  CHECK(points.size()==4 && near(points.covarianceAt(0), 0.05), "covariance follows removal");
  DataPointColumns empty({1, 2}, {10, 20});
  empty.addPoint(DataPoint2D(3, 0, 30, 0, 0));
  CHECK(!empty.hasCovariance(), "no covariance column made for points without covariance");
}

void testStorage()
{
  // x and y only: 2 columns written
  std::vector<double> x, y;
  for (int k=0; k<1000; k++) { x.push_back(0.1*k); y.push_back(100.0 + k%7); }
  DataPointColumns xy(x, y);
  std::string bytes = record(xy);
  CHECK(bytes.size() < 1000*8 + 100, "x and y only: about 8 bytes per point (" + std::to_string(bytes.size()) + " bytes)");
  DataPointColumns back = readBack<DataPointColumns>(bytes);
  CHECK(sameColumns(xy, back), "x and y read back exactly");

  // every column, asymmetric systematic, covariance, rank 1
  DataPointColumns full;
  full.setColumns({{1, 2, 3}}, {{0.1, 0.2, 0.3}}, {10, 20, 30}, {1, 2, 3}, {0.5, 0.6, 0.7}, {0.4, 0.6, 0.8});
  full.setCovarianceColumn({0.01, 0.0, -0.1});
  full.setTitle("a title");
  DataPointColumns fullBack = readBack<DataPointColumns>(record(full));
  CHECK(sameColumns(full, fullBack) && fullBack.title()=="a title", "all columns, covariance and title read back");

  // rank 2
  DataPointColumns map;
  map.setColumns({{1, 2}, {3, 4}}, {}, {5, 6}, {0.5, 0.6}, {});
  DataPointColumns mapBack = readBack<DataPointColumns>(record(map));
  CHECK(sameColumns(map, mapBack) && mapBack.rank()==2, "rank 2 read back");

  // -0 is kept (not an all-zero column)
  DataPointColumns signedZero({1}, {-0.0});
  DataPointColumns signedBack = readBack<DataPointColumns>(record(signedZero));
  CHECK(std::signbit(signedBack.valueColumn()[0]), "-0 kept");

  // empty
  DataPointColumns none;
  CHECK(readBack<DataPointColumns>(record(none)).size()==0, "empty read back");
}

void testTableToPoints()
{
  Table2D table("spectrum");
  table.addRow({1, 0.1, 10, 1, 0.01});
  table.addRow({2, 0.2, NAN, 2, 0.0});      // value missing: left out
  table.addRow({3, -0.3, 30, NAN, 9.0});    // negative x uncertainty, missing stat, invalid covariance
  table.addRow({4, 0.4, 40, 4, -0.1});
  table.setColumnNames({"x", "x_err", "y", "stat", "cov_xy"});
  TableColumnRoles roles = TableColumnRoles::guess(table);
  CHECK(roles.x==std::vector<int>{0} && roles.xError==std::vector<int>{1} && roles.value==2 && roles.stat==3 && roles.covariance==4,
        "roles found from the names");
  TableConversion::Report report;
  std::unique_ptr<DataPointColumns> points = TableConversion::toDataPoints(table, roles, report);
  CHECK(points->size()==3 && report.points==3 && report.skipped==1, "row without value left out");
  CHECK(report.negativeErrors==1 && report.missingErrors==1 && report.invalidCovariances==1, "uncertainties and covariance repaired: " + report.summary().str());
  CHECK(near(points->point(1).xError(), 0.3) && points->statErrorAt(1)==0.0 && points->covarianceAt(1)==0.0, "repaired values");
  CHECK(near(points->covarianceAt(0), 0.01) && near(points->covarianceAt(2), -0.1), "covariances kept");
  CHECK(points->name()=="spectrum", "points named after the table");

  // by number: 2, 3, 4 columns
  Table2D two("t2"); two.addRow({1, 2}); two.addRow({3, 4});
  TableColumnRoles r2 = TableColumnRoles::guess(two);
  CHECK(r2.x==std::vector<int>{0} && r2.value==1 && r2.stat<0, "2 columns: x, y");
  Table2D four("t4"); four.addRow({1, 0.1, 2, 0.2});
  TableColumnRoles r4 = TableColumnRoles::guess(four);
  CHECK(r4.xError==std::vector<int>{1} && r4.value==2 && r4.stat==3, "4 columns: x, x_err, y, stat");
  Table2D one("t1"); one.addRow({1});
  CHECK(!TableColumnRoles::guess(one).valid(), "1 column: no roles");
  bool refused = false;
  TableColumnRoles bad; bad.x = {0}; bad.value = 7;
  try { TableConversion::toDataPoints(two, bad, report); } catch (const MathException &) { refused = true; }
  CHECK(refused, "role beyond the columns refused");
}

void testPointsToTable()
{
  DataPointColumns points;
  points.setColumns({{1, 2}}, {{0.1, 0.2}}, {10, 20}, {1, 2}, {0.5, 0.5}, {0.3, 0.4});
  points.setCovarianceColumn({0.02, 0.0});
  Table2D t = TableConversion::toTable(points);
  CHECK(t.nRows()==2 && t.nColumns()==7, "7 columns: x, x_err, y, stat, syst_plus, syst_minus, cov_xy");
  CHECK(t.columnName(0)=="x" && t.columnName(2)=="y" && t.columnName(6)=="cov_xy", "column names");
  // and back: the same points
  TableConversion::Report report;
  std::unique_ptr<DataPointColumns> back = TableConversion::toDataPoints(t, TableColumnRoles::guess(t), report);
  CHECK(sameColumns(points, *back), "points -> table -> points");

  // trivial columns left out
  DataPointColumns xy({1, 2, 3}, {4, 5, 6});
  Table2D t2 = TableConversion::toTable(xy);
  CHECK(t2.nColumns()==2 && t2.columnName(1)=="y", "x and y only");
  CHECK(TableConversion::toTable(xy, true).nColumns()==7, "all columns asked for (the covariance included)");

  // a DataPointsVector with covariance
  DataPoint2D p(1, 0.5, 2, 1, 0);
  p.setErrorMatrix(0.25, 0.2, 1.0);
  DataPointsVector v;
  v.addPoint(p);
  Table2D t3 = TableConversion::toTable(v);
  CHECK(t3.columnIndex("cov_xy")>=0 && near(t3(0, unsigned(t3.columnIndex("cov_xy"))), 0.2), "covariance of a DataPointsVector");

  // empty
  CHECK(TableConversion::toTable(DataPointColumns()).nRows()==0, "no point: empty table");
}

void testHistogramToTable()
{
  Histogram h("h", "title", 4, 0.0, 2.0);
  h.fill(0.1); h.fill(0.2); h.fill(1.7, 2.0);
  Table2D t = TableConversion::toTable(h);
  CHECK(t.nRows()==4 && t.nColumns()==6, "4 bins, 6 columns: x, x_low, x_high, value, stat, entries");
  CHECK(near(t(0, 0), 0.25) && near(t(0, 1), 0.0) && near(t(0, 2), 0.5) && near(t(0, 3), 2.0) && near(t(0, 5), 2.0), "first bin");
  CHECK(near(t(3, 3), 2.0) && near(t(3, 4), 2.0), "weighted bin: value and uncertainty");
  CHECK(TableConversion::toTable(h, false).nRows()==2, "empty bins left out on request");
  Histogram h2("h2", "", 2, 0.0, 1.0, 3, 0.0, 3.0);
  h2.fill(0.7, 2.5, 1.0);
  Table2D t2 = TableConversion::toTable(h2);
  CHECK(t2.nRows()==6 && t2.nColumns()==9 && t2.columnName(3)=="x1", "rank 2: x0, x0_low, x0_high, x1, ...");
  CHECK(near(t2(5, 0), 0.75) && near(t2(5, 3), 2.5) && near(t2(5, 6), 1.0), "axis 0 varies fastest");
}
} // namespace

int main()
{
  testCovariance();
  testStorage();
  testTableToPoints();
  testPointsToTable();
  testHistogramToTable();
  std::printf("TestTableConversion: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
