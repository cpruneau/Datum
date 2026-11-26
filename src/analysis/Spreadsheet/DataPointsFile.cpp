/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file DataPointsFile.cpp
//! @brief Implementation of DataPointsFile.

#include "DataPointsFile.hpp"
#include "TableWriter.hpp"

namespace Datum
{

std::unique_ptr<MATH::DataPointColumns> DataPointsFile::read(const String & fileName, TableReader::Report & readReport,
                                                             MATH::TableConversion::Report & conversionReport,
                                                             const MATH::TableColumnRoles & roles)
{
  MATH::Table2D table;
  readReport = TableReader().read(fileName, table);
  MATH::TableColumnRoles used = roles.valid() ? roles : MATH::TableColumnRoles::guess(table);
  return MATH::TableConversion::toDataPoints(table, used, conversionReport);
}

void DataPointsFile::write(const String & fileName, const MATH::DataPoints & points, bool allColumns)
{
  const MATH::Histogram * histogram = dynamic_cast<const MATH::Histogram *>(&points);
  MATH::Table2D table = histogram ? MATH::TableConversion::toTable(*histogram)
                                  : MATH::TableConversion::toTable(points, allColumns);
  TableWriter().write(fileName, table);
}

} // namespace Datum
