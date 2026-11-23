/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
#ifndef DATUM_MATH_HistogramSyst
#define DATUM_MATH_HistogramSyst
#include "Histogram.hpp"

namespace Datum
{
namespace MATH
{

//!
//! @brief Histogram with an upper and a lower systematic uncertainty on the value of each bin.
//!
//! A HistogramSyst is a Histogram (same binning, filling, statistical uncertainties and common systematic uncertainty,
//! see DataPoints::setCommonSyst()) that also stores, for each bin, the systematic uncertainties of the bin value:
//! binSystPlusAtGlobal() and binSystMinusAtGlobal() (equal when symmetric), set with setBinSystAtGlobal() or the
//! setBinSystAt() functions of Histogram, which throw for a plain Histogram. They are stored in the precision of the
//! bin values (float or double, see setPrecision()).
//!
//! The systematic uncertainties are those of the bin values, not per entry: filling a bin does not change them, and the
//! bin setters of Histogram (setBinValueAt(), setBinEntriesAt(), ...) leave them as they are.
//!
//! <b>Arithmetic.</b> Statistical uncertainties are propagated as by Histogram. The systematic uncertainties of a result
//! are propagated to first order, each side separately, the operands assumed independent (as DataPoint does):
//!  - scale(c): both sides multiplied by |c|, swapped if c<0;
//!  - add(h, c), sums and differences: in quadrature; an operand entering with a negative coefficient contributes its
//!    lower side to the upper side of the result, and conversely;
//!  - products, ratios, inverse: d1*s1 and d2*s2 in quadrature (d = partial derivatives); the divisor of a ratio and the
//!    argument of inverse() enter with a negative derivative, their sides swap;
//!  - an operand that is a plain Histogram has no systematic uncertainty per bin;
//!  - bins set empty by the operation (products and ratios with an empty bin, division by zero) get none;
//!  - rebin(): the systematic uncertainties of the bins merged are added linearly (correlated from bin to bin), and
//!    the last, incomplete bin of an axis is scaled as its value.
//! The result of an operation on a HistogramSyst is a HistogramSyst (free operators below, rebin(), rebinned()).
//!
//! <b>Data points.</b> As DataPoints, the systematic uncertainty of a bin is sysErrorPlusAt() / sysErrorMinusAt(), and
//! sysErrorAt() the larger side; point() gives them to the DataPoint2D of a rank 1 histogram.
//!
//! <b>Binary files.</b> "DTMY" records: the Histogram record (format version 5) followed by the systematic
//! uncertainties. The class is registered with Folder.
//!
//! Example:
//! @code
//!   Datum::MATH::HistogramSyst h("pt","p_{T} spectrum",10,0.0,5.0);
//!   h.setBinAt(3,1,120.0,11.0);          // bin 3: one entry, value 120 +- 11 (stat)
//!   h.setBinSystAt(3,6.0,4.0);           // +6 -4 (syst)
//!   h.setCommonSyst(0.03);               // 3% normalization uncertainty on every bin
//!   HistogramSyst r = h/h;               // systematics propagated bin by bin
//! @endcode
//!
class HistogramSyst : public Histogram
{
protected:

  mutable BinValues _systPlus;    //!< upper systematic uncertainty of each bin value (same precision as the values)
  mutable BinValues _systMinus;   //!< lower systematic uncertainty of each bin value

  //! @brief Size the systematic uncertainties to the number of bins (zeros) if the binning changed, and keep their
  //! precision that of the bin values.
  void syncSyst() const;
  //! @brief Copy the systematic uncertainties of h if it is a HistogramSyst with the same number of bins, zeros
  //! otherwise. @param h histogram
  void copySystFrom(const Histogram & h);
  //! @brief Set the content of this histogram to that of r: binning, bins, common and bin systematic uncertainties.
  //! @param r result of an operation
  void assignFrom(const HistogramSyst & r);

public:

  //! @name Construction and assignment
  //! @{

