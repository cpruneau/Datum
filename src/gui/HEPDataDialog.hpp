/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file HEPDataDialog.hpp
//! @brief HEPDataDialog: the HEPData import and export windows of DatumGui, with their preview.
#ifndef DATUM__GUI__HEPDataDialog
#define DATUM__GUI__HEPDataDialog
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "String.hpp"
#include "HEPDataPlan.hpp"

namespace Datum
{
  class Folder;

  namespace GUI
  {
  class FileDialog;

  //!
  //! @brief The HEPData windows of DatumGui: import HEPData YAML files into a new data file, or export objects of
  //! data files to a HEPData submission; in both cases a preview shows what will be done, which OK does and Cancel
  //! does not.
  //!
  //! <b>Import</b> (File > Import > HEPData YAML): the folder (or archive, or file) to read - a folder holding
  //! submission.yaml, or a folder of such folders, archives and YAML data files (see HEPDataImportPlan::scan()) -,
  //! the options of the conversion, the name of the data file made. Preview reads the files and converts the tables
  //! in memory, and shows the submissions, their tables and the objects each table gives; OK makes a new data file
  //! in the browser holding those objects (onImport).
  //!
  //! <b>Export</b> (File > Export > HEPData YAML): the data files whose objects are offered (those open in the
  //! browsers), the objects exported (histograms, points, with their fits; functions of one variable, over a range)
  //! and the table of each (name, description, location, keywords, qualifiers, headers); the submission: directory,
  //! archive, lossless or strict mode; the publication and collaboration, the comment; the keywords, qualifiers and
  //! licence of the tables that give none. Preview shows the files to be written and the planned submission, with
  //! what hepdata.net would reject; OK writes it.
  //!
  //! The preview is a window of its own, resizable and scrollable: a tree of bullets, the actions (what OK does) in
  //! green, warnings in yellow, errors in red; Expand All, Collapse All, Copy (the tree as text). A change made in the
  //! dialog after Preview makes the preview stale: OK waits for Preview again.
  //!
  //! Help (a button of the windows, or Help > HEPData Import and Export) opens a window summarizing how HEPData files
  //! become Datum objects and back (helpText(); the whole guide is docs/HEPData.md).
  //!
  class HEPDataDialog
  {
  public:
  //! @brief A data file whose objects can be exported.
  struct ExportFile
  {
    String label;                           //!< name shown
    std::shared_ptr<const Folder> folder;   //!< its folders and objects (kept alive while the dialog uses them)
    bool selected = false;                  //!< its objects offered
  };
  //! @brief OK of an import: make the data file. @param name name of the data file @param plan objects to add
  //! @param message set to what to tell (success or failure) @return false on failure
  using ImportCallback = std::function<bool(const String & name, const HEPDataImportPlan & plan, String & message)>;

  enum Mode { kImport, kExport };

  //! @brief Constructor (closed).
  HEPDataDialog();

  static const char * windowName() { return "HEPData###HEPDataDialog"; }          //!< Name of the window
  static const char * previewName() { return "HEPData Preview###HEPDataPreview"; } //!< Name of the preview window
  static const char * helpName() { return "HEPData Help###HEPDataHelp"; }          //!< Name of the help window

  //! @brief Open the import window. @param path folder (or file) proposed
  void openImport(const String & path);
  //! @brief Open the export window. @param files data files offered @param directory directory proposed
  void openExport(const std::vector<ExportFile> & files, const String & directory);
  //! @brief Open the help window (drawn by render()).
  void openHelp() { _helpOpen = true; _helpFocus = true; }
  //! @brief Whether the help window is open. @return true if so
  bool helpShown() const { return _helpOpen; }
  //! @brief Draw the help: import, export, uncertainties, functions and fits, lossless and strict files, publication,
  //! the task HEPDataExporter (in the current ImGui window).
  static void helpText();
  //! @brief Whether a window is open (the help window aside). @return true if so
  bool isOpen() const { return _open || _previewOpen; }
  //! @brief Mode of the window. @return import or export
  Mode mode() const { return _mode; }
  //! @brief Draw the windows, if open; call once per frame. @param files file dialog used by the Browse buttons
  void render(FileDialog & files);

