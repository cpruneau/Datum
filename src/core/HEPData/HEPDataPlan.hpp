/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file HEPDataPlan.hpp
//! @brief What an import or an export of HEPData YAML files will do, shown before it is done (the preview of DatumGui).
//! The guide: docs/HEPData.md.
#ifndef DATUM_HEPDataPlan
#define DATUM_HEPDataPlan
#include <memory>
#include <vector>
#include "String.hpp"
#include "HEPDataSubmission.hpp"
#include "HEPDataConverter.hpp"

namespace Datum
{
class Folder;

//!
//! @brief A line of a preview, with the lines below it: a tree shown as a list of bullets.
//!
//! The kind tells how a line is shown: an item of the files read or written, an action (what OK will do), a warning
//! (something lost or doubtful), an error (what will not be done).
//!
struct HEPDataPreviewNode
{
  enum Kind { kItem, kAction, kWarning, kError };
  String text;                                  //!< the line
  Kind kind = kItem;                            //!< how it is shown
  bool open = false;                            //!< shown expanded at first
  std::vector<HEPDataPreviewNode> children;     //!< lines below

  //! @brief Add a line below this one. @param text text @param kind kind @param open shown expanded @return the line added
  HEPDataPreviewNode & add(const String & text, Kind kind=kItem, bool open=false);
  //! @brief Number of lines of the tree (this one included). @return count
  unsigned int count() const;
  //! @brief Number of lines of a kind in the tree. @param kind kind @return count
  unsigned int count(Kind kind) const;
  //! @brief The first line of the tree whose text contains a text. @param text text @return line, or nullptr
  const HEPDataPreviewNode * find(const String & text) const;
  //!
  //! @brief The tree as text: one line per node, "- " indented by two spaces per level; actions start with "=> ",
  //! warnings with "warning: ", errors with "error: ". @param depth level of this line @return text
  //!
  String toText(unsigned int depth=0) const;
};

//!
//! @brief Descriptions of HEPData tables and submissions, and of Datum objects, as preview trees.
//!
class HEPDataPreview
{
public:
  //!
  //! @brief A table: name and data file; below it the description, location, keywords, licence, resources, the
  //! variables (bins or values, range, errors, qualifiers) and the first rows. @param table table
  //! @param maxRows rows shown (0: none) @return line of the table
  //!
  static HEPDataPreviewNode table(const HEPDataTable & table, unsigned int maxRows=3);
  //! @brief The publication and the first document of a submission (comment, resources), added below a line.
  //! @param submission submission @param node line to add to
  static void header(const HEPDataSubmission & submission, HEPDataPreviewNode & node);
  //! @brief A whole submission: header and tables. @param submission submission @param title text of the line
  //! @param maxRows rows shown per table @return line
  static HEPDataPreviewNode submission(const HEPDataSubmission & submission, const String & title, unsigned int maxRows=3);
  //! @brief One line describing an object: class, name, rank, bins (regular or variable) or points, uncertainties,
  //! common systematic uncertainty, fits. @param object object @return text
  static String describe(const MATH::DataPoints & object);
  //! @brief One line describing a function: class, name, parameters. @param function function @return text
  static String describe(const MATH::ParameterizedFunction & function);
  //! @brief A number as short text (%g, 6 significant digits). @param x number @return text
  static String number(double x);
};

//!
//! @brief An import of HEPData YAML files: what is read, and what objects are made, before they are added.
//!
//! prepare() reads every submission found in a folder (see scan()) and converts its tables in memory, keeping the
//! objects; preview() describes the files and the objects; addTo() adds the objects to a folder (OK in DatumGui);
//! dropping the plan adds nothing (Cancel). A table written by Datum for a function or a fit (datum block with a
//! function) gives that function; the other tables give one histogram or set of points per dependent variable (see
//! HEPDataConverter::toObjects()).
//!
class HEPDataImportPlan
{
public:
  //! @brief An object to be made.
  struct Object
  {
    String name;                                           //!< name of the object (unique in its source)
    String table;                                          //!< table it comes from
    std::shared_ptr<MATH::DataPoints> points;              //!< histogram or points (or null)
    std::shared_ptr<MATH::ParameterizedFunction> function; //!< function (or null)
  };
  //! @brief Something read: a submission (a directory, a submission.yaml, an archive) or loose data files.
  struct Source
  {
    String path;                    //!< what is read (for loose data files: the folder)
    String kind;                    //!< "submission", "archive", "submission.yaml" or "data files"
    String folder;                  //!< folder of the objects in the data file made (from the name of the source)
    std::vector<String> files;      //!< the loose data files read (kind "data files")
    HEPDataSubmission submission;   //!< tables read
    std::vector<Object> objects;    //!< objects made
    std::vector<String> warnings;   //!< warnings of the conversion
    std::vector<String> errors;     //!< what could not be read or converted
  };