  //! @brief Default constructor: rank 1 histogram named "H" with a single bin spanning [0,1).
  HistogramSyst();
  //! @brief Empty histogram with the binning of a partition. @param partition binning
  HistogramSyst(const Partition & partition);
  //! @brief Empty rank 1 histogram. @param name name @param title title @param n number of bins
  //! @param minimum lower edge @param maximum upper edge
  HistogramSyst(const String & name, const String & title, unsigned int n, double minimum, double maximum);
  //! @brief Empty rank 2 histogram. @param name name @param title title @param n0 bins along axis 0
  //! @param minimum_0 lower edge of axis 0 @param maximum_0 upper edge of axis 0 @param n1 bins along axis 1
  //! @param minimum_1 lower edge of axis 1 @param maximum_1 upper edge of axis 1
  HistogramSyst(const String & name, const String & title,
                unsigned int n0, double minimum_0, double maximum_0,
                unsigned int n1, double minimum_1, double maximum_1);
  //! @brief Empty rank 3 histogram. @param name name @param title title @param n0 bins along axis 0
  //! @param minimum_0 lower edge of axis 0 @param maximum_0 upper edge of axis 0 @param n1 bins along axis 1
  //! @param minimum_1 lower edge of axis 1 @param maximum_1 upper edge of axis 1 @param n2 bins along axis 2
  //! @param minimum_2 lower edge of axis 2 @param maximum_2 upper edge of axis 2
  HistogramSyst(const String & name, const String & title,
                unsigned int n0, double minimum_0, double maximum_0,
                unsigned int n1, double minimum_1, double maximum_1,
                unsigned int n2, double minimum_2, double maximum_2);
  //! @brief Copy of a histogram of any class: a HistogramSyst with its systematic uncertainties, a plain Histogram
  //! with none (zero). @param source histogram to copy
  explicit HistogramSyst(const Histogram & source);
  //! @brief Copy constructor. @param source histogram to copy
  HistogramSyst(const HistogramSyst & source);
  //! @brief Copy assignment. @param rhs histogram to copy @return this histogram
  HistogramSyst & operator=(const HistogramSyst & rhs);
  //! @brief Destructor.
  virtual ~HistogramSyst() {}
  //! @brief Polymorphic copy. @return new copy, owned by the caller
  HistogramSyst * clone() const override { return new HistogramSyst(*this); }

  //! @}

  //! @name Systematic uncertainties of the bins
  //! @{

  //! @brief Whether this histogram stores systematic uncertainties per bin. @return true
  bool hasSystUncertainties() const override { return true; }
  //! @brief Upper systematic uncertainty of the value of a bin. @param k global bin index @return uncertainty
  //! @throws IndexOutOfBoundException if k>=nBinsTotal()
  double binSystPlusAtGlobal(unsigned int k) const override;
  //! @brief Lower systematic uncertainty of the value of a bin. @param k global bin index @return uncertainty
  //! @throws IndexOutOfBoundException if k>=nBinsTotal()
  double binSystMinusAtGlobal(unsigned int k) const override;
  using Histogram::setBinSystAtGlobal;
  //! @brief Set the upper and lower systematic uncertainties of the value of a bin. @param k global bin index
  //! @param plus upper uncertainty (>= 0) @param minus lower uncertainty (>= 0)
  //! @throws IndexOutOfBoundException if k>=nBinsTotal(); MathException if an uncertainty is negative
  void setBinSystAtGlobal(unsigned int k, double plus, double minus) override;
  //! @brief Whether some bin has different upper and lower systematic uncertainties. @return true if asymmetric
  bool hasAsymmetricSyst() const;
  //! @brief Set the systematic uncertainties of every bin to zero.
  void clearSyst();
  //! @brief Set the storage precision of the values, their uncertainties and the systematic uncertainties.
  //! @param precision kFloat or kDouble
  void setPrecision(Precision precision) override;

  //! @}

  //! @name Data points
  //! @{

