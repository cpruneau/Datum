/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
#ifndef DATUM_HEPDataSubmission
#define DATUM_HEPDataSubmission
#include <vector>
#include "String.hpp"
#include "HEPDataTable.hpp"

namespace Datum
{

//!
//! @brief The publication and the people behind a submission.
//!
//! HEPData takes the paper (title, authors, journal, DOI) and the collaboration from the INSPIRE record attached to
//! the submission: submission.yaml has no field for them. Datum keeps them as comment lines at the top of
//! submission.yaml ("# Collaboration: ALICE", "# INSPIRE: 1234567", ...), which HEPData ignores and Datum reads back;
//! summary() gives them as text, to put in the comment of the submission if wanted. Other comment lines at the top of
//! the file are kept in notes.
//!
struct HEPDataPublication
{
  String collaboration;          //!< collaboration (e.g. ALICE)
  String title;                  //!< title of the paper
  String inspire;                //!< INSPIRE record number (e.g. 1234567)
  String arXiv;                  //!< arXiv identifier (e.g. 2501.01234)
  String doi;                    //!< DOI of the paper
  String journal;                //!< journal reference
  String contact;                //!< contact (name, e-mail)
  std::vector<String> notes;     //!< other comment lines at the top of submission.yaml (without '#')

  //! @brief Whether nothing is set. @return true if so
  bool isEmpty() const;
  //! @brief The comment lines written at the top of submission.yaml (with '#', one per line). @return text
  String headerLines() const;
  //! @brief Read the comment lines at the top of a text (lines starting with '#'). @param text text of submission.yaml
  //! @return publication
  static HEPDataPublication fromHeader(const String & text);
  //! @brief The publication as one sentence, e.g. "ALICE Collaboration, Title, arXiv:2501.01234 (INSPIRE 1234567)."
  //! @return text (empty if nothing is set)
  String summary() const;
};

//!
//! @brief A HEPData submission: submission.yaml (comment, resources, one document per table) and the data files.
//! The guide to HEPData in Datum: docs/HEPData.md.
//!
//! read() accepts what hepdata.net gives and what authors prepare:
//!  - a directory holding submission.yaml and the data files;
//!  - a submission.yaml file (the data files next to it), including the single-YAML-file format, where the variables
//!    are in the documents of submission.yaml instead of data files;
//!  - a single data file (one table, named after the file, without metadata);
//!  - an archive (.tar.gz, .tgz, .tar, .zip) of one of the above, extracted with the tar command (unzip for .zip if
//!    tar cannot) into a temporary directory.
//! write() writes submission.yaml and the data files in a directory, and optionally an archive of them (.tar.gz or
//! .zip, made with tar or zip).
//!
//! Example:
//! @code
//!   Datum::HEPDataSubmission s = Datum::HEPDataSubmission::read("HEPData-ins1512299-v1-yaml.tar.gz");
//!   const Datum::HEPDataTable * t = s.table("Table 1");
//!   double y = t->dependent[0].values[3].value;
//! @endcode
//!
class HEPDataSubmission
{
public:

  String comment;                          //!< comment of the whole submission
  HEPDataPublication publication;          //!< publication and collaboration (comment lines of submission.yaml)
  std::vector<HEPDataResource> resources;  //!< additional resources of the whole submission
  std::vector<HEPDataTable>    tables;     //!< tables, in order

  //! @brief Read a submission (see the class description). @param path directory, submission.yaml, data file or archive
  //! @return submission @throws FileException if it cannot be read or is invalid
  static HEPDataSubmission read(const String & path);
  //! @brief Read a submission from the text of submission.yaml; data files are read from a directory.
  //! @param text YAML text @param directory directory of the data files ("" for the single-YAML-file format only)
  //! @return submission @throws FileException if the text or a data file is invalid
  static HEPDataSubmission readYaml(const String & text, const String & directory);

  //! @brief The table with a name. @param name name @return table, or nullptr if there is none
  const HEPDataTable * table(const String & name) const;
  //! @brief The table with a name. @param name name @return table, or nullptr if there is none
  HEPDataTable * table(const String & name);
  //! @brief Add a table; its data file is named after its position if it has none ("data<k>.yaml"). @param t table
  //! @return the table added
  HEPDataTable & addTable(const HEPDataTable & t);

  //! @brief What would make this submission invalid for HEPData: the problems of its tables (see
  //! HEPDataTable::problems()), prefixed with the table name, and tables with the same name or data file.
  //! @return one sentence per problem (empty: valid)
  std::vector<String> problems() const;

  //!
  //! @brief The text of submission.yaml: the comment lines of the publication (if any), the first document (comment, resources) if either is set, then one document
  //! per table (name, description, location, keywords, data_file, licence, resources). @return YAML text
  //!
  String submissionYaml() const;
  //!
  //! @brief Write submission.yaml and the data files in a directory (created if needed), and an archive if asked.
  //! @param directory directory @param archive archive to make of the directory (.tar.gz, .tgz or .zip), or "" for none
  //! @param digits significant digits of the numbers (0: shortest exact form) @param withDatumBlock write the datum
  //! blocks of the tables (lossless mode); false for a submission to HEPData
  //! @throws FileException if a file cannot be written or the archive cannot be made
  //!
  void write(const String & directory, const String & archive="", unsigned int digits=0, bool withDatumBlock=true) const;
};

} // namespace Datum

#endif