  //!
  //! @brief What can be read in a folder: the folder itself if it holds submission.yaml; otherwise its sub-folders
  //! that hold one, its archives (.tar.gz, .tgz, .tar, .zip), and its other .yaml, .yml and .json files taken as
  //! loose data files (one table each, gathered in one source). A path that is a file is read alone.
  //! @param path folder (or file) @return sources, not read yet (path, kind, folder, files)
  //!
  static std::vector<Source> scan(const String & path);
  //! @brief Read and convert everything found in a folder (or a file). @param path folder or file @param options
  //! options of the conversion @return number of objects made
  unsigned int prepare(const String & path, const HEPDataConverter::ImportOptions & options = HEPDataConverter::ImportOptions());
  //! @brief Folder (or file) prepared. @return path
  const String & path() const { return _path; }
  //! @brief Sources read. @return sources
  const std::vector<Source> & sources() const { return _sources; }
  //! @brief Number of objects to be made. @return count
  unsigned int nObjects() const;
  //! @brief Number of errors. @return count
  unsigned int nErrors() const;
  //! @brief Name proposed for the data file made: that of the folder (or file). @return name
  String name() const;
  //!
  //! @brief The preview: one line per source with its publication, comment and tables, each table with the objects
  //! it gives (actions), the warnings and errors; a first line summing up what OK does. @param maxRows rows shown per
  //! table @param name name of the data file made (empty: name()) @return tree
  //!
  HEPDataPreviewNode preview(unsigned int maxRows=3, const String & name=String()) const;
  //!
  //! @brief Add the objects to a folder: in a sub-folder per source when there are several, directly otherwise;
  //! names made unique. The objects are copies: the plan can be added again. @param folder folder @return number of
  //! objects added
  //!
  unsigned int addTo(Folder & folder) const;

protected:
  String _path;                     //!< folder (or file) prepared
  std::vector<Source> _sources;     //!< sources read
};

//!
//! @brief An export to HEPData YAML files: the metadata, the objects chosen, and the submission planned.
//!
//! The objects are not owned (they belong to the open files of the application); keep them alive while the plan is
//! used, e.g. with Item::owner. build() makes the submission in memory, preview() shows it with the files to be
//! written and what HEPData would reject, write() writes it (OK in DatumGui).
//!
class HEPDataExportPlan
{
public:
  //! @brief Metadata of a table: keywords, qualifiers, licence. An empty field takes the value of the defaults.
  struct TableInfo
  {
    String description;   //!< description (caption); default: the title of the object
    String location;      //!< location in the paper, e.g. "Figure 3a"
    String observables;   //!< keyword observables, values separated by commas (e.g. "DSIG/DPT")
    String reactions;     //!< keyword reactions (e.g. "P P --> PI0 X")
    String cmenergies;    //!< keyword cmenergies (e.g. "13000")
    String phrases;       //!< keyword phrases (e.g. "Inclusive, Transverse Momentum")
    String qualifiers;    //!< qualifiers of the values, "NAME = value [units]" separated by ';' (e.g. "SQRT(S) = 13 [TEV]")
    String licenseName;   //!< data licence (e.g. CC0)
    String licenseUrl;    //!< its URL
  };
  //! @brief An object to export: a histogram or set of points (with its fits if wanted), or a function.
  struct Item
  {
    String source;                                        //!< file it comes from (shown)
    String path;                                          //!< path in that file (shown)
    const MATH::DataPoints * points = nullptr;            //!< histogram or points (or null)
    const MATH::ParameterizedFunction * function = nullptr;   //!< function (or null)
    std::shared_ptr<const void> owner;                    //!< keeps the objects alive (optional)
    bool   include = true;                                //!< exported
    bool   fits = true;                                   //!< also the fits of the object, a table each
    String tableName;                                     //!< name of the table (default: name of the object)
    String xName, xUnits, yName, yUnits;                  //!< headers (default: from the titles)
    double low = 0.0, high = 1.0;                         //!< range of a function
    unsigned int nPoints = 100;                           //!< points of a function
    TableInfo info;                                       //!< metadata (empty fields: the defaults)
  };

  HEPDataPublication publication;     //!< publication (comment lines of submission.yaml)
  String comment;                     //!< comment of the submission
  bool   publicationInComment = true; //!< begin the comment with publication.summary()
  TableInfo defaults;                 //!< metadata of the tables whose fields are empty
  std::vector<HEPDataResource> resources;   //!< additional resources of the submission
  String directory;                   //!< directory written (created if needed)
  String archive;                     //!< archive made of it (.tar.gz, .tgz or .zip; empty: none)
  bool   lossless = true;             //!< write the datum blocks (false for a submission to HEPData)
  unsigned int digits = 0;            //!< significant digits (0: shortest exact form)
  std::vector<Item> items;            //!< objects

  //!
  //! @brief The objects of a folder that can be exported: histograms and sets of points (with fits) of one or more
  //! variables (not those whose values and uncertainties are all zero), functions of one variable. @param folder folder @param source name of the file shown
  //! @param owner keeps the folder alive (optional) @return items, all included, each table named after the entry of its
  //! object in the folder
  //!
  static std::vector<Item> itemsOf(const Folder & folder, const String & source, std::shared_ptr<const void> owner=nullptr);
  //! @brief Parse qualifiers "NAME = value [units]; ...". @param text text @return qualifiers (malformed parts skipped)
  static std::vector<HEPDataQualifier> parseQualifiers(const String & text);
  //! @brief Number of items included. @return count
  unsigned int nIncluded() const;
  //!
  //! @brief The submission: one table per item included (and per fit), with names made unique, metadata, keywords.
  //! @param errors if given, the items that could not be converted are appended (they are left out)
  //! @param origins if given, one line per table: the item it comes from
  //! @return submission
  //!
  HEPDataSubmission build(std::vector<String> * errors=nullptr, std::vector<String> * origins=nullptr) const;
  //! @brief The files that write() writes (directory first, then the files in it, then the archive). @return paths
  std::vector<String> files() const;
  //!
  //! @brief The preview: where the files go (warning if a submission is replaced), the header of submission.yaml,
  //! the tables, what HEPData would reject (warnings), the items that cannot be converted (errors). @param maxRows
  //! rows shown per table @return tree
  //!
  HEPDataPreviewNode preview(unsigned int maxRows=3) const;
  //! @brief Write the submission (and the archive). @return the submission written
  //! @throws Exception if there is no directory or nothing to write; FileException if a file cannot be written
  HEPDataSubmission write() const;
};

} // namespace Datum
#endif
