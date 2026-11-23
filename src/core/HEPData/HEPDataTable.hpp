/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
#ifndef DATUM_HEPDataTable
#define DATUM_HEPDataTable
#include <limits>
#include <vector>
#include "String.hpp"

namespace Datum
{

//!
//! @brief One uncertainty of a value of a HEPData table: symmetric ("symerror") or asymmetric ("asymerror").
//!
//! The numbers are kept as written in the file: for an asymmetric error, plus and minus are signed (usually plus>0 and
//! minus<0), and either may be missing (a one-sided error, written ''); a percentage ("2%") is relative to the value.
//! upper() and lower() give the absolute sizes of the upward and downward uncertainties, as HEPData reads them:
//! upward = max(0, plus, minus), downward = -min(0, plus, minus); a symmetric error e gives |e| on both sides.
//!
struct HEPDataError
{
  enum Kind { kSymmetric, kAsymmetric };

  Kind   kind         = kSymmetric;  //!< symmetric or asymmetric
  String label;                      //!< label, e.g. "stat", "sys", "sys,lumi" (may be empty)
  double plus         = 0.0;         //!< symmetric error, or upper shift of an asymmetric one (signed, as written)
  double minus        = 0.0;         //!< lower shift of an asymmetric error (signed, as written)
  bool   plusMissing  = false;       //!< asymmetric error without upper side ('')
  bool   minusMissing = false;       //!< asymmetric error without lower side ('')
  bool   percent      = false;       //!< the numbers are percentages of the value

  //! @brief A symmetric error. @param error size @param label label @param percent percentage of the value @return error
  static HEPDataError symmetric(double error, const String & label="", bool percent=false);
  //! @brief An asymmetric error. @param plus upper shift (>= 0 usually) @param minus lower shift (<= 0 usually)
  //! @param label label @param percent percentages of the value @return error
  static HEPDataError asymmetric(double plus, double minus, const String & label="", bool percent=false);

  //! @brief Absolute size of the upward uncertainty. @param value the value the error belongs to (for percentages)
  //! @return size (>= 0)
  double upper(double value) const;
  //! @brief Absolute size of the downward uncertainty. @param value the value the error belongs to @return size (>= 0)
  double lower(double value) const;
  //! @brief Whether the label marks a statistical error: "stat" (any case), alone or before a comma. @return true if so
  bool isStatistical() const;
};

//!
//! @brief One row of a variable of a HEPData table.
//!
//! An independent variable has a bin (low, high), a value, or both; a dependent variable has a value with its errors.
//! A value that is not a number (e.g. '-' for an empty bin) is kept as text (isNumeric() false).
//!
struct HEPDataValue
{
  bool   hasValue = false;   //!< a value is given
  bool   numeric  = true;    //!< the value is a number (otherwise see text)
  double value    = 0.0;     //!< value, if numeric
  String text;               //!< value as written, when it is not a number (e.g. "-")
  bool   hasLow   = false;   //!< a lower edge is given
  bool   hasHigh  = false;   //!< an upper edge is given
  double low      = 0.0;     //!< lower edge of the bin (may be -inf)
  double high     = 0.0;     //!< upper edge of the bin (may be +inf)
  std::vector<HEPDataError> errors;   //!< uncertainties (dependent variables only)

  //! @brief A numeric value. @param v value @return value
  static HEPDataValue number(double v);
  //! @brief A bin [low, high], without value. @param low lower edge @param high upper edge @return value
  static HEPDataValue bin(double low, double high);
  //! @brief A non-numeric value, e.g. "-". @param text text @return value
  static HEPDataValue textValue(const String & text);

  //! @brief Whether this row is a bin (both edges given). @return true if so
  bool isBin() const { return hasLow && hasHigh; }
  //! @brief Whether the value is a number. @return true if a numeric value is given
  bool isNumeric() const { return hasValue && numeric; }
  //! @brief Position of the row: the value if given, else the middle of the bin (an infinite edge: the finite one).
  //! @return position, NaN if there is neither
  double center() const;
};

//! @brief A qualifier of a dependent variable (e.g. SQRT(S) = 7000 GEV).
struct HEPDataQualifier
{
  String name;    //!< name
  String value;   //!< value, as written
  String units;   //!< units (may be empty)
};

//! @brief A variable (column) of a HEPData table.
struct HEPDataVariable
{
  String name;                                 //!< name (header)
  String units;                                //!< units (header; may be empty)
  std::vector<HEPDataQualifier> qualifiers;    //!< qualifiers (dependent variables only)
  std::vector<HEPDataValue>     values;        //!< one per row

