/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file HistogramOperations.hpp
//! @brief HistogramOperations: the Histograms menu of DatumGui (sum, difference, product, ratio, scaling of histograms).
#ifndef DATUM__GUI__HistogramOperations
#define DATUM__GUI__HistogramOperations
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
  //! @brief Operations on the histograms shown in a graph of DatumGui (menu Histograms): sum, difference, product,
  //! ratio, scaling by a constant.
  //!
  //! Choosing an operation opens a window listing the histograms of the selected graph of the current canvas. The
  //! histograms are chosen by clicking their names (highlighted, numbered in the order clicked: the order of the
  //! operands); each has a coefficient, 1 by default:
  //!  - Sum: c1 A + c2 B + ... (two or more histograms);
  //!  - Difference: c1 A - c2 B;
  //!  - Product: (c1 A) (c2 B), bin by bin;
  //!  - Ratio: (c1 A) / (c2 B), bin by bin (bins where either is empty, or the divisor zero, left empty);
  //!  - Scale: c A (one histogram).
  //! The histograms must have the same binning. OK computes the result (MATH::Histogram::scale(), add(),
  //! multiplyBy(), divideBy(): statistical uncertainties propagated, and the systematic ones when the first operand
  //! is a MATH::HistogramSyst) and shows it in a new canvas docked below the canvas of the graph; Cancel does
  //! nothing. The context menu of the canvas of a result has Save Result... (a name asked; onSave adds it to a
  //! folder) and Close Result. The histograms of results whose canvas was closed are kept for the session (undo may
  //! show their canvas again).
  //!
  class HistogramOperations
  {
  public:
  //! @brief The operations.
  enum Operation { kSum, kDifference, kProduct, kRatio, kScale };
  //! @brief A result shown.
  struct Result
  {
    int id = 0;                                    //!< identifier
    std::string targetPane;                        //!< window of the canvas of the result
    String expression;                             //!< what it is, e.g. "A + 2 B"
    std::unique_ptr<MATH::Histogram> histogram;    //!< the result shown
  };

  //! @name The computation (no window needed)
  //! @{
  //! @brief Name of an operation, e.g. "Sum". @param op operation @return name
  static const char * name(Operation op);
  //! @brief Fewest operands. @param op operation @return 2, or 1 (Scale)
  static unsigned int minimumOperands(Operation op) { return op == kScale ? 1u : 2u; }
  //! @brief Most operands. @param op operation @return 1 (Scale), 2, or unlimited (Sum)
  static unsigned int maximumOperands(Operation op) { return op == kScale ? 1u : (op == kSum ? 1000000u : 2u); }
  //! @brief The operation as text, e.g. "A + 2 B", "A - B", "(A) x (0.5 B)", "A / B", "3 A". @param op operation
  //! @param labels labels of the operands @param coefficients coefficients @return text
  static String expression(Operation op, const std::vector<String> & labels, const std::vector<double> & coefficients);
  //!
  //! @brief Compute an operation. @param op operation @param operands histograms, in order
  //! @param coefficients one per operand @param labels labels of the operands (for the name and title)
  //! @param error set to why it cannot be computed @return result (null on failure), named after the operation and
  //! titled with the expression
  //!
  static std::unique_ptr<MATH::Histogram> compute(Operation op, const std::vector<const MATH::Histogram *> & operands,
                                                  const std::vector<double> & coefficients, const std::vector<String> & labels,
                                                  String & error);
  //! @brief Number of histograms shown in a graph of a pane. @param pane pane @param graph graph @return count
  static unsigned int histogramsIn(CanvasPane & pane, int graph);
  //! @}

  //! @brief Constructor (the panes given with setPanes()).
  HistogramOperations() {}
  //! @brief The canvas panes of the window. @param panes functions
  void setPanes(const Projector::Panes & panes) { _panes = panes; }

  //! @brief Open the window of an operation for the histograms of graph g of a pane. @param op operation
  //! @param pane pane @param graph graph
  void open(Operation op, CanvasPane & pane, int graph);
  //! @brief Whether the window is open. @return true if so
  bool isOpen() const { return _open; }
  //! @brief Click on the histogram k of the list (select it, or unselect it). @param k index in the list
  void toggle(unsigned int k);
  //! @brief Set the coefficient of histogram k of the list. @param k index @param c coefficient
  void setCoefficient(unsigned int k, double c) { if (k < _coefficients.size()) _coefficients[k] = c; }
  //! @brief The histograms chosen, in order (indices in the list). @return indices
  const std::vector<unsigned int> & chosen() const { return _chosen; }
  //! @brief Labels of the histograms listed. @return labels
  const std::vector<String> & labels() const { return _labels; }
  //! @brief OK: compute, and show the result in a canvas below. @return false if it cannot be done (message set)
  bool confirm();
  //! @brief Cancel: close the window, nothing done.
  void cancel() { _open = false; _message = String(); }
  //! @brief Message of the window (why OK cannot be done). @return message
  const String & message() const { return _message; }

  //! @brief Once per frame: results whose canvas was closed dropped.
  void update();
  //! @brief Items of the context menu of a graph (CanvasPane::contextMenuExtras): Save Result... and Close Result on
  //! the canvas of a result. @param pane pane @param graph graph
  void contextMenu(CanvasPane & pane, int graph);
  //! @brief Draw the window of the operation and the Save Result dialog, if open (once per frame).
  void render();
  //! @brief Open the Save Result dialog. @param id result
  void openSave(int id);
  //! @brief Save a result under a name. @param id result @param name name @param message set to what to tell
  //! @return false on failure
  bool save(int id, const String & name, String & message);
  //! @brief The results shown. @return results
  const std::vector<std::unique_ptr<Result>> & results() const { return _results; }

  //! Adds a histogram to the folder chosen under a name (see Projector::onSave).
  std::function<bool(const MATH::Histogram * histogram, const String & name, String & message)> onSave;
  //! Called with what to tell (status line).
  std::function<void(const String &)> onStatus;

  protected:
  //! @brief The histograms of the graph, as listed (null if no longer shown). @return histograms
  std::vector<const MATH::Histogram *> listed();
  //! @brief Draw the window of the operation.
  void renderWindow();
  //! @brief Draw the Save Result dialog.
  void renderSave();

  Projector::Panes _panes;                       //!< the canvas panes of the window
  bool _open = false;                            //!< the window is open
  bool _focus = false;                           //!< bring it to the front at the next frame
  Operation _operation = kSum;                   //!< operation of the window
  std::string _sourcePane;                       //!< window of the canvas of the graph
  int _graph = 0;                                //!< graph of the histograms
  std::vector<const MATH::Histogram *> _histograms;   //!< histograms listed (not owned)
  std::vector<String> _labels;                   //!< their labels
  std::vector<double> _coefficients;             //!< their coefficients
  std::vector<unsigned int> _chosen;             //!< histograms chosen, in order
  String _message;                               //!< why OK cannot be done
  std::vector<std::unique_ptr<Result>> _results; //!< results shown
  std::vector<std::unique_ptr<MATH::Histogram>> _retired;   //!< results whose canvas was closed (kept for undo)
  int _nextId = 1;                               //!< next identifier
  int _saveId = 0;                               //!< result saved by the dialog (0: closed)
  bool _saveRequested = false;                   //!< open the dialog at the next frame
  std::string _saveName;                         //!< name typed
  String _saveMessage;                           //!< message of the dialog
  };

  } // namespace GUI
} // namespace Datum

#endif // !DATUM__GUI__HistogramOperations
