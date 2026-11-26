/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TableConversion.hpp
//! @brief TableConversion: tables (Table2D) to data points (DataPointColumns), and data points or histograms to tables.
#ifndef DATUM_MATH_TableConversion
#define DATUM_MATH_TableConversion
#include <memory>
#include <vector>
#include "String.hpp"
#include "Table2D.hpp"
#include "DataPoints.hpp"
#include "DataPointColumns.hpp"
#include "Histogram.hpp"

namespace Datum
{
  namespace MATH
  {

  //!
  //! @brief The role of the columns of a table in data points: which columns are the independent variables x, their
  //! uncertainties, the value y, its statistical and systematic uncertainties, and the covariance of x and y.
  //!
  //! A role is a column index, or -1 when no column has it (a missing uncertainty is zero). There is one x column per
  //! independent variable (the rank of the points); xError, when given, has one entry per x column (-1 for an x
  //! without uncertainty).
  //!
  struct TableColumnRoles
  {
    std::vector<int> x;        //!< columns of the independent variables (at least one)
    std::vector<int> xError;   //!< columns of their uncertainties (empty, or one per x; -1: none)
    int value     = -1;        //!< column of the values (y)
    int stat      = -1;        //!< column of the statistical uncertainties of the values
    int systPlus  = -1;        //!< column of the (upper) systematic uncertainties
    int systMinus = -1;        //!< column of the lower systematic uncertainties (-1: equal to the upper ones)
    int covariance = -1;       //!< column of the covariances of x and y (rank 1 only)

    //! @brief Whether the roles name an x and a value. @return true if so
    bool valid() const { return !x.empty() && value>=0; }

    //!
    //! @brief Roles found from the names of the columns, or from their number.
    //!
    //! Names (any case): x, x0, x1, ... for the variables; x_err, ex, dx, xerr, x_error (x0_err, ...) for their
    //! uncertainties; y or value for the values; stat, ey, dy, y_err, yerr, err, error for their statistical
    //! uncertainties; syst, sys, syst_plus, sys_plus for the systematic uncertainties, and syst_minus, sys_minus for the
    //! lower ones; cov, cov_xy, covariance for the covariance of x and y. Without such names, by the number of columns:
    //! 2: x, y; 3: x, y, stat; 4: x, x_err, y, stat; 5: x, x_err, y, stat, syst; 6 or more: x, x_err, y, stat,
    //! syst_plus, syst_minus (the order written by TableConversion::toTable()).
    //! @param table table @return roles (not valid() if the table has fewer than 2 columns)
    //!
    static TableColumnRoles guess(const Table2D & table);
  };

  //!
  //! @brief Conversions between tables and data points: a table read from a text or CSV file becomes data points (a
  //! DataPointColumns) that can be plotted, fitted and stored in Datum files, and data points or a histogram become a
  //! table, written as text, CSV or Excel, or shown in a table pane.
  //!
  //! @code
  //!   Datum::MATH::Table2D table = ...;                                   // e.g. read by Datum::TableReader
  //!   Datum::MATH::TableColumnRoles roles = Datum::MATH::TableColumnRoles::guess(table);
  //!   Datum::MATH::TableConversion::Report report;
  //!   std::unique_ptr<Datum::MATH::DataPointColumns> points = Datum::MATH::TableConversion::toDataPoints(table, roles, report);
  //!   Datum::MATH::Table2D back = Datum::MATH::TableConversion::toTable(*points);
  //! @endcode
  //!
  class TableConversion
  {
  public:

    //! @brief What a conversion of a table to data points did.
    struct Report
    {
      unsigned int points  = 0;   //!< points made
      unsigned int skipped = 0;   //!< rows left out: an x or the value is missing (NaN) or infinite
      unsigned int missingErrors = 0;   //!< uncertainties missing (NaN), taken as zero
      unsigned int negativeErrors = 0;  //!< negative uncertainties, taken as their absolute value
      unsigned int invalidCovariances = 0;   //!< covariances not valid (|cov| > xError stat, or missing), taken as zero
      //! @brief One line describing the conversion. @return text
      String summary() const;
    };

    //!
    //! @brief Data points from the columns of a table.
    //! @param table table @param roles the roles of its columns @param report what was done
    //! @param name name of the points (empty: the name of the table)
    //! @return points (rank: the number of x columns), never null
    //! @throws MathException if the roles are not valid or name columns the table does not have
    //!
    static std::unique_ptr<DataPointColumns> toDataPoints(const Table2D & table, const TableColumnRoles & roles,
                                                          Report & report, const String & name="");

    //!
    //! @brief A table of data points: columns x, x_err, y, stat, syst_plus, syst_minus, cov_xy for points of rank 1
    //! (x0, x0_err, x1, x1_err, ..., value, stat, ... for higher ranks).
    //! @param points data points of any kind (DataPointColumns, DataPointsVector, a histogram, ...)
    //! @param allColumns write every column; otherwise the uncertainty columns that are all zero, the lower systematic
    //! when it equals the upper one, and the covariance when there is none, are left out
    //! @param name name of the table (empty: the name of the points)
    //! @return table with named columns
    //!
    static Table2D toTable(const DataPoints & points, bool allColumns=false, const String & name="");

    //!
    //! @brief A table of the bins of a histogram: for each axis its centre and edges (x, x_low, x_high; x0, x0_low, ...
    //! for several axes), then the value, its statistical uncertainty and the number of entries of the bin.
    //! @param histogram histogram @param emptyBins include the bins without entries, value or uncertainty
    //! @param name name of the table (empty: the name of the histogram)
    //! @return table with named columns, one row per bin (axis 0 varying fastest)
    //!
    static Table2D toTable(const Histogram & histogram, bool emptyBins=true, const String & name="");
  };

  } // namespace MATH
} // namespace Datum

#endif // DATUM_MATH_TableConversion
