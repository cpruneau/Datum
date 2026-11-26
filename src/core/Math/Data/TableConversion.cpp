/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TableConversion.cpp
//! @brief Implementation of TableConversion.

#include "TableConversion.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include "DataPoint2D.hpp"
#include "MathExceptions.hpp"

namespace Datum
{
  namespace MATH
  {

  namespace
  {
  std::string lower(const String & s)
  {
  std::string t = s.str();
  for (char & c : t) c = char(std::tolower((unsigned char)c));
  return t;
  }

  //! Index of the first column whose name (any case) is one of names, or -1.
  int findColumn(const std::vector<std::string> & columns, std::initializer_list<const char *> names)
  {
  for (const char * n : names)
    for (std::size_t k=0; k<columns.size(); k++) if (columns[k]==n) return int(k);
  return -1;
  }

  //! Whether every value of a column is zero.
  bool allZero(const std::vector<double> & column)
  {
  for (double v : column) if (v!=0.0) return false;
  return true;
  }
  } // namespace

  // ---------------------------------------------------------------------------------------------------- roles

  TableColumnRoles TableColumnRoles::guess(const Table2D & table)
  {
  TableColumnRoles roles;
  unsigned int n = table.nColumns();
  if (n<2) return roles;
  std::vector<std::string> names(n);
  bool named = table.hasColumnNames();
  for (unsigned int k=0; k<n; k++) names[k] = lower(table.columnName(k));
  if (named)
    {
    // several variables: x0, x1, ... with x0_err, ...; or one: x with x_err
    for (unsigned int i=0; ; i++)
      {
      std::string xi = "x" + std::to_string(i);
      int c = findColumn(names, { xi.c_str() });
      if (c<0) break;
      roles.x.push_back(c);
      std::string e1 = xi + "_err", e2 = "e" + xi, e3 = "d" + xi, e4 = xi + "_error";
      roles.xError.push_back(findColumn(names, { e1.c_str(), e2.c_str(), e3.c_str(), e4.c_str() }));
      }
    if (roles.x.empty())
      {
      int c = findColumn(names, { "x" });
      if (c>=0) { roles.x.push_back(c); roles.xError.push_back(findColumn(names, { "x_err", "ex", "dx", "xerr", "x_error" })); }
      }
    roles.value      = findColumn(names, { "y", "value" });
    roles.stat       = findColumn(names, { "stat", "ey", "dy", "y_err", "yerr", "err", "error", "stat_err" });
    roles.systPlus   = findColumn(names, { "syst_plus", "sys_plus", "syst", "sys" });
    roles.systMinus  = findColumn(names, { "syst_minus", "sys_minus" });
    roles.covariance = findColumn(names, { "cov_xy", "cov", "covariance" });
    if (roles.valid())
      {
      bool anyError = false;
      for (int e : roles.xError) if (e>=0) anyError = true;
      if (!anyError) roles.xError.clear();
      return roles;
      }
    roles = TableColumnRoles();
    }
  // by the number of columns
  switch (n)
    {
    case 2:  roles.x = {0}; roles.value = 1; break;
    case 3:  roles.x = {0}; roles.value = 1; roles.stat = 2; break;
    case 4:  roles.x = {0}; roles.xError = {1}; roles.value = 2; roles.stat = 3; break;
    case 5:  roles.x = {0}; roles.xError = {1}; roles.value = 2; roles.stat = 3; roles.systPlus = 4; break;
    default: roles.x = {0}; roles.xError = {1}; roles.value = 2; roles.stat = 3; roles.systPlus = 4; roles.systMinus = 5; break;
    }
  return roles;
  }

  // ------------------------------------------------------------------------------------------- to data points

