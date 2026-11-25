/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file FunctionTools.hpp
//! @brief FunctionTools: the Functions menu of DatumGui (new, open, save, duplicate, sum, difference, product, ratio,
//! scale of functions).
#ifndef DATUM__GUI__FunctionTools
#define DATUM__GUI__FunctionTools

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "String.hpp"
#include "ParameterizedFunction.hpp"
#include "ParameterizedComposite.hpp"

namespace Datum
{
  namespace GRAPHICS { class Graph; }
  namespace GUI
  {

  //!
  //! @brief The Functions menu of DatumGui: functions defined in a window, read from Datum files, saved, duplicated and
  //! combined.
  //!
  //! - New: a window to define a function (a class of FunctionRegistry or a composite, its parameters, its range and
  //!   its name); Add shows it in the selected graph of the current canvas (a canvas is made if there is none), and
  //!   the window stays open to add more.
  //! - Duplicate: the same window, filled with a copy of a function shown, to edit before Add.
  //! - Open: the functions of a Datum file (MATH::ParameterizedFunction objects, in any folder), checked to be shown.
  //! - Save, Save As: a function to a Datum file (the function itself, read back by Open), or as a table of its values
  //!   (.txt, .csv, .tsv, .dtb, .xlsx: x and f(x), or x, y and f(x, y) on a grid).
  //! - Sum, Difference, Product, Ratio, Scale: as the operations on histograms, of the functions shown in the selected
  //!   graph: chosen in order with a coefficient each; the result (a MATH::ParameterizedComposite: c1 A + c2 B + ...,
  //!   c1 A - c2 B, (c1 A)(c2 B), (c1 A)/(c2 B), c A) is shown in the graph and can be saved, fitted, combined again.
  //!
  //! The application shows the functions made (onShow) and owns them.
  //!
  class FunctionTools
  {
  public:
  //! @brief Operations on functions.
  enum Operation { kSum, kDifference, kProduct, kRatio, kScale };
  //! @brief A function shown in a graph: index of its item, function, range of its curve.
  struct Shown
  {
    int item = -1;                                          //!< index of the item in the graph
    const MATH::ParameterizedFunction * function = nullptr; //!< the function
    String label;                                           //!< label of the item
    double xMin = 0, xMax = 1, yMin = 0, yMax = 1;          //!< range of the curve (or map)
  };

  //! @name Computation (no window needed)
  //! @{
  //! @brief Name of an operation, e.g. "Sum". @param op operation @return name
  static const char * name(Operation op);
  //! @brief Fewest operands. @param op operation @return 2, or 1 (Scale)
  static unsigned int minimumOperands(Operation op) { return op == kScale ? 1u : 2u; }
  //! @brief Most operands. @param op operation @return 1 (Scale), 2, or unlimited (Sum)
  static unsigned int maximumOperands(Operation op) { return op == kScale ? 1u : (op == kSum ? 1000000u : 2u); }
  //! @brief The functions shown in a graph (MATH::ParameterizedFunction items). @param graph graph @return functions
  static std::vector<Shown> functionsIn(const GRAPHICS::Graph & graph);
  //! @brief The operation as text, e.g. "A + 2 B". @param op operation @param labels labels
  //! @param coefficients coefficients @return text
  static String expression(Operation op, const std::vector<String> & labels, const std::vector<double> & coefficients);
  //! @brief Combine functions. @param op operation @param operands functions, in order @param coefficients one per
  //! operand @param labels labels of the operands @param error set to why it cannot be done
  //! @return composite (null on failure), named after the operation and titled with the expression
  static std::unique_ptr<MATH::ParameterizedComposite> combine(Operation op,
                                                               const std::vector<const MATH::ParameterizedFunction *> & operands,
                                                               const std::vector<double> & coefficients,
                                                               const std::vector<String> & labels, String & error);
  //! @brief Write a function to a file: a Datum file (.datum: the function, added to the file or replacing the
  //! object of the same name), otherwise a table of its values (TableWriter: .txt, .csv, .tsv, .dtb, .xlsx).
  //! @param f function @param path file @param shown range of the values written @param error set on failure
  //! @return false on failure
  static bool write(const MATH::ParameterizedFunction & f, const String & path, const Shown & shown, String & error);
  //! @brief The functions of a Datum file. @param path file @param names set to their paths in the file
  //! @param error set on failure @return copies of the functions (owned by the caller)
  static std::vector<std::unique_ptr<MATH::ParameterizedFunction>> read(const String & path, std::vector<String> & names,
                                                                        String & error);
  //! @}

  //! @name Windows
  //! @{
  //! @brief New: the window to define functions, empty. @param xMin default range @param xMax default range
  void openNew(double xMin, double xMax);
  //! @brief Duplicate: the window filled with a copy of a function. @param shown function and its range
  void openDuplicate(const Shown & shown);
  //! @brief Open: the functions of a Datum file, to choose. @param path file @param xMin default range
  //! @param xMax default range
  void openFile(const String & path, double xMin, double xMax);
  //! @brief The window of an operation, for the functions of a graph. @param op operation @param graph graph
  void openOperation(Operation op, const GRAPHICS::Graph & graph);
  //! @brief Draw the windows that are open (once per frame).
  void render();
  //! @}

  //! Shows a function made by the windows (owned by the application from then on), over a range.
  std::function<void(std::unique_ptr<MATH::ParameterizedFunction> f, double xMin, double xMax, double yMin, double yMax)> onShow;
  //! Called with what to tell (status line).
  std::function<void(const String &)> onStatus;

  protected:
  //! @brief Draw the window that defines a function.
  void renderEditor();
  //! @brief Draw the window that lists the functions of a file.
  void renderFile();
  //! @brief Draw the window of an operation.
  void renderOperation();
  //! @brief Make the function of the editor of a class. @param className class or composite name
  void makePrototype(const String & className, unsigned int nParameters);

  // the editor (New, Duplicate)
  bool _editorOpen = false;                                   //!< the window is open
  bool _editorFocus = false;                                  //!< brought to the front at the next frame
  std::unique_ptr<MATH::ParameterizedFunction> _prototype;    //!< function edited
  std::string _className;                                     //!< its class ("" for a copy of a composite)
  int _nParameters = 0;                                       //!< number of parameters asked
  std::string _name;                                          //!< name given
  double _xMin = 0, _xMax = 10, _yMin = 0, _yMax = 10;        //!< range
  String _editorMessage;                                      //!< why Add cannot be done
  int _added = 0;                                             //!< functions added (for the default names)

  // the functions of a file (Open)
  bool _fileOpen = false;
  std::string _filePath;
  std::vector<std::unique_ptr<MATH::ParameterizedFunction>> _fileFunctions;
  std::vector<String> _fileNames;
  std::vector<char> _fileChecked;
  double _fileXMin = 0, _fileXMax = 10, _fileYMin = 0, _fileYMax = 10;

  // an operation
  bool _operationOpen = false;
  bool _operationFocus = false;
  Operation _operation = kSum;
  std::vector<std::unique_ptr<MATH::ParameterizedFunction>> _operands;   //!< copies of the functions of the graph
  std::vector<String> _labels;
  std::vector<double> _coefficients;
  std::vector<unsigned int> _chosen;
  double _opXMin = 0, _opXMax = 1, _opYMin = 0, _opYMax = 1;
  String _operationMessage;
  };

  } // namespace GUI
} // namespace Datum

#endif // !DATUM__GUI__FunctionTools
