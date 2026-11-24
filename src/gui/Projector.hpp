/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file Projector.hpp
//! @brief Projector: projections of maps (histograms of rank 2) onto x or y in DatumGui, over a slice that can be dragged.
#ifndef DATUM__GUI__Projector
#define DATUM__GUI__Projector
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "String.hpp"
#include "Histogram.hpp"

namespace Datum
{
  namespace GUI
  {
  class CanvasPane;

  //!
  //! @brief Projections of the maps of DatumGui onto x or y (context menu of a graph > Project), each shown in a
  //! canvas below the canvas of the map, with the slice summed marked on the map.
  //!
  //! A map (a histogram of rank 2, MATH::Histogram, not a profile) is projected onto x (the bins of y summed, for each
  //! x) or onto y, over the whole range of the other axis or over a range chosen with the mouse (CanvasPane::
  //! selectRange()). The projection (MATH::Histogram::project2DOnto1D()) is shown in a new canvas docked below that of
  //! the map, and the range summed is marked on the map by two lines (CanvasPane::setRangeMarker()). Dragging a line
  //! projects again when the mouse rests and when it is released; the lines then snap to the edges of the bins summed.
  //! The title of the projection gives the range (e.g. "1.2 < y < 3.4").
  //!
  //! The context menu of the canvas of a projection has Save Projection... (a name is asked; onSave adds the
  //! histogram to a folder) and Close Projection. A projection ends when its canvas is closed; when its map is no
  //! longer shown, the projection stays as it is, without lines to drag. The histogram of a projection that ended is
  //! kept for the session: undo may show its canvas again.
  //!
  class Projector
  {
  public:
  //! @brief A projection shown.
  struct Projection
  {
    int id = 0;                                    //!< identifier (also that of the marker of the range)
    std::string sourcePane;                        //!< window of the canvas of the map ("###name")
    int graph = 0;                                 //!< graph of the map in that canvas
    const MATH::Histogram * source = nullptr;      //!< the map (not owned; null when it is no longer shown)
    String label;                                  //!< its label in the graph
    int axis = 0;                                  //!< axis kept: 0 (onto x, a range of y summed) or 1 (onto y)
    bool whole = true;                             //!< the whole range of the other axis
    double low = 0.0, high = 0.0;                  //!< the range summed: edges of the first and last bins
    std::string targetPane;                        //!< window of the canvas of the projection
    std::unique_ptr<MATH::Histogram> histogram;    //!< the projection shown (its address stays: the graph shows it)
  };

  //! @name The computation (no window needed)
  //! @{
  //!
  //! @brief The bins of an axis of a map from a range: the edges nearest to low and to high (at least one bin, within
  //! the axis). @param h map @param axis axis (0 or 1) @param low lower end @param high upper end
  //! @param first set to the first bin @param last set to the last bin
  //!
  static void binRange(const MATH::Histogram & h, unsigned int axis, double low, double high, unsigned int & first,
                       unsigned int & last);
  //!
  //! @brief The projection of a map onto an axis, the other axis summed over a range (snapped to the edges of its
  //! bins). @param h map (rank 2) @param keptAxis 0 (onto x) or 1 (onto y) @param low lower end of the range
  //! @param high upper end @param lowEdge set to the lower edge of the first bin summed @param highEdge set to the
  //! upper edge of the last @return projection, named <name>_px or <name>_py, titled with the range
  //! @throws Exception if the map is not of rank 2
  //!
  static MATH::Histogram project(const MATH::Histogram & h, int keptAxis, double low, double high, double & lowEdge,
                                 double & highEdge);
  //! @brief The range summed as text, e.g. "1.2 < y < 3.4" (the title of the summed axis if short, else x or y).
  //! @param h map @param keptAxis axis kept @param low lower edge @param high upper edge @return text
  static String rangeText(const MATH::Histogram & h, int keptAxis, double low, double high);
  //! @}