  //! @brief Number of rows. @return size
  unsigned int size() const { return (unsigned int) values.size(); }
  //! @brief Whether every row is a bin. @return true if so (and there is at least one row)
  bool isBinned() const;
  //! @brief The qualifier with a name. @param name name @return qualifier, or nullptr if there is none
  const HEPDataQualifier * qualifier(const String & name) const;
  //! @brief Labels of the errors, in the order they first appear. @return labels (an empty label as "")
  std::vector<String> errorLabels() const;
};

//! @brief A keyword of a table (name and values, e.g. cmenergies: [7000]).
struct HEPDataKeyword
{
  String name;                  //!< name, e.g. reactions, observables, cmenergies, phrases
  std::vector<String> values;   //!< values
};

//! @brief An additional resource (file or link) of a submission or of a table.
struct HEPDataResource
{
  String location;             //!< file name or URL
  String description;          //!< description
  String type;                 //!< type (optional, e.g. HistFactory)
  String licenseName;          //!< licence (optional)
  String licenseUrl;           //!< licence URL (optional)
  String licenseDescription;   //!< licence description (optional)
};

//!
//! @brief A table of a HEPData submission: its metadata (from submission.yaml) and its data (the data file).
//!
//! Reading and writing go through yaml-cpp (not exposed by this header). The data file holds independent_variables
//! and dependent_variables; a file written by Datum in lossless mode may also hold a top-level "datum" block, kept
//! here as YAML text (datumBlock) and read by the converters of Datum objects.
//!
class HEPDataTable
{
public:

  String name;                                  //!< name (at most 64 characters for HEPData)
  String description;                           //!< description (caption)
  String location;                              //!< location in the publication (optional)
  String dataFile;                              //!< name of the data file
  std::vector<HEPDataKeyword>  keywords;        //!< keywords
  std::vector<HEPDataResource> resources;       //!< additional resources of the table
  String licenseName;                           //!< data licence (optional)
  String licenseUrl;                            //!< data licence URL
  String licenseDescription;                    //!< data licence description
  std::vector<HEPDataVariable> independent;     //!< independent variables (x)
  std::vector<HEPDataVariable> dependent;       //!< dependent variables (y)
  String datumBlock;                            //!< the "datum" block of the data file, as YAML text (empty if none)

  //! @brief Number of rows (that of the first variable; 0 if none). @return rows
  unsigned int nRows() const;
  //! @brief The values of a keyword. @param name keyword name @return values (empty if there is no such keyword)
  std::vector<String> keyword(const String & name) const;
  //! @brief Set a keyword (replacing one with the same name). @param name name @param values values
  void setKeyword(const String & name, const std::vector<String> & values);

  //!
  //! @brief What would make this table invalid for HEPData: variables of different sizes, dependent variables with
  //! independent ones absent, bins whose errors are all zero, missing name, description or keywords, a name of more
  //! than 64 characters, an asymmetric error missing on both sides, an independent value with only one edge.
  //! @return one sentence per problem (empty: valid)
  //!
  std::vector<String> problems() const;

  //! @name Data file (YAML)
  //! @{

  //! @brief Read the variables (and the datum block) from YAML text. @param text YAML of a data file
  //! @throws FileException if the text is not a valid data file
  void readDataYaml(const String & text);
  //! @brief Read the variables (and the datum block) from a data file (YAML or JSON). @param fileName file name
  //! @throws FileException if the file cannot be read or is not a valid data file
  void readDataFile(const String & fileName);
  //!
  //! @brief The data file as YAML text: bins and errors in flow style, as HEPData writes them.
  //! @param digits significant digits of the numbers (0: the shortest form that reads back exactly)
  //! @param withDatumBlock include the datum block (lossless mode) if there is one
  //! @return YAML text
  //!
  String dataYaml(unsigned int digits=0, bool withDatumBlock=true) const;
  //! @brief Write the data file. @param fileName file name @param digits see dataYaml() @param withDatumBlock see dataYaml()
  //! @throws FileException if the file cannot be written
  void writeDataFile(const String & fileName, unsigned int digits=0, bool withDatumBlock=true) const;

  //! @}
};

} // namespace Datum

#endif
