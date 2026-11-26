/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file DataPointsFile.hpp
//! @brief DataPointsFile: data points and histograms read from, and written to, text, CSV and Excel tables.
#ifndef DATUM__DataPointsFile
#define DATUM__DataPointsFile
#include <memory>
#include "String.hpp"
#include "DataPoints.hpp"
#include "DataPointColumns.hpp"
#include "Histogram.hpp"
#include "TableConversion.hpp"
#include "TableReader.hpp"

namespace Datum
{

//!
//! @brief Data points read from tables of numbers (text, CSV: TableReader), and data points or histograms written as
//! tables (text, CSV, binary tables, Excel: TableWriter). The columns are given roles (MATH::TableColumnRoles): x, its
//! uncertainty, y, its statistical and systematic uncertainties, the covariance of x and y; by default they are
//! found from the names of the columns, or from their number (MATH::TableColumnRoles::guess()).
//!
//! @code
//!   Datum::TableReader::Report read;
//!   Datum::MATH::TableConversion::Report converted;
//!   auto points = Datum::DataPointsFile::read("spectrum.csv", read, converted);   // x, x_err, y, stat, ...
//!   Datum::DataPointsFile::write("copy.txt", *points);
//! @endcode
//!
class DataPointsFile
{
public:
  //!
  //! @brief Data points from a table file. @param fileName file (any format of TableReader)
  //! @param readReport what was read @param conversionReport what was converted
  //! @param roles roles of the columns (not valid(): found by MATH::TableColumnRoles::guess())
  //! @return points named after the file
  //! @throws FileException if the file cannot be read; MathException if the roles do not fit the table
  //!
  static std::unique_ptr<MATH::DataPointColumns> read(const String & fileName, TableReader::Report & readReport,
                                                      MATH::TableConversion::Report & conversionReport,
                                                      const MATH::TableColumnRoles & roles=MATH::TableColumnRoles());
  //!
  //! @brief Write data points as a table (MATH::TableConversion::toTable()), in the format of the extension of the file.
  //! @param fileName file (.txt, .dat, .csv, .tsv, .ssv, .dtb, .xlsx) @param points data points (a histogram is
  //! written bin by bin, with the edges of its bins and its entries) @param allColumns write every column
  //! @throws FileException if the file cannot be written
  //!
  static void write(const String & fileName, const MATH::DataPoints & points, bool allColumns=false);
};

} // namespace Datum

#endif // DATUM__DataPointsFile