  //! @brief The canvas panes, as the window of the application gives them (DatumGui: paneIndexOfWindow(), pane(),
  //! addPaneBelow(), requestClosePane()).
  struct Panes
  {
    std::function<CanvasPane *(const std::string & windowId)> find;     //!< the pane of a window, or null
    std::function<CanvasPane &(const std::string & windowId)> addBelow; //!< a new pane docked below a window
    std::function<void(const std::string & windowId)> close;            //!< close the pane of a window
  };
  //! @brief Constructor (the panes given with setPanes()).
  Projector() {}
  //! @brief The canvas panes of the window. @param panes functions
  void setPanes(const Panes & panes) { _panes = panes; }

  //!
  //! @brief Project item k of graph g of a pane onto an axis (context menu > Project): over the whole range of the
  //! other axis, or once a range is chosen with the mouse. A projection of the same map onto the same axis in the
  //! same graph is replaced. @param pane pane of the map @param graph graph @param item item (a histogram of rank 2)
  //! @param axis 0 (onto x) or 1 (onto y) @param whole whole range (otherwise chosen with the mouse)
  //! @return false if the item is not a map that can be projected
  //!
  bool request(CanvasPane & pane, int graph, int item, int axis, bool whole);
  //! @brief The lines of a range were moved (CanvasPane::onRangeMoved): projected again; the lines snapped to the
  //! bins when final. @param id projection @param low lower end @param high upper end @param final drag ended
  void moved(int id, double low, double high, bool final);
  //! @brief Once per frame: projections whose canvas was closed dropped, lines of the maps shown again (canvases
  //! drawn again), maps no longer shown noted.
  void update();
  //! @brief Items of the context menu of a graph (CanvasPane::contextMenuExtras): on a canvas of a projection, Save
  //! Projection... and Close Projection. @param pane pane @param graph graph
  void contextMenu(CanvasPane & pane, int graph);
  //! @brief Draw the Save Projection dialog, if open (once per frame).
  void renderDialogs();
  //! @brief Open the Save Projection dialog for a projection. @param id projection
  void openSave(int id);
  //! @brief Save a projection under a name (as OK of the dialog). @param id projection @param name name
  //! @param message set to what to tell @return false on failure
  bool save(int id, const String & name, String & message);
  //! @brief The projections shown. @return projections
  const std::vector<std::unique_ptr<Projection>> & projections() const { return _projections; }
  //! @brief The projection of an id, or null. @param id id @return projection
  Projection * projection(int id);
  //! @brief The projection shown in a pane, or null. @param pane pane @return projection
  Projection * projectionIn(const CanvasPane & pane);

  //! Adds a projection to the folder chosen (OK of Save Projection): @p histogram, under @p name; sets @p message
  //! (where it went, or why it could not be saved) and returns false on failure. With a null histogram, only tells
  //! where it would go (message), and returns false if the name is taken there.
  std::function<bool(const MATH::Histogram * histogram, const String & name, String & message)> onSave;
  //! Called with what to tell (status line).
  std::function<void(const String &)> onStatus;

  protected:
  //! @brief Project again over the current range and show it in the canvas of the projection.
  void refresh(Projection & p);
  //! @brief The pane of a window, or null. @param windowId window @return pane
  CanvasPane * paneOf(const std::string & windowId);
  //! @brief Whether the map of a projection is still shown in its graph. @param p projection @return true if so
  bool sourceShown(const Projection & p);
  //! @brief Mark the range on the map. @param p projection
  void mark(Projection & p);

  Panes _panes;                                          //!< the canvas panes of the window
  std::vector<std::unique_ptr<Projection>> _projections; //!< projections shown
  //! Histograms of the projections dropped: kept, since the states of undo may still show them (as closed files are)
  std::vector<std::unique_ptr<MATH::Histogram>> _retired;
  int _nextId = 1;                                       //!< next identifier
  int _saveId = 0;                                       //!< projection saved by the dialog (0: closed)
  bool _saveRequested = false;                           //!< open the dialog at the next frame
  std::string _saveName;                                 //!< name typed
  String _saveMessage;                                   //!< message of the dialog
  bool _saveError = false;                               //!< the message is an error
  };

  } // namespace GUI
} // namespace Datum

#endif // !DATUM__GUI__Projector
