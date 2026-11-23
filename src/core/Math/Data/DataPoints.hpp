#ifndef DATUM_MATH_DataPoints
#define DATUM_MATH_DataPoints
#include <vector>
#include "String.hpp"
#include "Object.hpp"
#include "DataPoint2D.hpp"
#include "FitRecord.hpp"

namespace Datum
{
  class BinaryWriter;
  class BinaryReader;
}

namespace Datum
{
  namespace MATH
  {

  //!
  //! @brief Abstract container of two-dimensional data points (DataPoint2D).
  //!
  //! Defines the interface shared by all collections of (x, y) points with uncertainties,
  //! independently of how the points are stored. Concrete classes must implement
  //! size(), point() and addPoint(const DataPoint2D&); everything else has a default
  //! implementation written in terms of these three functions, which concrete classes
  //! may override for efficiency.
  //!
  //! Points can be added one at a time (addPoint) or in bulk (addPoints), either as
  //! DataPoint2D objects or from parallel vectors x, y (and optionally uncertainties).
  //!
  //! Concrete implementations: Datum::GRAPHICS::Data2DPoints (stores DataPoint2D objects) and
  //! Datum::GRAPHICS::Data2DColumns (stores one vector per quantity), both through Datum::GRAPHICS::Data2D.
  //!
  //! Example:
  //! @code
  //!   std::vector<double> x = {1.0, 2.0, 3.0};
  //!   std::vector<double> y = {4.0, 5.0, 6.0};
  //!   Datum::GRAPHICS::Data2DPoints data;   // any concrete DataPoints
  //!   data.addPoints(x, y);
  //!   data.addPoint(Datum::MATH::DataPoint2D(4.0f, 7.0f));
  //!   float yMax = data.yMaximum();       // 7
  //! @endcode
  //!
  class DataPoints : public Object
  {
  public:

  //! @brief Default constructor: name and title "DataPoints".
  DataPoints();
  //! @brief Constructor with a name. @param name object name @param title object title @param className class name
  DataPoints(const String & name, const String & title, const String & className);

  //! @brief Destructor.
  virtual ~DataPoints() {}

  //! @name Interface to implement
  //! @{

  //! @brief Number of points in this container. @return number of points
  virtual unsigned int size() const = 0;

  //!
  //! @brief Number of independent variables of the data points: 1 for (x, y) points.
  //! @return rank of the data (1 by default; derived classes holding points of higher rank override it)
  //!
  virtual unsigned int rank() const { return 1; }

  //! @name Points of any rank
  //! Access to point index as coordinates (rank() values) and a value with uncertainties, valid for any rank.
  //! The default implementations, for rank 1, read point(index); classes holding points of higher rank override them.
  //! @{

  //! @brief Coordinates of a point. @param index index of the point @return rank() coordinates (x for rank 1)
  virtual std::vector<double> coordinatesAt(unsigned int index) const;
  //! @brief Uncertainties of the coordinates of a point. @param index index of the point @return rank() uncertainties (xError for rank 1)
  virtual std::vector<double> coordinateErrorsAt(unsigned int index) const;
  //! @brief Value of a point. @param index index of the point @return value (y for rank 1)
  virtual double valueAt(unsigned int index) const;
  //! @brief Statistical uncertainty of the value of a point. @param index index of the point @return statistical uncertainty (yStatError for rank 1)
  virtual double statErrorAt(unsigned int index) const;
  //! @brief Systematic uncertainty of the value of a point: the larger of the upper and lower ones. @param index index
  //! of the point @return systematic uncertainty (ySysError for rank 1)
  virtual double sysErrorAt(unsigned int index) const;
  //! @brief Upper systematic uncertainty of the value of a point. @param index index of the point @return uncertainty
  //! (here sysErrorAt(): classes holding asymmetric uncertainties override it)
  virtual double sysErrorPlusAt(unsigned int index) const;
  //! @brief Lower systematic uncertainty of the value of a point. @param index index of the point @return uncertainty
  //! (here sysErrorAt(): classes holding asymmetric uncertainties override it)
  virtual double sysErrorMinusAt(unsigned int index) const;

