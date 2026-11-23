/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
#ifndef DATUM_HEPDataConverter
#define DATUM_HEPDataConverter
#include <memory>
#include <vector>
#include "String.hpp"
#include "HEPDataSubmission.hpp"
#include "DataPoints.hpp"

namespace Datum
{

//! @brief Options of the import.
struct HEPDataImportOptions
{
  std::vector<String> commonLabels = {String("norm"), String("sys,common")};   //!< labels of the common systematic uncertainty
  double binWidthTolerance = 1.0E-6;   //!< relative tolerance of equal bin widths (for histograms)
  double commonTolerance   = 0.05;     //!< relative variation of the common uncertainty that gives a warning
  bool   histograms        = true;     //!< make histograms from bins (false: always points)
  bool   keepPositions     = false;    //!< make points, not histograms, when the rows give positions away from the bin middles
};

//! @brief Options of the export.
struct HEPDataExportOptions
{
  bool   lossless      = true;          //!< write the datum block (false for a submission to HEPData)
  bool   keepEmptyBins = false;         //!< write empty bins of histograms (value '-'); always in lossless mode
  String statLabel     = "stat";        //!< label of the statistical uncertainty
  String systLabel     = "sys";         //!< label of the systematic uncertainty
  String commonLabel   = "norm";        //!< label of the common systematic uncertainty
  std::vector<String> xNames;           //!< names of the independent variables (default: axis titles, or x, y, ...)
  std::vector<String> yNames;           //!< names of the dependent variables (default: the titles of the objects)
  //! qualifiers of each dependent variable (one list per object, or a single list for all)
  std::vector< std::vector<HEPDataQualifier> > qualifiers;
};

//! @brief An object made from a dependent variable of a table, with what it came from.
struct HEPDataImported
{
  std::shared_ptr<MATH::DataPoints> object;      //!< the object (Histogram, HistogramSyst or DataPointsVector)
  String table;                                  //!< name of the table
  unsigned int column = 0;                       //!< index of the dependent variable
  std::vector<HEPDataQualifier> qualifiers;      //!< qualifiers of the dependent variable
};

//!
//! @brief Conversion between HEPData tables and Datum objects: histograms (Histogram, HistogramSyst, Profile as its
//! values) and sets of data points (DataPointsVector, DataPointColumns, any rank).
//!
//! <b>Import</b> (toObjects()): one object per dependent variable of a table.
//!  - Independent variables that are bins not overlapping (up to three; gaps between rows are empty bins, every
//!    combination of bins at most once): a Histogram, or a HistogramSyst if there are systematic errors, with regular
//!    axes when the bins have equal widths and variable bins otherwise. Positions given within the bins (values away
//!    from the middles) are not kept (a warning), unless ImportOptions::keepPositions asks for points instead.
//!  - Otherwise (points, non-binned or overlapping variables, more variables): a DataPointsVector of
//!    DataPoint2D, DataPoint3D or DataPointND. The coordinate of a row is its value if given, else the middle of its
//!    bin; its uncertainty half the width of the bin (0 for an infinite edge or a point).
//!  - No independent variable: a DataPointsVector with the row index as x.
//!  - Errors: those whose label is statistical (HEPDataError::isStatistical(); or the only error of a variable if it
//!    is unlabelled) are the statistical uncertainty, added in quadrature, the larger side if asymmetric (a warning);
//!    those whose label is one of ImportOptions::commonLabels (by default "norm" and "sys,common") give the common
//!    systematic uncertainty, relative to the value, averaged over the rows (each side; a warning if it varies by
//!    more than ImportOptions::commonTolerance); all the others are added in quadrature, each side separately, into
//!    the systematic uncertainty of the point or bin. Percentages are converted to absolute uncertainties.
//!  - Rows whose value is not a number (e.g. '-') are empty bins, or are skipped for points.
//!  - Names: the stem of the data file (or the table name), followed by _<k> for the k-th dependent variable when
//!    there are several; titles: "<name> (<units>)" of the dependent variable; axis titles of histograms likewise.
//!  - A table written by Datum in lossless mode has a datum block: the object is then rebuilt with its class, name,
//!    title, axes, numbers of entries per bin and precision.
//!
//! <b>Export</b> (toTable()): one dependent variable per object, the objects sharing the independent variables of the
//! first one (same binning or same coordinates).
//!  - Histograms: one independent variable per axis, with the edges of the bins (rank 2 and more flattened, axis 0
//!    fastest); empty bins are omitted (or written with ExportOptions::keepEmptyBins, always in lossless mode).
//!  - Points: the coordinates as values, with low and high edges if their uncertainty is not zero.
//!  - Errors: the statistical uncertainty (label ExportOptions::statLabel), the systematic one (systLabel; symerror,
//!    or asymerror when the sides differ), the common systematic uncertainty as a percentage (commonLabel).
//!  - Headers: from the titles, "<name> (<units>)" split into name and units.
//!  - Lossless mode (ExportOptions::lossless): the datum block of the table records what HEPData cannot hold.
//!
class HEPDataConverter
{
public:

  //! @brief Options of the import (see HEPDataImportOptions).
  using ImportOptions = HEPDataImportOptions;
  //! @brief Options of the export (see HEPDataExportOptions).
  using ExportOptions = HEPDataExportOptions;
  //! @brief An object made from a table (see HEPDataImported).
  using Imported = HEPDataImported;

  //! @brief Objects of a table (one per dependent variable). @param table table @param options options
  //! @param warnings if given, messages are appended (asymmetric statistical errors, varying common uncertainty, ...)
  //! @return objects @throws Exception if the table cannot be converted (e.g. variables of different sizes)
  static std::vector<Imported> toObjects(const HEPDataTable & table, const ImportOptions & options = ImportOptions(),
                                         std::vector<String> * warnings = nullptr);
  //! @brief Objects of all the tables of a submission. @param submission submission @param options options
  //! @param warnings if given, messages are appended @return objects
  static std::vector<Imported> toObjects(const HEPDataSubmission & submission, const ImportOptions & options = ImportOptions(),
                                         std::vector<String> * warnings = nullptr);

  //! @brief A table holding one object. @param object object @param options options @return table (no name,
  //! description or keywords: to be set) @throws Exception if the object cannot be converted
  static HEPDataTable toTable(const MATH::DataPoints & object, const ExportOptions & options = ExportOptions());
  //! @brief A table holding several objects with the same binning or coordinates, one dependent variable each.
  //! @param objects objects (at least one) @param options options @return table
  //! @throws Exception if the objects differ in binning or coordinates, or cannot be converted
  static HEPDataTable toTable(const std::vector<const MATH::DataPoints *> & objects, const ExportOptions & options = ExportOptions());
  //! @brief A table holding imported objects again, with their qualifiers. @param objects objects of one table
  //! @param options options (its qualifiers are replaced by those of the objects) @return table
  static HEPDataTable toTable(const std::vector<Imported> & objects, ExportOptions options = ExportOptions());

  //! @brief Split "name (units)" into name and units (the last parenthesis at the end). @param title title
  //! @param name name @param units units (empty if none)
  static void splitTitle(const String & title, String & name, String & units);
  //! @brief "name (units)", or name if units is empty. @param name name @param units units @return title
  static String joinTitle(const String & name, const String & units);
};

} // namespace Datum

#endif
