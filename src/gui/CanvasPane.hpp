/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file CanvasPane.hpp
//! @brief CanvasPane: canvas pane of the DatumGui window, with its toolbar and mouse interactions.
#ifndef DATUM__GUI__CanvasPane
#define DATUM__GUI__CanvasPane
#include <functional>
#include "Canvas.hpp"
#include "BezierCurve.hpp"
#include "FileBrowser.hpp"

struct ImFont;

namespace Datum
{
  namespace GUI
  {

  class ImGuiPainter;

  //!
  //! @brief One canvas pane of the DatumGui window: a GRAPHICS::Canvas (divided into graphs) with a toolbar.
  //!
  //! Toolbar: Add / Replace (what showing an object does to the selected graph), Log X, Log Y (scales of the
  //! selected graph), Graphs (divide the canvas into n' x m' graphs), Clear (empty the selected graph), + (new pane).
  //! Clicking a graph selects it (blue frame); the mouse position is reported in data coordinates (hoverText()).
  //! Objects dragged from the file browser can be dropped on any graph (onDrop): the graph under the mouse is
  //! highlighted while dragging.
  //!
  //! Mouse on a graph:
  //!  - drag in the frame: zoom on the rectangle; drag along an axis: zoom on that axis; double-click: automatic ranges
  //!  - drag the title, an axis title or the legend: move it; Shift + drag an axis: move its labels
  //!  - right-click the title, an axis, an axis title, the legend or a data point: dialog editing it (GraphEditor);
  //!    elsewhere: context menu of the graph
  //!  - Escape cancels a drag
  //!  - the lines of a range marked on a graph (setRangeMarker(), e.g. the slice of a projection): drag a line, or its
  //!    handle, to move it; the owner is told when the mouse rests and when it is released (onRangeMoved)
  //!  - while a range is being chosen (selectRange()): drag across the graph along the axis of the range
  //!
  //! beforeChange is called before any change made from the pane, so that the application can record an undo state.
  //!
  class CanvasPane
  {
  public:

  //! @brief What showing an object does to the selected graph: add it, or replace its contents
  enum Mode { kAdd, kReplace };
  //! @brief Comparison of two histograms A and B bin by bin (onCompare): B - A, B / A, or (B - A)/sqrt(sA^2 + sB^2)
  enum Comparison { kDifference, kRatio, kSignificance };

  //! @brief Constructor: one graph. @param name name of the pane, unique in the window (e.g. "Canvas 1")
  CanvasPane(const String & name);

  GRAPHICS::Canvas & canvas() { return _canvas; }   //!< The canvas of the pane
  const GRAPHICS::Canvas & canvas() const { return _canvas; }   //!< The canvas of the pane
  const String & name() const { return _name; }   //!< Name of the pane (identifies its window)
  const String & title() const { return _title; }   //!< Title shown on the tab of the pane (the name at first)
  //! @brief Set the title shown on the tab (the window keeps its identity). @param title title
  void setTitle(const String & title);
  const char * windowId() const { return _windowId.c_str(); }   //!< "###name": id of the window, whatever its title
  const char * windowLabel() const { return _windowLabel.c_str(); }   //!< "title###name": label of the window

  Mode mode() const { return _mode; }   //!< Add or Replace
  void setMode(Mode mode) { _mode = mode; }   //!< Set Add or Replace
  //! @brief Whether the shapes of a graph stay when its contents are replaced (Replace mode, folders shown).
  bool keepShapes() const { return _keepShapes; }
  void setKeepShapes(bool keep) { _keepShapes = keep; }   //!< Keep the shapes of a graph when its contents are replaced
  //! @brief Index of the selected graph (the target of Add/Replace, Log X/Y, Copy/Paste).
  int  selectedGraph() const { return _selectedGraph; }
  //! @brief Select graph k (nothing if out of range). @param k index of the graph
  void setSelectedGraph(int k);
  GRAPHICS::Graph & currentGraph() { return _canvas.graph(_selectedGraph); }   //!< The selected graph