  String TableConversion::Report::summary() const
  {
  std::string s = std::to_string(points) + " points";
  if (skipped>0) s += ", " + std::to_string(skipped) + " rows left out (x or value missing)";
  if (missingErrors>0) s += ", " + std::to_string(missingErrors) + " missing uncertainties taken as zero";
  if (negativeErrors>0) s += ", " + std::to_string(negativeErrors) + " negative uncertainties taken as positive";
  if (invalidCovariances>0) s += ", " + std::to_string(invalidCovariances) + " covariances not valid, taken as zero";
  return String(s.c_str());
  }

  std::unique_ptr<DataPointColumns> TableConversion::toDataPoints(const Table2D & table, const TableColumnRoles & roles,
                                                                  Report & report, const String & name)
  {
  report = Report();
  if (!roles.valid()) throw MathException("Data points need an x column and a value column",__FUNCTION__);
  if (!roles.xError.empty() && roles.xError.size()!=roles.x.size())
    throw MathException("One x uncertainty column (or -1) is needed per x column",__FUNCTION__);
  if (roles.covariance>=0 && roles.x.size()!=1) throw MathException("A covariance of x and y needs one x column",__FUNCTION__);
  int nColumns = int(table.nColumns());
  auto check = [nColumns](int c) { if (c>=nColumns) throw MathException("The table has no column "+std::to_string(c),"TableConversion::toDataPoints()"); };
  for (int c : roles.x) { if (c<0) throw MathException("An x column is missing","TableConversion::toDataPoints()"); check(c); }
  for (int c : roles.xError) check(c);
  check(roles.value); check(roles.stat); check(roles.systPlus); check(roles.systMinus); check(roles.covariance);

  std::size_t rank = roles.x.size();
  std::vector< std::vector<double> > x(rank), xError(rank);
  std::vector<double> value, stat, systPlus, systMinus, covariance;
  auto error = [&](unsigned int row, int column)
    {
    if (column<0) return 0.0;
    double e = table(row, unsigned(column));
    if (std::isnan(e)) { report.missingErrors++; return 0.0; }
    if (!std::isfinite(e)) { report.missingErrors++; return 0.0; }
    if (e<0) { report.negativeErrors++; return -e; }
    return e;
    };
  for (unsigned int row=0; row<table.nRows(); row++)
    {
    bool ok = std::isfinite(table(row, unsigned(roles.value)));
    for (int c : roles.x) ok = ok && std::isfinite(table(row, unsigned(c)));
    if (!ok) { report.skipped++; continue; }
    for (std::size_t i=0; i<rank; i++)
      {
      x[i].push_back(table(row, unsigned(roles.x[i])));
      xError[i].push_back(error(row, roles.xError.empty() ? -1 : roles.xError[i]));
      }
    value.push_back(table(row, unsigned(roles.value)));
    stat.push_back(error(row, roles.stat));
    systPlus.push_back(error(row, roles.systPlus));
    systMinus.push_back(roles.systMinus>=0 ? error(row, roles.systMinus) : systPlus.back());
    if (roles.covariance>=0)
      {
      // the covariance as stored (single precision) must satisfy cov^2 <= var(x) var(y)
      double c = table(row, unsigned(roles.covariance));
      double ex = float(xError[0].back()), ey = float(stat.back());
      if (!std::isfinite(c) || double(float(c))*float(c) > ex*ex*ey*ey*(1.0+1.0e-6)) { if (!(std::isnan(c))) report.invalidCovariances++; c = 0.0; }
      covariance.push_back(c);
      }
    }
  std::unique_ptr<DataPointColumns> points(new DataPointColumns(unsigned(rank)));
  points->setColumns(x, xError, value, stat, systPlus, systMinus);
  if (!covariance.empty()) points->setCovarianceColumn(covariance);
  points->setName(name.isEmpty() ? table.name() : name);
  points->setTitle(points->name());
  report.points = points->size();
  return points;
  }

  // ---------------------------------------------------------------------------------------------- to tables