  //! @brief Systematic uncertainty of a bin, the larger side. @param index global bin index @return uncertainty
  double sysErrorAt(unsigned int index) const override;
  //! @brief Upper systematic uncertainty of a bin. @param index global bin index @return uncertainty
  double sysErrorPlusAt(unsigned int index) const override;
  //! @brief Lower systematic uncertainty of a bin. @param index global bin index @return uncertainty
  double sysErrorMinusAt(unsigned int index) const override;
  //! @brief Data point of a bin (rank 1): x = bin center, y = bin value, statistical and systematic uncertainties.
  //! @param index bin index @return data point @throws MathException if the rank is not 1
  DataPoint2D point(unsigned int index) const override;

  //! @}

  //! @name Arithmetic (systematic uncertainties propagated, see the class description)
  //! @{

  void scale(double coefficient) override;
  void add(double value) override;
  void add(const Histogram & h, double coefficient=1.0) override;
  void multiplyBy(const Histogram & rhs, double coefficient=1.0) override;
  void divideBy(const Histogram & rhs, double coefficient=1.0) override;
  void inverse() override;
  void setAsSumOf(const Histogram & h1, const Histogram & h2,
                  double coefficient1=1.0, double coefficient2=1.0) override;
  void setAsSumOf(const std::vector<const Histogram *> & histograms,
                  const std::vector<double> & coefficients) override;
  void setAsProductOf(const Histogram & h1, const Histogram & h2,
                      double coefficient1=1.0, double coefficient2=1.0) override;
  void setAsProductOf(const std::vector<const Histogram *> & histograms,
                      const std::vector<double> & coefficients) override;
  void setAsRatioOf(const Histogram & h1, const Histogram & h2,
                    double coefficient1=1.0, double coefficient2=1.0) override;
  //! @brief Add rhs bin by bin. @param rhs histogram with the same binning @return this histogram
  HistogramSyst & operator+=(const Histogram & rhs) { add(rhs,1.0); return *this; }
  //! @brief Subtract rhs bin by bin. @param rhs histogram with the same binning @return this histogram
  HistogramSyst & operator-=(const Histogram & rhs) { add(rhs,-1.0); return *this; }
  //! @brief Multiply by rhs bin by bin. @param rhs histogram with the same binning @return this histogram
  HistogramSyst & operator*=(const Histogram & rhs) { multiplyBy(rhs,1.0); return *this; }
  //! @brief Divide by rhs bin by bin. @param rhs histogram with the same binning @return this histogram
  HistogramSyst & operator/=(const Histogram & rhs) { divideBy(rhs,1.0); return *this; }
  //! @brief Add a constant to every bin value. @param value constant @return this histogram
  HistogramSyst & operator+=(double value) { add(value); return *this; }
  //! @brief Subtract a constant from every bin value. @param value constant @return this histogram
  HistogramSyst & operator-=(double value) { add(-value); return *this; }
  //! @brief Multiply every bin by a constant. @param value constant @return this histogram
  HistogramSyst & operator*=(double value) { scale(value); return *this; }
  //! @brief Divide every bin by a constant. @param value constant (non-zero) @return this histogram
  //! @throws Exception if value is zero
  HistogramSyst & operator/=(double value);

  //! @}

  //! @name Rebinning
  //! @{

  //! @brief A HistogramSyst with the bins merged n by n along every axis (see Histogram::rebin()); the systematic
  //! uncertainties of the bins merged are added linearly. @param n number of bins merged (>= 1) @return rebinned histogram
  HistogramSyst rebin(unsigned int n) const;
  //! @brief A HistogramSyst with the bins merged factors[k] by factors[k] along axis k. @param factors one per axis
  //! @return rebinned histogram
  HistogramSyst rebin(const std::vector<unsigned int> & factors) const;
  //! @brief Rebinned copy (a HistogramSyst), owned by the caller. @param factors one per axis @return new histogram
  Histogram * rebinned(const std::vector<unsigned int> & factors) const override;

  //! @}

