/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file Extractor.hpp
//! @brief Extractor: segments of the histograms of a graph of DatumGui (1D) or regions of its maps (2D), between lines
//! that can be dragged.
#ifndef DATUM__GUI__Extractor
#define DATUM__GUI__Extractor
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "String.hpp"
#include "Histogram.hpp"
#include "Projector.hpp"

namespace Datum
{
  namespace GUI
  {
  class CanvasPane;

  //!
  //! @brief Extractions of the histograms of DatumGui (context menu of a graph > Extract), each shown in a canvas below
  //! the canvas of the histograms, the range extracted marked on the graph by lines that can be dragged.
  //!
  //! A graph showing histograms of rank 1 gives two vertical lines (the range of x); a graph showing maps (histograms
  //! of rank 2, plane view) gives two vertical and two horizontal lines (the ranges of x and y). The lines start at a
  //! quarter and three quarters of the range shown. Every histogram of the graph of that rank is extracted over the
  //! range (MATH::Histogram::extract1(), extract2()), the bins whose edges are nearest to the lines, and the extracts
  //! are shown together in a new canvas docked below that of the graph; their titles give the range. Dragging a line
  //! extracts again when the mouse rests (0.3 s) and when it is released; the lines then snap to the edges of the bins
  //! (those of the first histogram).
  //!
  //! The context menu of the canvas of an extraction has Save Extraction... (a name asked for each histogram; onSave
  //! adds them to a folder) and Cancel Extraction (lines removed, canvas closed, nothing saved); that of the graph
  //! extracted has Cancel Extraction. An extraction ends when its canvas is closed; when the histograms are no longer
  //! shown, it stays as it is, without lines. The histograms of an extraction that ended are kept for the session
  //! (undo may show its canvas again).
  //!
  class Extractor
  {
  public:
  //! @brief An extraction shown.
  struct Extraction
  {
    int id = 0;                                              //!< identifier
    std::string sourcePane;                                  //!< window of the canvas of the histograms ("###name")
    int graph = 0;                                           //!< graph of the histograms in that canvas
    unsigned int rank = 1;                                   //!< 1: histograms (range of x), 2: maps (ranges of x and y)
    std::vector<const MATH::Histogram *> sources;            //!< the histograms (not owned; empty once no longer shown)
    std::vector<String> labels;                              //!< their labels in the graph
    double low[2] = { 0.0, 0.0 };                            //!< lower ends of the ranges of x and y (edges of bins)
    double high[2] = { 0.0, 0.0 };                           //!< upper ends
    std::string targetPane;                                  //!< window of the canvas of the extraction
    std::vector<std::unique_ptr<MATH::Histogram>> histograms;   //!< the extracts shown (their addresses stay)
  };

  //! @name The computation (no window needed)
  //! @{
  //!
  //! @brief A segment of a histogram of rank 1, or a region of a map, the ranges snapped to the edges of its bins
  //! (see Projector::binRange()). @param h histogram (rank 1 or 2) @param low lower ends (x, and y for a map)
  //! @param high upper ends @param lowEdge set to the lower edges of the first bins kept @param highEdge set to the
  //! upper edges of the last @return extract, named <name>_extract, titled with the ranges
  //! @throws Exception if the rank is not 1 or 2
  //!
  static MATH::Histogram extract(const MATH::Histogram & h, const double low[2], const double high[2], double lowEdge[2],
                                 double highEdge[2]);
  //! @brief The ranges as text, e.g. "1.5 < x < 4" or "1.5 < x < 4, 0 < y < 2" (axis titles if short).
  //! @param h histogram @param low lower ends @param high upper ends @return text
  static String rangeText(const MATH::Histogram & h, const double low[2], const double high[2]);
  //! @brief Identifier of the marker of an axis of an extraction. @param id extraction @param axis 0 x, 1 y @return id
  static int markerId(int id, int axis) { return kMarkerBase + 2*id + axis; }
  //! @}

  //! @brief Constructor (the panes given with setPanes()).
  Extractor() {}
  //! @brief The canvas panes of the window. @param panes functions
  void setPanes(const Projector::Panes & panes) { _panes = panes; }