  Table2D TableConversion::toTable(const DataPoints & points, bool allColumns, const String & name)
  {
  unsigned int n = points.size(), rank = points.rank();
  std::vector< std::vector<double> > x(rank, std::vector<double>(n)), xError(rank, std::vector<double>(n));
  std::vector<double> value(n), stat(n), systPlus(n), systMinus(n), covariance(n, 0.0);
  for (unsigned int k=0; k<n; k++)
    {
    std::vector<double> c = points.coordinatesAt(k), e = points.coordinateErrorsAt(k);
    for (unsigned int i=0; i<rank; i++) { x[i][k] = i<c.size() ? c[i] : 0.0; xError[i][k] = i<e.size() ? e[i] : 0.0; }
    value[k] = points.valueAt(k);
    stat[k]  = points.statErrorAt(k);
    systPlus[k]  = points.sysErrorPlusAt(k);
    systMinus[k] = points.sysErrorMinusAt(k);
    if (rank==1)
      {
      if (const DataPointColumns * columns = dynamic_cast<const DataPointColumns *>(&points)) covariance[k] = columns->covarianceAt(k);
      else if (!dynamic_cast<const Histogram *>(&points)) covariance[k] = points.point(k).xyCovariance();
      }
    }
  Table2D table(name.isEmpty() ? points.name() : name);
  if (n==0) return table;   // no point: an empty table
  auto add = [&](const std::vector<double> & column, const std::string & columnName, bool keep)
    {
    if (keep) table.addColumn(column, String(columnName.c_str()));
    };
  for (unsigned int i=0; i<rank; i++)
    {
    std::string xn = rank==1 ? std::string("x") : "x" + std::to_string(i);
    add(x[i], xn, true);
    add(xError[i], xn + "_err", allColumns || !allZero(xError[i]));
    }
  add(value, rank==1 ? "y" : "value", true);
  add(stat, "stat", allColumns || !allZero(stat));
  bool asymmetric = systPlus!=systMinus;
  add(systPlus, "syst_plus", allColumns || !allZero(systPlus) || asymmetric);
  add(systMinus, "syst_minus", allColumns || asymmetric);
  add(covariance, "cov_xy", rank==1 && (allColumns || !allZero(covariance)));
  return table;
  }

  Table2D TableConversion::toTable(const Histogram & histogram, bool emptyBins, const String & name)
  {
  unsigned int rank = histogram.rank();
  std::vector<std::string> axes;
  for (unsigned int a=0; a<rank; a++) axes.push_back(rank==1 ? std::string("x") : "x" + std::to_string(a));
  std::vector<String> names;
  for (unsigned int a=0; a<rank; a++)
    { names.push_back(String(axes[a].c_str())); names.push_back(String((axes[a]+"_low").c_str())); names.push_back(String((axes[a]+"_high").c_str())); }
  names.push_back("value");
  names.push_back("stat");
  names.push_back("entries");
  Table2D table(name.isEmpty() ? histogram.name() : name);
  unsigned int total = histogram.size();
  std::vector<unsigned int> index(rank);
  for (unsigned int k=0; k<total; k++)
    {
    long entries = histogram.binEntriesAtGlobal(k);
    double v = histogram.binValueAtGlobal(k), e = histogram.binUncertaintyAtGlobal(k);
    if (!emptyBins && entries==0 && v==0.0 && e==0.0) continue;
    std::vector<double> row;
    unsigned int rest = k;   // global index = i0 + n0 (i1 + n1 (i2 + ...)): axis 0 varies fastest
    for (unsigned int a=0; a<rank; a++)
      {
      unsigned int nb = histogram.nBins(a), i = rest % nb;
      rest /= nb;
      row.push_back(histogram.binCenter(i, a));
      row.push_back(histogram.binLowEdge(i, a));
      row.push_back(histogram.binHighEdge(i, a));
      }
    row.push_back(v);
    row.push_back(e);
    row.push_back(double(entries));
    table.addRow(row);
    }
  if (table.nRows()>0) table.setColumnNames(names);   // no bin kept: an empty table
  return table;
  }

  } // namespace MATH
} // namespace Datum