  //! @brief Show an item in the selected graph, added or replacing the contents according to mode(). @return item
  GRAPHICS::GraphItem & show(const GRAPHICS::GraphItem & item);
  //! @brief Divide the canvas into nColumns x nRows graphs (keeps the existing graphs, see Canvas::divide()).
  void divide(int nColumns, int nRows);

  //! Tools of the shape palette (left of the toolbar): select and move, or draw a shape or a text box over a graph
  enum Tool { kSelectTool, kRectangleTool, kEllipseTool, kTriangleTool, kPolygonTool, kLineTool, kTextTool };
  Tool tool() const { return _tool; }   //!< Tool of the palette in use
  void setTool(Tool tool) { _tool = tool; _drafting = false; }   //!< Use a tool of the palette
  //! @brief Whether a shape is selected (clicked with the select tool). @return true if so
  bool hasSelectedShape() const;
  //! @brief Remove the selected shape (undo recorded).
  void deleteSelectedShape();

  //! @name Curve edited with the mouse
  //! @{
  //!
  //! @brief Edit a curve shown in a graph of the pane (e.g. a map of the Detector Maps window): its knots are drawn,
  //! dragged with the mouse (kept within the bounds of the curve and between their neighbors), a click on the curve
  //! adds a knot, a right-click on a knot removes it. The curve is not owned: it must stay alive while edited.
  //! @param curve curve (null: none) @param changed called after each change
  //!
  void editCurve(MATH::BezierCurve * curve, const std::function<void()> & changed);
  //! @brief Stop editing the curve.
  void stopEditingCurve() { _curve = nullptr; _curveChanged = nullptr; }
  //! @brief The curve edited, or null. @return curve
  MATH::BezierCurve * editedCurve() const { return _curve; }
  //! @}

  //! @name Ranges marked on a graph (the slices of projections)
  //! A range is drawn as two lines across the frame of a graph, with a handle and the value at one end of each, and
  //! the band between them lightly shaded. A line is dragged by itself or by its handle (Escape puts it back);
  //! onRangeMoved is called when the mouse rests during the drag (final false) and when it is released (final true).
  //! @{
  //! @brief A range marked on a graph.
  struct RangeMarker
  {
  int    id = 0;         //!< identifier, chosen by the owner
  int    graph = 0;      //!< graph of the pane
  int    axis = 1;       //!< 0: vertical lines at x = low and x = high; 1: horizontal lines at y = low and y = high
  double low = 0.0;      //!< lower end (data coordinates)
  double high = 1.0;     //!< upper end (data coordinates)
  };
  //! @brief Mark a range (replacing the marker of the same id). @param marker marker
  void setRangeMarker(const RangeMarker & marker);
  //! @brief Remove the marker of an id. @param id id
  void removeRangeMarker(int id);
  //! @brief The marker of an id, or null. @param id id @return marker
  const RangeMarker * rangeMarker(int id) const;
  //! @brief The markers. @return markers
  const std::vector<RangeMarker> & rangeMarkers() const { return _markers; }
  //!
  //! @brief Choose a range with the mouse: the next drag of the left button across graph k, along an axis, gives the
  //! range to done (Escape, or a click elsewhere, cancels). @param graph graph @param axis 0 (a range of x) or 1 (y)
  //! @param done called with the range (low < high, data coordinates)
  //!
  void selectRange(int graph, int axis, const std::function<void(double low, double high)> & done);
  //! @brief Stop choosing a range (done is not called).
  void cancelRangeSelection() { _rangeGraph = -1; _rangeDone = nullptr; }
  //! @brief Whether a range is being chosen. @return true if so
  bool selectingRange() const { return _rangeGraph >= 0; }
  //! @brief Move a line of a marker as a drag would (tests, programs): the line and the value, then onRangeMoved
  //! (final). @param id marker @param line 0 the lower line, 1 the upper one @param value new value
  void moveRangeLine(int id, int line, double value);
  //! Called when the lines of a marker were moved: the mouse rests (final false) or was released (final true).
  std::function<void(int id, double low, double high, bool final)> onRangeMoved;
  //! @}