  //! @}

  //!
  //! @brief Return a copy of point number index.
  //! @param index point index, 0 <= index < size()
  //! @return the point
  //!
  virtual DataPoint2D point(unsigned int index) const = 0;

  //!
  //! @brief Append one point.
  //! @param dataPoint point to append
  //!
  virtual void addPoint(const DataPoint2D & dataPoint) = 0;

  //! @}

  //! @brief Check whether the container holds no points. @return true if size()==0
  bool empty() const { return size()==0; }

  //! @name Adding points
  //! Vectors of uncertainties may be empty, in which case the uncertainties are set to zero;
  //! otherwise all vectors must have the same size (MathException otherwise).
  //! @{

  //!
  //! @brief Append one point given its values and uncertainties.
  //!
  //! The default implementation builds a DataPoint2D and calls addPoint(const DataPoint2D&).
  //! @param x x value @param xError uncertainty on x @param y y value
  //! @param yStatError statistical uncertainty on y @param ySysError systematic uncertainty on y
  //!
  virtual void addPoint(float x, float xError, float y, float yStatError, float ySysError);

  //!
  //! @brief Append all the given points.
  //! @param dataPoints points to append
  //!
  virtual void addPoints(const std::vector<DataPoint2D> & dataPoints);

  //!
  //! @brief Append points from two vectors x and y (no uncertainties).
  //! @param x x values @param y y values (same size as x)
  //! @throws MathException if x and y have different sizes
  //!
  virtual void addPoints(const std::vector<double> & x,
                         const std::vector<double> & y);

  //!
  //! @brief Append points from vectors x, y and the statistical uncertainties on y.
  //! @param x x values @param y y values @param yStatError statistical uncertainties on y (or empty)
  //! @throws MathException if the vector sizes differ
  //!
  virtual void addPoints(const std::vector<double> & x,
                         const std::vector<double> & y,
                         const std::vector<double> & yStatError);

  //!
  //! @brief Append points from vectors of values and uncertainties.
  //! @param x x values @param xError uncertainties on x (or empty) @param y y values
  //! @param yStatError statistical uncertainties on y (or empty) @param ySysError systematic uncertainties on y (or empty)
  //! @throws MathException if the vector sizes differ
  //!
  virtual void addPoints(const std::vector<double> & x,
                         const std::vector<double> & xError,
                         const std::vector<double> & y,
                         const std::vector<double> & yStatError,
                         const std::vector<double> & ySysError);

  //! @}

  //! @name Extrema
  //! Default implementations scan all points; they throw MathException if the container is empty.
  //! When several points share an extremum, the first one is used.
  //! @{

  //! @brief Smallest x value. @return minimum of x
  virtual float xMinimum()    const;
  //! @brief Largest x value. @return maximum of x
  virtual float xMaximum()    const;
  //! @brief x value of the point with the largest y. @return x at maximum y
  virtual float xAtYMaximum() const;
  //! @brief x value of the point with the smallest y. @return x at minimum y
  virtual float xAtYMinimum() const;
  //! @brief Smallest y value. @return minimum of y
  virtual float yMinimum()    const;
  //! @brief Largest y value. @return maximum of y
  virtual float yMaximum()    const;
  //! @brief y value of the point with the largest x. @return y at maximum x
  virtual float yAtXMaximum() const;
  //! @brief y value of the point with the smallest x. @return y at minimum x
  virtual float yAtXMinimum() const;
  //! @brief Extent of the x values. @return xMaximum()-xMinimum()
  float xRange() const { return xMaximum() - xMinimum(); }
  //! @brief Extent of the y values. @return yMaximum()-yMinimum()
  float yRange() const { return yMaximum() - yMinimum(); }

  //! @}

  //! @name Extrema of points of any rank
  //! Read through coordinatesAt() and valueAt(), so they apply to every rank and every derived class.
  //! They throw MathException if the container is empty. Derived classes may declare functions with the same
  //! names; those are then used on objects of that class (Histogram uses minimumInRange() and maximumInRange()
  //! for searches over a range of bins, so the functions below apply to histograms as well).
  //! @{