  //! @name What the buttons do (also for programs and tests)
  //! @{
  //! @brief Preview: read and convert (import) or build (export), and show the preview. @return false if nothing can be done
  bool preview();
  //! @brief Whether OK can be clicked: a preview is shown, not stale, with something to do. @return true if so
  bool canConfirm() const;
  //! @brief OK: do what the preview shows, then close both windows. @return false on failure (message shown)
  bool confirm();
  //! @brief Cancel: close the preview; nothing is done.
  void cancel();
  //! @brief Close both windows.
  void close();
  //! @}

  //! @name State (for programs and tests)
  //! @{
  String & importPath() { return _importPath; }                      //!< folder (or file) to import
  String & importName() { return _importName; }                      //!< name of the data file made (empty: from the folder)
  HEPDataConverter::ImportOptions & importOptions() { return _importOptions; }   //!< options of the import
  const HEPDataImportPlan & importPlan() const { return _importPlan; }            //!< import prepared by Preview
  HEPDataExportPlan & exportPlan() { return _exportPlan; }           //!< export (metadata and items)
  std::vector<ExportFile> & exportFiles() { return _files; }         //!< data files offered for export
  //! @brief Make the items again from the data files selected, keeping the settings of those already there.
  void updateItems();
  const HEPDataPreviewNode & previewTree() const { return _tree; }   //!< preview shown
  bool previewShown() const { return _previewOpen; }                 //!< whether the preview is shown
  bool stale() const { return _stale; }                              //!< changed since the preview
  void setStale() { _stale = true; }                                  //!< mark the preview stale
  const String & message() const { return _message; }                //!< last message
  unsigned int & previewRows() { return _rows; }                     //!< rows shown per table in the preview
  //! @}

  ImportCallback onImport;                                    //!< OK of an import (makes the data file)
  std::function<void(const String &)> onStatus;              //!< tells the outcome (status line)

  protected:
  void renderImport(FileDialog & files);
  void renderExport(FileDialog & files);
  void renderItemEditor(HEPDataExportPlan::Item & item);
  void renderPreview();
  void renderNode(const HEPDataPreviewNode & node, int & id);
  //! @brief An edited text field: marks the preview stale. @return true if edited
  bool edit(const char * label, String & value, const char * hint=nullptr, float width=0.0f);
  //! @brief Set the message. @param text text @param error whether it is an error
  void say(const String & text, bool error);

  Mode _mode = kImport;                      //!< import or export
  bool _open = false;                        //!< the window is open
  bool _focus = false;                       //!< bring the window to the front at the next frame
  bool _previewOpen = false;                 //!< the preview is open
  bool _previewFocus = false;                //!< bring the preview to the front at the next frame
  bool _helpOpen = false;                    //!< the help window is open
  bool _helpFocus = false;                   //!< bring the help to the front at the next frame
  bool _stale = false;                       //!< changed since the preview
  int  _expand = 0;                          //!< 1: expand all, -1: collapse all (at the next frame of the preview)
  unsigned int _rows = 3;                    //!< rows shown per table in the preview
  unsigned int _count = 0;                   //!< objects (import) or tables (export) of the preview
  HEPDataPreviewNode _tree;                  //!< preview shown
  String _message;                           //!< outcome of the last action
  bool _error = false;                       //!< the message is an error
  // import
  String _importPath;                        //!< folder (or file) to read
  String _importName;                        //!< name of the data file made (empty: that of the folder)
  String _commonLabels;                      //!< labels of the common systematic uncertainty, separated by commas
  HEPDataConverter::ImportOptions _importOptions;   //!< options of the conversion
  HEPDataImportPlan _importPlan;             //!< import prepared by Preview
  // export
  std::vector<ExportFile> _files;            //!< data files offered
  HEPDataExportPlan _exportPlan;             //!< the export
  int _selected = -1;                        //!< item whose table is edited
  int _archive = 0;                          //!< 0 none, 1 .tar.gz, 2 .zip
  String _archiveName;                       //!< name of the archive (in the parent of the directory)
  };

  } // namespace GUI
} // namespace Datum

#endif // !DATUM__GUI__HEPDataDialog
