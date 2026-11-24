/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file HEPDataExporter.hpp
//! @brief HEPDataExporter: task writing histograms, points, fits and functions of Datum files as a HEPData submission.
#ifndef DATUM__HEPDataExporter
#define DATUM__HEPDataExporter
#include <memory>
#include <vector>
#include "Task.hpp"
#include "HEPDataPlan.hpp"

namespace Datum
{
class DatumFile;

//!
//! @brief Task writing objects of Datum files as a HEPData submission (submission.yaml and one data file per table),
//! the same as File > Export > HEPData YAML of DatumGui, in a workflow: run by bin/Datum as a job of its own, after the
//! jobs that make the files (its job writes no Datum file: JOB:OUTPUT_FILE NONE).
//!
//! Configuration (keys below the name of the task; strings NONE when not set):
//!  - HISTOGRAM:IMPORT:PATH, HISTOGRAM:IMPORT:FILE_NAME: the Datum files read (several names separated by spaces;
//!    PATH NONE: the histogram directory of the job).
//!  - HEPDATA:EXPORT:PATH: the directory written (created if needed);
//!    HEPDATA:EXPORT:ARCHIVE: an archive made of it (.tar.gz, .tgz or .zip; a name without directory is put next to
//!    the directory), NONE for none. In paths, $VARIABLES are expanded, and a relative path is in the histogram
//!    directory of the job ($DATUM_HISTOS_EXPORT_PATH, set by bin/Datum: where the jobs of the workflow write their
//!    files), or in the current directory outside bin/Datum.
//!  - MODE: LOSSLESS (the data files keep a datum block from which Datum reads the objects back exactly; hepdata.net
//!    ignores it) or STRICT (plain HEPData, for a submission to hepdata.net). DIGITS: significant digits (0: the
//!    shortest exact form). FITS: also the fits attached to the histograms and points, a table each.
//!  - The tables: either listed one by one, TABLE_1, TABLE_2, ... TABLE_N, N given by TABLE:N (0: until the first
//!    without PATH), each
//!    with PATH (path of the object in its file), FILE (the file holding it, when several are read; NONE: the first
//!    that has it), NAME (name of the table; NONE: that of the object), DESCRIPTION, LOCATION, OBSERVABLES, REACTIONS,
//!    CMENERGIES, PHRASES, QUALIFIERS, X_NAME, X_UNITS, Y_NAME, Y_UNITS, and for a function LOW, HIGH, POINTS; or, when
//!    none is listed (TABLE:N 0 and TABLE_1:PATH NONE), all the objects of the files whose path matches OBJECTS (patterns with * and ?, separated
//!    by spaces; default *) and none of EXCLUDE: histograms and points with values (fits with them), functions of one
//!    variable (over FUNCTION:LOW to FUNCTION:HIGH, FUNCTION:POINTS points).
//!  - TABLES:OBSERVABLES, REACTIONS, CMENERGIES, PHRASES (keywords, values separated by commas), QUALIFIERS
//!    ("NAME = value [units]; ..."), LOCATION, LICENSE, LICENSE_URL: for the tables that give none.
//!  - PUBLICATION:COLLABORATION, TITLE, INSPIRE, ARXIV, DOI, JOURNAL, CONTACT: comment lines at the top of
//!    submission.yaml (see HEPDataPublication); PUBLICATION:IN_COMMENT: the comment begins with them. COMMENT: the
//!    comment of the submission.
//!  - PREVIEW_ONLY: print the preview and write nothing. PREVIEW_ROWS: rows of each table shown in the preview.
//!    FAIL_ON_PROBLEMS: stop (an exception) if hepdata.net would reject the submission or an object cannot be written,
//!    instead of a warning.
//!
//! The guide, with the table of the keys and an example: docs/HEPData.md, section 9.
//!
//! execute() prints the preview (at report level INFO; the same tree as in DatumGui, as text), then writes the files.
//!
class HEPDataExporter
:
public Task
{
public:
  //! @brief Default constructor.
  HEPDataExporter();
  //! @brief Destructor.
  virtual ~HEPDataExporter() {}
  //! @brief Add the default configuration (see the class description).
  virtual void setDefaultConfiguration() override;
  //! @brief Read the files, plan the submission, print the preview, write it (see the class description).
  //! @throws Exception if a file or a listed object is missing, or (FAIL_ON_PROBLEMS) the submission has problems
  virtual void execute() override;
  //! @brief The plan made from the configuration and the files read (by execute(), or by programs and tests).
  //! @return plan (the objects belong to the files kept by the task)
  HEPDataExportPlan makePlan();
  //! @brief The submission written by the last execute() (empty if none). @return submission
  const HEPDataSubmission & written() const { return _written; }
  //! @brief The preview printed by the last execute(). @return tree
  const HEPDataPreviewNode & preview() const { return _preview; }

protected:
  //! @brief A string of the configuration, empty if NONE or missing. @param key key below the name of the task
  String text(const String & key) const;
  std::vector<std::shared_ptr<DatumFile>> _files;   //!< files read
  std::vector<String> _fileNames;                    //!< their names
  HEPDataSubmission _written;                        //!< submission written last
  HEPDataPreviewNode _preview;                       //!< preview printed last
};

} // namespace Datum
#endif