  //! @brief Whether a graph can be extracted from: histograms of rank 1, or maps in a plane view. @param pane pane
  //! @param graph graph @return true if so
  static bool extractable(CanvasPane & pane, int graph);
  //!
  //! @brief Extract from graph g of a pane (context menu > Extract): lines at a quarter and three quarters of the
  //! ranges shown, the extracts in a canvas below. An extraction of the same graph is shown again. @param pane pane
  //! @param graph graph @return false if the graph shows nothing to extract
  //!
  bool request(CanvasPane & pane, int graph);
  //! @brief Lines moved (CanvasPane::onRangeMoved): extracted again; the lines snapped to the bins when final.
  //! Markers of other owners are ignored. @param markerId marker @param low lower end @param high upper end
  //! @param final drag ended @return true if the marker was one of an extraction
  bool moved(int markerId, double low, double high, bool final);
  //! @brief Once per frame: extractions whose canvas was closed dropped, lines shown again, histograms no longer shown noted.
  void update();
  //! @brief Items of the context menu of a graph (CanvasPane::contextMenuExtras): Save Extraction... and Cancel
  //! Extraction on the canvas of an extraction, Cancel Extraction on the graph extracted. @param pane pane @param graph graph
  void contextMenu(CanvasPane & pane, int graph);
  //! @brief Draw the Save Extraction dialog, if open (once per frame).
  void renderDialogs();
  //! @brief Open the Save Extraction dialog. @param id extraction
  void openSave(int id);
  //!
  //! @brief Save the extracts under names (as OK of the dialog): all, or none if a name is empty, holds '/', is given
  //! twice or is taken in the folder (onSave with a null histogram). @param id extraction @param names one per extract
  //! @param message set to what to tell @return false if nothing was saved
  //!
  bool save(int id, const std::vector<String> & names, String & message);
  //! @brief Cancel an extraction: lines removed, canvas closed, nothing saved. @param id extraction
  void cancel(int id);
  //! @brief The extractions shown. @return extractions
  const std::vector<std::unique_ptr<Extraction>> & extractions() const { return _extractions; }
  //! @brief The extraction of an id, or null. @param id id @return extraction
  Extraction * extraction(int id);

  //! Adds a histogram to the folder chosen under a name, setting the message, false on failure; with a null histogram,
  //! only tells where it would go (message) and whether the name is free there (see Projector::onSave).
  std::function<bool(const MATH::Histogram * histogram, const String & name, String & message)> onSave;
  //! Called with what to tell (status line).
  std::function<void(const String &)> onStatus;

  protected:
  static constexpr int kMarkerBase = 1000000;   //!< markers of extractions: apart from those of projections
  //! @brief Extract again over the current ranges and show the extracts in the canvas of the extraction.
  void refresh(Extraction & e);
  //! @brief Mark the ranges on the graph. @param e extraction
  void mark(Extraction & e);
  //! @brief Remove the lines of an extraction. @param e extraction
  void unmark(Extraction & e);
  //! @brief Whether the histograms of an extraction are all still shown in their graph. @param e extraction
  bool sourcesShown(const Extraction & e);
  //! @brief The pane of a window, or null. @param windowId window @return pane
  CanvasPane * paneOf(const std::string & windowId);

  Projector::Panes _panes;                               //!< the canvas panes of the window
  std::vector<std::unique_ptr<Extraction>> _extractions; //!< extractions shown
  std::vector<std::unique_ptr<MATH::Histogram>> _retired;   //!< extracts of extractions that ended (kept for undo)
  int _nextId = 1;                                       //!< next identifier
  int _saveId = 0;                                       //!< extraction saved by the dialog (0: closed)
  bool _saveRequested = false;                           //!< open the dialog at the next frame
  std::vector<std::string> _saveNames;                   //!< names typed
  String _saveMessage;                                   //!< message of the dialog
  };

  } // namespace GUI
} // namespace Datum

#endif // !DATUM__GUI__Extractor