  //! @brief Empty all bins: entries, values, uncertainties and systematic uncertainties (binning kept).
  void reset() override;
  //! @brief Remove the binning and the content.
  void clear() override;
  //! @brief Print the histogram and the systematic uncertainties of its bins.
  void print() const override;

  //! @name Binary files (independent of ROOT)
  //! A HistogramSyst is saved as one "DTMY" record: the payload of a Histogram record (format version 5), then a flag
  //! byte (0: no systematic uncertainty, 1: symmetric, 2: asymmetric), the number of bins with a non-zero uncertainty
  //! (varint) and, for each, the gap in global bin index since the previous one (varint), the upper uncertainty and, if
  //! asymmetric, the lower one (float32, or float64 in double precision).
  //! @{

  //! @brief Tag of HistogramSyst records in binary files. @return "DTMY"
  static const char * binaryTag() { return "DTMY"; }
  //! @brief Format version written (and highest version read). @return version
  static unsigned int binaryVersion() { return 1; }
  //! @brief Write the binary payload after the name. @param w writer
  void encodeBinary(BinaryWriter & w) const;
  //! @brief Rebuild this histogram from a binary payload. @param r reader @param name stored name @param version format
  //! version @throws Exception if the data is invalid
  void decodeBinary(BinaryReader & r, const String & name, unsigned int version);
  //! @brief Write as one binary record to a stream. @param out stream @throws FileException if writing fails
  void writeBinary(std::ostream & out) const;
  //! @brief Read the next HistogramSyst record of a stream. @param in stream @return false at the end of the stream
  bool readBinary(std::istream & in);
  //! @brief Save to a binary file. @param fileName file name @param append add at the end of the file instead of replacing it
  void saveBinary(const String & fileName, bool append=false) const;
  //! @brief Load the HistogramSyst of a binary file with a name (the first one if name is empty).
  //! @param fileName file name @param name name of the histogram
  void loadBinary(const String & fileName, const String & name="");
  //! @brief Save several histograms to one binary file (replacing it). @param fileName file name @param histograms histograms
  static void saveBinary(const String & fileName, const std::vector<HistogramSyst> & histograms);
  //! @brief Load all the HistogramSyst objects of a binary file, in order. @param fileName file name @return histograms
  static std::vector<HistogramSyst> loadAllBinary(const String & fileName);

  //! @}
};

//! @name Operators on HistogramSyst (systematic uncertainties propagated; the result is a HistogramSyst)
//! @{
HistogramSyst operator+(const HistogramSyst & left, const HistogramSyst & right);
HistogramSyst operator+(const HistogramSyst & left, const Histogram & right);
HistogramSyst operator+(const Histogram & left, const HistogramSyst & right);
HistogramSyst operator-(const HistogramSyst & left, const HistogramSyst & right);
HistogramSyst operator-(const HistogramSyst & left, const Histogram & right);
HistogramSyst operator-(const Histogram & left, const HistogramSyst & right);
HistogramSyst operator*(const HistogramSyst & left, const HistogramSyst & right);
HistogramSyst operator*(const HistogramSyst & left, const Histogram & right);
HistogramSyst operator*(const Histogram & left, const HistogramSyst & right);
HistogramSyst operator/(const HistogramSyst & left, const HistogramSyst & right);
HistogramSyst operator/(const HistogramSyst & left, const Histogram & right);
HistogramSyst operator/(const Histogram & left, const HistogramSyst & right);
HistogramSyst operator+(const HistogramSyst & left, double right);
HistogramSyst operator+(double left, const HistogramSyst & right);
HistogramSyst operator-(const HistogramSyst & left, double right);
HistogramSyst operator-(double left, const HistogramSyst & right);
HistogramSyst operator*(const HistogramSyst & left, double right);
HistogramSyst operator*(double left, const HistogramSyst & right);
HistogramSyst operator/(const HistogramSyst & left, double right);
HistogramSyst operator/(double left, const HistogramSyst & right);
//! @}

} // namespace MATH
} // namespace Datum

#endif