  //! @brief Window holding the pane: -1 for the main window, otherwise a window of canvases (DatumGui::addPaneWindow()).
  int  host() const { return _host; }
  //! @brief Set the window holding the pane. @param host -1 for the main window, or the index of a window of canvases
  void setHost(int host) { _host = host; }
  bool showToolbar() const { return _showToolbar; }   //!< Whether the toolbar is shown
  void setShowToolbar(bool v) { _showToolbar = v; }   //!< Show or hide the toolbar

  //! @brief Draw the pane (toolbar and canvas) in the current ImGui window. @param width width available
  //! @param height height available @param isCurrent whether this pane is the current pane (highlighted)
  //! @param font font of the plots
  //! @return true if the pane was clicked (it should become the current pane)
  bool render(float width, float height, bool isCurrent, ImFont * font);

  //! @brief Position under the mouse in data coordinates, or empty. @return text
  const String & hoverText() const { return _hoverText; }
  //! @brief Screen rectangle of the canvas at the last render (pixels). @param x left edge
  //! @param y top edge @param w width @param h height
  void canvasRect(float & x, float & y, float & w, float & h) const { x = _rectX; y = _rectY; w = _rectW; h = _rectH; }

  //! Called before the pane changes its canvas (undo).
  std::function<void()> beforeChange;
  //! Called when an object of the file browser is dropped on graph k of the pane.
  std::function<void(int graph, const ObjectReference & object)> onDrop;
  //! Called by the + button of the toolbar (new canvas pane).
  std::function<void()> onAddPane;
  //! Called by the close button of the toolbar (x): close this pane.
  std::function<void()> onClosePane;
  //! Called to show the values of data points shown in a graph as a table (graph menu, Show Data).
  std::function<void(const MATH::DataPoints * data, const String & label)> onShowData;
  //! Called by "Fit..." in the context menu of a graph (made the selected graph), with the index of the item to fit (-1: any).
  std::function<void(int item)> onFit;
  //! Called by "Residual Map" in the context menu of a graph, for fit k of data points of rank 2.
  std::function<void(const MATH::DataPoints * data, unsigned int fit)> onResidualMap;
  //! Called by "Projections" in the context menu of a graph, for a histogram of rank 2 with fits.
  std::function<void(const MATH::DataPoints * data)> onProjections;
  //! Called by "Project" in the context menu of a graph showing a histogram of rank 2 (2D view): item k of graph g
  //! projected onto axis (0: onto x, the y range summed; 1: onto y), over the whole range of the other axis, or over
  //! a range chosen with the mouse (whole false).
  std::function<void(int graph, int item, int axis, bool whole)> onProject;
  //! Called by "Extract" in the context menu of graph k, showing histograms of rank 1, or maps in a plane view: a
  //! segment of the histograms (or a region of the maps) between lines that can be dragged.
  std::function<void(int graph)> onExtract;
  //! Called while the context menu of graph k is drawn, after the items of the pane: menu items of the application
  //! (e.g. Save Projection).
  std::function<void(int graph)> contextMenuExtras;
  //! Called by "Compare" in the context menu of a graph, for two histograms of the same rank shown in it, with the
  //! comparison chosen (see Comparison).
  std::function<void(const MATH::DataPoints * first, const MATH::DataPoints * second,
                     const String & firstLabel, const String & secondLabel, int comparison)> onCompare;
  //! The histogram a histogram shown is rebinned from, and its factors (empty if it is not rebinned), for "Rebin" in
  //! the context menu of a graph.
  std::function<const MATH::DataPoints *(const MATH::DataPoints * data, std::vector<unsigned int> & factors)> rebinSource;
  //! Called by "Rebin" in the context menu of a graph: show item k of graph g rebinned by these factors (one per axis;
  //! all 1: the original histogram).
  std::function<void(int graph, int item, const std::vector<unsigned int> & factors)> onRebin;
  //! Called by "Save Graph As..." in the context menu of graph k.
  std::function<void(int graph)> onSaveGraph;
  //! Called by "Save Canvas As..." in the context menu of a graph.
  std::function<void()> onSaveCanvas;
  //! Called by "Rename Canvas..." in the context menu of a graph.
  std::function<void()> onRename;