  //!
  //! @brief Smallest value of one independent variable over all points.
  //! @param index independent variable, 0 to rank()-1 (0 is x)
  //! @return minimum of the variable
  //! @throws MathException if the container is empty or index>=rank()
  //!
  double minimum(unsigned int index) const;
  //!
  //! @brief Largest value of one independent variable over all points.
  //! @param index independent variable, 0 to rank()-1 (0 is x)
  //! @return maximum of the variable
  //! @throws MathException if the container is empty or index>=rank()
  //!
  double maximum(unsigned int index) const;
  //! @brief Smallest dependent value over all points. @return minimum value @throws MathException if the container is empty
  double minimumValue() const;
  //! @brief Largest dependent value over all points. @return maximum value @throws MathException if the container is empty
  double maximumValue() const;

  //! @}

  //! @brief Print all points to std::cout, one per line.
  virtual void print() const override;

  //! @name Table of the values (as the data pane of DatumGui shows them)
  //! @{

  //! @brief Kind and size, e.g. "Histogram (1D), 100 bins" or "Data points (2D), 25 points". @return text
  String description() const;
  //! @brief Names of the columns: "#", each coordinate and "± " its name (the dimension titles of a histogram, or
  //! x, y, z, x3, ...), the value (the title of a histogram, or y, or value), "± stat.", "± syst.". @return names
  std::vector<String> columnNames() const;
  //! @brief Which columns of columnNames() hold something: uncertainty columns that are zero for every point are
  //! not. @return one flag per column
  std::vector<bool> columnsShown() const;
  //! @brief Cells of point k after its index ("%.6g"): coordinates and their uncertainties, value, statistical and
  //! systematic uncertainties. @param k index @return cells
  std::vector<String> rowValues(unsigned int k) const;
  //! @brief Names of the columns of the fits: "f(x) <label>" ("f(x,y) <label>" for rank 2) and, if asked,
  //! "pull <label>" for each fit.
  //! @param pulls with the pulls @return names
  std::vector<String> fitColumnNames(bool pulls=true) const;
  //!
  //! @brief Cells of the fits for point k: f(x) of each fit, and its pull (pullAt()) if asked; empty where they cannot
  //! be computed. @param k index @param pulls with the pulls @param outside if given, set for each cell to whether the
  //! point is outside the range of the fit @return cells
  //!
  std::vector<String> fitValues(unsigned int k, bool pulls=true, std::vector<bool> * outside=nullptr) const;
  //! @brief Pull of point k to a fit: (value - f(x))/(statistical uncertainty). @param k index @param fit fit
  //! @return pull, or NaN if the point has no statistical uncertainty or f(x) cannot be computed
  double pullAt(unsigned int k, const FitRecord & fit) const;
  //!
  //! @brief The table as tab-separated text, header row first (what Copy of the data pane gives): the columns shown
  //! (columnsShown()), then those of the fits. @param fits with the columns of the fits @param pulls with the pulls
  //! @return text
  //!
  String table(bool fits=true, bool pulls=true) const;
  //! @}

  //! @name Common systematic uncertainty
  //! A systematic uncertainty shared by all the points (or bins), e.g. that of a luminosity or of a normalization,
  //! relative to the value: 0.02 is 2% of the value of each point. It has an upper and a lower side (equal when
  //! symmetric), is kept apart from the uncertainties of the points, and is saved with them in Datum files.
  //! @{