  protected:

  String _name;   //!< name of the pane
  String _title;   //!< title shown on the tab
  std::string _windowId;   //!< "###name"
  std::string _windowLabel;   //!< "title###name"
  GRAPHICS::Canvas _canvas;   //!< the canvas, divided into graphs
  Mode _mode;   //!< Add or Replace
  bool _keepShapes;   //!< the shapes of a graph stay when its contents are replaced
  int  _selectedGraph;   //!< index of the selected graph
  bool _showToolbar;   //!< the toolbar is shown
  int  _contextGraph;   //!< graph whose context menu is open
  int  _rebinOther = 2;  //!< factor typed in "Rebin > Other" of the context menu
  int  _gridColumns;   //!< values edited in the Graphs popup
  int  _gridRows;   //!< values edited in the Graphs popup
  String _hoverText;   //!< data coordinates under the mouse, or empty
  float _rectX;   //!< screen rectangle of the canvas at the last render (pixels)
  float _rectY;   //!< screen rectangle of the canvas at the last render (pixels)
  float _rectW;   //!< screen rectangle of the canvas at the last render (pixels)
  float _rectH;   //!< screen rectangle of the canvas at the last render (pixels)

  //! What a drag of the left mouse button does.
  enum DragKind { kNoDrag, kZoom, kZoomX, kZoomY, kMoveTitle, kMoveAxisTitle, kMoveLegend, kMoveLabels, kRotate,
                  kMoveShape, kMoveVertex, kMoveKnot, kMoveRange, kSelectRange };
  //! State of a drag of the left mouse button
  struct Drag
  {
  DragKind kind = kNoDrag;   //!< what the drag does
  int   graph = -1;   //!< graph dragged in
  int   axis  = -1;   //!< axis zoomed, or whose title or labels are moved
  float startX = 0;      //!< screen position where the drag started (pixels)
  float startY = 0;      //!< screen position where the drag started (pixels)
  float initialX = 0;  //!< position (offset, legend corner, label gap) before the drag
  float initialY = 0;  //!< position (offset, legend corner, label gap) before the drag
  bool  moved = false;   //!< the mouse moved enough to start the drag
  int   shape = -1;   //!< shape moved (kMoveShape), or whose point is moved (kMoveVertex)
  int   vertex = -1;   //!< point of the shape moved (kMoveVertex)
  std::vector<GRAPHICS::Point2D> points;   //!< points of the shape before the drag
  double initialLow = 0.0;   //!< range of the marker before the drag (kMoveRange)
  double initialHigh = 0.0;  //!< range of the marker before the drag (kMoveRange)
  double restTime = 0.0;     //!< time the mouse last moved (kMoveRange)
  float  restX = 0, restY = 0;   //!< position of the mouse then
  bool   reported = true;    //!< onRangeMoved called since the mouse last moved
  };
  Drag _drag;   //!< drag in progress (kind kNoDrag if none)
  int  _editGraph;                     //!< graph whose part is edited in the "editor" popup
  GRAPHICS::Graph::Pick _editPick;     //!< part edited
  bool _editRecorded;                  //!< undo state recorded for the current dialog
  bool _openEditor;                    //!< open the editor popup at the next opportunity
  GRAPHICS::Graph _editBefore;         //!< graph when the dialog was opened (for undo)
  Tool _tool;                          //!< tool of the shape palette in use
  GRAPHICS::Shape _shapeStyle;         //!< attributes given to new shapes (those of the shape edited last)
  int  _host = -1;                     //!< window holding the pane (-1: the main window)
  int  _shapeGraph;                    //!< graph of the selected shape (-1: no shape selected)
  int  _shapeIndex;                    //!< index of the selected shape in its graph
  bool _drafting;                      //!< a shape is being drawn
  int  _draftGraph;                    //!< graph of the shape being drawn
  GRAPHICS::Shape _draft;              //!< shape being drawn (points: fractions of the cell)
  bool _openShapeEditor;               //!< open the shape dialog at the next opportunity
  bool _openShapeMenu;                 //!< open the context menu of the selected shape at the next opportunity
  MATH::BezierCurve * _curve = nullptr;   //!< curve edited with the mouse (not owned), or null
  std::function<void()> _curveChanged;     //!< called after each change of the curve
  std::vector<RangeMarker> _markers;       //!< ranges marked on the graphs
  int  _rangeGraph = -1;                   //!< graph where a range is being chosen (-1: none)
  int  _rangeAxis = 1;                     //!< axis of the range being chosen
  int  _hoverMarker = -1;                  //!< marker under the mouse at the last frame (index), or -1
  int  _hoverLine = -1;                    //!< its line under the mouse (0 lower, 1 upper)
  std::function<void(double, double)> _rangeDone;   //!< called with the range chosen
  //! @brief Screen position of a line of a marker: the line from (x0, y0) to (x1, y1), its handle at (hx, hy).
  //! @return false if the graph is not drawn as a plane or the line is outside the frame
  bool rangeLine(ImGuiPainter & painter, const RangeMarker & marker, int line, float & x0, float & y0, float & x1,
                 float & y1, float & hx, float & hy) const;
  //! @brief Mouse on the lines of the markers of graph k. @return true if the mouse was used
  bool handleRangeMarkers(ImGuiPainter & painter, int graph);
  //! @brief Mouse while a range is chosen in graph k. @return true if the mouse was used
  bool handleRangeSelection(ImGuiPainter & painter, int graph);
  //! @brief Draw the markers. @param painter painter
  void drawRangeMarkers(ImGuiPainter & painter);
  //! @brief Value of the position p along an axis of graph k. @return value
  double rangeValue(int graph, int axis, const GRAPHICS::Point2D & p) const;
  //! @brief Graph showing the edited curve. @return index, or -1
  int curveGraph() const;
  //! @brief Mouse on the edited curve (knots dragged, added, removed). @return true if the mouse was used
  bool handleCurve(ImGuiPainter & painter, int graph);
  //! @brief Knots of the edited curve. @param painter painter
  void drawCurveKnots(ImGuiPainter & painter);

  void changing() { if (beforeChange) beforeChange(); }   //!< Record the undo state before a change
  //! @brief Draw the toolbar: Add/Replace, Log X/Y, Graphs, Clear, +.
  void renderToolbar();
  //! @brief Draw the context menu of a graph, if open.
  void renderContextMenu();
  //! @brief Draw the Rebin menu of an item of the context menu. @param graph graph @param item item
  void renderRebinMenu(int graph, int item);
  //! @brief Draw the dialog editing a part of a graph, if open (see GraphEditor).
  void renderEditor();
  //! Open the dialog editing the part pick of graph k.
  void edit(int k, const GRAPHICS::Graph::Pick & pick);
  //! Mouse buttons on the canvas: drags (zoom, move), double-click, right-click. @return true if the pane was clicked
  bool handleMouse(ImGuiPainter & painter, bool hovered);
  //! Apply the move of the current drag (screen displacement dx, dy in pixels).
  void applyMove(float dx, float dy);
  //! @brief Draw the palette of tools (select, rectangle, ellipse, triangle, polygon, line).
  void renderPalette();
  //! @brief Mouse with a drawing tool: draw a new shape in the graph under the mouse. @return true if the pane was clicked
  bool handleDrawing(ImGuiPainter & painter, bool hovered, int graph, const GRAPHICS::Point2D & p);
  //! @brief Outline and handles of the selected shape, and the shape being drawn.
  void drawShapeOverlay(ImGuiPainter & painter);
  //! @brief Context menu and dialog of the selected shape, if open.
  void renderShapeMenus();
  //! @brief The selected shape, or null (checked against the graphs, which undo may have changed). @return shape
  GRAPHICS::Shape * selectedShape();
  };

  } // namespace GUI
} // namespace Datum

#endif // !DATUM__GUI__CanvasPane