  //! @brief Set a symmetric common systematic uncertainty. @param relative fraction of the value (>= 0)
  //! @throws MathException if relative<0
  void setCommonSyst(double relative) { setCommonSyst(relative,relative); }
  //! @brief Set the upper and lower common systematic uncertainties. @param plus upper side, fraction of the value (>= 0)
  //! @param minus lower side, fraction of the value (>= 0) @throws MathException if one is negative
  void setCommonSyst(double plus, double minus);
  //! @brief Upper common systematic uncertainty. @return fraction of the value
  double getCommonSystPlus() const  { return _commonSystPlus; }
  //! @brief Lower common systematic uncertainty. @return fraction of the value
  double getCommonSystMinus() const { return _commonSystMinus; }
  //! @brief Common systematic uncertainty: the larger side. @return fraction of the value
  double getCommonSyst() const { return _commonSystPlus>=_commonSystMinus ? _commonSystPlus : _commonSystMinus; }
  //! @brief Whether a common systematic uncertainty is set (either side non-zero). @return true if set
  bool hasCommonSyst() const { return _commonSystPlus!=0.0 || _commonSystMinus!=0.0; }
  //! @brief Whether its upper and lower sides differ. @return true if asymmetric
  bool hasAsymmetricCommonSyst() const { return _commonSystPlus!=_commonSystMinus; }
  //! @brief Remove the common systematic uncertainty.
  void clearCommonSyst() { _commonSystPlus = 0.0; _commonSystMinus = 0.0; }
  //!
  //! @brief Common systematic uncertainty of the result of a sum, difference, product or ratio of two sets of points:
  //! the relative uncertainties of the operands added in quadrature, each side separately (the operands independent).
  //! For a difference or a ratio, pass swapSecond=true: the second operand enters with a negative sign, its sides swap.
  //! @param a first operand @param b second operand @param swapSecond whether the sides of b swap
  //!
  void setCommonSystOf(const DataPoints & a, const DataPoints & b, bool swapSecond=false);
  //! @}

  //! @name Fits attached (see FitRecord, Fitter::record())
  //! @{

  //! @brief Attach a copy of a fit. @param record record
  void addFit(const FitRecord & record) { _fits.add(record); }
  //! @brief Attach a fit (shared). @param record record
  void addFit(const std::shared_ptr<const FitRecord> & record) { _fits.add(record); }
  //! @brief Number of fits attached. @return number
  unsigned int nFits() const { return _fits.size(); }
  //! @brief Fit k. @param k index @return record @throws MathException if k is out of range
  const FitRecord & fit(unsigned int k) const { return _fits.at(k); }
  //! @brief Fit k, shared. @param k index @return record @throws MathException if k is out of range
  std::shared_ptr<const FitRecord> fitPointer(unsigned int k) const { return _fits.pointer(k); }
  //! @brief Detach fit k. @param k index @throws MathException if k is out of range
  void removeFit(unsigned int k) { _fits.remove(k); }
  //! @brief Detach all the fits.
  void clearFits() { _fits.clear(); }
  //! @}

  protected:

  FitList _fits;   //!<! fits attached (not streamed by ROOT; shared by copies)
  double _commonSystPlus  = 0.0;   //!< upper common systematic uncertainty, fraction of the value
  double _commonSystMinus = 0.0;   //!< lower common systematic uncertainty, fraction of the value

  //!
  //! @brief Write the common systematic uncertainty in a binary payload: a flag byte (0 none, 1 symmetric, 2
  //! asymmetric) followed by the side(s) as float64. Used by the records of the derived classes.
  //! @param w writer
  //!
  void encodeCommonSyst(BinaryWriter & w) const;
  //! @brief Read the common systematic uncertainty written by encodeCommonSyst(). @param r reader
  //! @throws Exception if the data is invalid
  void decodeCommonSyst(BinaryReader & r);

  //!
  //! @brief Index of the point with the extreme value of x (useY=false) or y (useY=true).
  //! @param useY true to scan y, false to scan x @param findMaximum true for the maximum, false for the minimum
  //! @return index of the extremum point
  //! @throws MathException if the container is empty
  //!
  unsigned int extremumIndex(bool useY, bool findMaximum) const;

  //!
  //! @brief Extremum of one independent variable (index<rank()) or of the dependent value (index==rank()).
  //! @param index variable index, or rank() for the dependent value @param findMaximum true for the maximum, false for the minimum
  //! @param source name of the calling function, reported in exceptions
  //! @return the extremum
  //! @throws MathException if the container is empty or index>rank()
  //!
  double extremum(unsigned int index, bool findMaximum, const char * source) const;

  };

  } // namespace MATH
} // namespace Datum

#endif
