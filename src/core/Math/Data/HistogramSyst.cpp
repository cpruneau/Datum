/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
#include "HistogramSyst.hpp"
#include "MathExceptions.hpp"
#include "BinaryIO.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>

namespace Datum
{
namespace MATH
{

namespace
{
//! Upper systematic uncertainty of bin k of any histogram (0 for a plain Histogram).
double systPlusOf(const Histogram & h, unsigned int k)  { return h.hasSystUncertainties() ? h.binSystPlusAtGlobal(k)  : 0.0; }
//! Lower systematic uncertainty of bin k of any histogram (0 for a plain Histogram).
double systMinusOf(const Histogram & h, unsigned int k) { return h.hasSystUncertainties() ? h.binSystMinusAtGlobal(k) : 0.0; }

//! Contribution of an operand with partial derivative d to the upper (upper=true) or lower side of the result: a
//! negative derivative turns the lower side of the operand into the upper side of the result, and conversely.
double side(double d, double plus, double minus, bool upper)
{
  if (d>=0.0) return d*(upper ? plus : minus);
  return -d*(upper ? minus : plus);
}

//! Quadrature sum.
double quadrature(double a, double b) { return std::sqrt(a*a+b*b); }
}

//////////////////////////////////////////////////////////////////////////
// Construction
//////////////////////////////////////////////////////////////////////////

HistogramSyst::HistogramSyst()
:
Histogram(), _systPlus(), _systMinus()
{
  syncSyst();
}

HistogramSyst::HistogramSyst(const Partition & partition)
:
Histogram(partition), _systPlus(), _systMinus()
{
  syncSyst();
}

HistogramSyst::HistogramSyst(const String & name, const String & title, unsigned int n, double minimum, double maximum)
:
Histogram(name,title,n,minimum,maximum), _systPlus(), _systMinus()
{
  syncSyst();
}

HistogramSyst::HistogramSyst(const String & name, const String & title,
                             unsigned int n0, double minimum_0, double maximum_0,
                             unsigned int n1, double minimum_1, double maximum_1)
:
Histogram(name,title,n0,minimum_0,maximum_0,n1,minimum_1,maximum_1), _systPlus(), _systMinus()
{
  syncSyst();
}

HistogramSyst::HistogramSyst(const String & name, const String & title,
                             unsigned int n0, double minimum_0, double maximum_0,
                             unsigned int n1, double minimum_1, double maximum_1,
                             unsigned int n2, double minimum_2, double maximum_2)
:
Histogram(name,title,n0,minimum_0,maximum_0,n1,minimum_1,maximum_1,n2,minimum_2,maximum_2), _systPlus(), _systMinus()
{
  syncSyst();
}

HistogramSyst::HistogramSyst(const Histogram & source)
:
Histogram(source), _systPlus(), _systMinus()
{
  copySystFrom(source);
}

HistogramSyst::HistogramSyst(const HistogramSyst & source)
:
Histogram(source), _systPlus(source._systPlus), _systMinus(source._systMinus)
{
  syncSyst();
}

HistogramSyst & HistogramSyst::operator=(const HistogramSyst & rhs)
{
  if (this!=&rhs)
    {
    Histogram::operator=(rhs);
    _systPlus  = rhs._systPlus;
    _systMinus = rhs._systMinus;
    syncSyst();
    }
  return *this;
}

void HistogramSyst::syncSyst() const
{
  bool isDouble = precision()==kDouble;
  _systPlus.setDouble(isDouble);
  _systMinus.setDouble(isDouble);
  std::size_t n = nBinsTotal();
  if (_systPlus.size()!=n)  _systPlus.assign(n,0.0);
  if (_systMinus.size()!=n) _systMinus.assign(n,0.0);
}

void HistogramSyst::copySystFrom(const Histogram & h)
{
  syncSyst();
  const HistogramSyst * s = dynamic_cast<const HistogramSyst *>(&h);
  if (s==this) return;
  if (s && s->nBinsTotal()==nBinsTotal())
    {
    s->syncSyst();
    for (unsigned int k=0; k<nBinsTotal(); k++)
      {
      _systPlus[k]  = s->_systPlus[k];
      _systMinus[k] = s->_systMinus[k];
      }
    }
  else
    {
    _systPlus.fill(0.0);
    _systMinus.fill(0.0);
    }
}

void HistogramSyst::assignFrom(const HistogramSyst & r)
{
  assignContent(r);   // binning, bins, common systematic uncertainty
  r.syncSyst();
  _systPlus  = r._systPlus;
  _systMinus = r._systMinus;
  syncSyst();
}

//////////////////////////////////////////////////////////////////////////
// Systematic uncertainties of the bins
//////////////////////////////////////////////////////////////////////////

double HistogramSyst::binSystPlusAtGlobal(unsigned int k) const
{
  if (k>=nBinsTotal()) throw IndexOutOfBoundException(k,nBinsTotal(),"Out of bound index",__FUNCTION__);
  syncSyst();
  return _systPlus[k];
}

double HistogramSyst::binSystMinusAtGlobal(unsigned int k) const
{
  if (k>=nBinsTotal()) throw IndexOutOfBoundException(k,nBinsTotal(),"Out of bound index",__FUNCTION__);
  syncSyst();
  return _systMinus[k];
}

void HistogramSyst::setBinSystAtGlobal(unsigned int k, double plus, double minus)
{
  if (k>=nBinsTotal()) throw IndexOutOfBoundException(k,nBinsTotal(),"Out of bound index",__FUNCTION__);
  if (!(plus>=0.0))  throw MathException("Systematic uncertainty: plus<0",__FUNCTION__);
  if (!(minus>=0.0)) throw MathException("Systematic uncertainty: minus<0",__FUNCTION__);
  syncSyst();
  _systPlus[k]  = plus;
  _systMinus[k] = minus;
}

bool HistogramSyst::hasAsymmetricSyst() const
{
  syncSyst();
  for (unsigned int k=0; k<nBinsTotal(); k++) if (_systPlus[k]!=_systMinus[k]) return true;
  return false;
}

void HistogramSyst::clearSyst()
{
  syncSyst();
  _systPlus.fill(0.0);
  _systMinus.fill(0.0);
}

void HistogramSyst::setPrecision(Precision precision)
{
  Histogram::setPrecision(precision);
  syncSyst();
}

//////////////////////////////////////////////////////////////////////////
// Data points
//////////////////////////////////////////////////////////////////////////

double HistogramSyst::sysErrorAt(unsigned int index) const
{
  return std::max(binSystPlusAtGlobal(index),binSystMinusAtGlobal(index));
}

double HistogramSyst::sysErrorPlusAt(unsigned int index) const  { return binSystPlusAtGlobal(index); }
double HistogramSyst::sysErrorMinusAt(unsigned int index) const { return binSystMinusAtGlobal(index); }

DataPoint2D HistogramSyst::point(unsigned int index) const
{
  DataPoint2D p = Histogram::point(index);
  p.setSyst(binSystPlusAtGlobal(index),binSystMinusAtGlobal(index));
  return p;
}

//////////////////////////////////////////////////////////////////////////
// Arithmetic
//////////////////////////////////////////////////////////////////////////

void HistogramSyst::scale(double coefficient)
{
  Histogram::scale(coefficient);
  syncSyst();
  double a = std::fabs(coefficient);
  for (unsigned int k=0; k<nBinsTotal(); k++)
    {
    double plus = a*_systPlus[k], minus = a*_systMinus[k];
    if (coefficient<0) std::swap(plus,minus);
    _systPlus[k]  = plus;
    _systMinus[k] = minus;
    }
}

void HistogramSyst::add(double value)
{
  Histogram::add(value);   // the systematic uncertainties of the values are unchanged
}

void HistogramSyst::add(const Histogram & h, double coefficient)
{
  checkCompatible(h,__FUNCTION__);
  syncSyst();
  unsigned int n = nBinsTotal();
  std::vector<double> plus(n), minus(n);
  for (unsigned int k=0; k<n; k++)
    {
    double p2 = systPlusOf(h,k), m2 = systMinusOf(h,k);
    plus[k]  = quadrature(_systPlus[k], side(coefficient,p2,m2,true));
    minus[k] = quadrature(_systMinus[k],side(coefficient,p2,m2,false));
    }
  Histogram::add(h,coefficient);
  for (unsigned int k=0; k<n; k++) { _systPlus[k] = plus[k]; _systMinus[k] = minus[k]; }
}

void HistogramSyst::multiplyBy(const Histogram & rhs, double coefficient)
{
  checkCompatible(rhs,__FUNCTION__);
  syncSyst();
  unsigned int n = nBinsTotal();
  std::vector<double> plus(n,0.0), minus(n,0.0);
  for (unsigned int k=0; k<n; k++)
    {
    if (_entries[k]==0 || rhs.binEntriesAtGlobal(k)==0) continue;   // set empty by the product
    double v1 = value(k), v2 = coefficient*rhs.binValueAtGlobal(k);
    double d1 = v2, d2 = coefficient*v1;                             // d(v1*c*w)/dv1, d(v1*c*w)/dw
    double p2 = systPlusOf(rhs,k), m2 = systMinusOf(rhs,k);
    plus[k]  = quadrature(side(d1,_systPlus[k],_systMinus[k],true), side(d2,p2,m2,true));
    minus[k] = quadrature(side(d1,_systPlus[k],_systMinus[k],false),side(d2,p2,m2,false));
    }
  Histogram::multiplyBy(rhs,coefficient);
  for (unsigned int k=0; k<n; k++) { _systPlus[k] = plus[k]; _systMinus[k] = minus[k]; }
}

void HistogramSyst::divideBy(const Histogram & rhs, double coefficient)
{
  checkCompatible(rhs,__FUNCTION__);
  syncSyst();
  unsigned int n = nBinsTotal();
  std::vector<double> plus(n,0.0), minus(n,0.0);
  for (unsigned int k=0; k<n; k++)
    {
    double w  = rhs.binValueAtGlobal(k);
    double v2 = coefficient*w;
    if (_entries[k]==0 || rhs.binEntriesAtGlobal(k)==0 || v2==0.0) continue;   // set empty by the ratio
    double v1 = value(k);
    double d1 = 1.0/v2;                    // d(v1/(c*w))/dv1
    double d2 = -v1/(coefficient*w*w);     // d(v1/(c*w))/dw
    double p2 = systPlusOf(rhs,k), m2 = systMinusOf(rhs,k);
    plus[k]  = quadrature(side(d1,_systPlus[k],_systMinus[k],true), side(d2,p2,m2,true));
    minus[k] = quadrature(side(d1,_systPlus[k],_systMinus[k],false),side(d2,p2,m2,false));
    }
  Histogram::divideBy(rhs,coefficient);
  for (unsigned int k=0; k<n; k++) { _systPlus[k] = plus[k]; _systMinus[k] = minus[k]; }
}

void HistogramSyst::inverse()
{
  syncSyst();
  unsigned int n = nBinsTotal();
  std::vector<double> plus(n,0.0), minus(n,0.0);
  for (unsigned int k=0; k<n; k++)
    {
    double v = value(k);
    if (_entries[k]==0 || v==0.0) continue;   // set empty by inverse()
    double d = -1.0/(v*v);
    plus[k]  = side(d,_systPlus[k],_systMinus[k],true);
    minus[k] = side(d,_systPlus[k],_systMinus[k],false);
    }
  Histogram::inverse();
  for (unsigned int k=0; k<n; k++) { _systPlus[k] = plus[k]; _systMinus[k] = minus[k]; }
}

void HistogramSyst::setAsSumOf(const Histogram & h1, const Histogram & h2, double coefficient1, double coefficient2)
{
  if (!h1.compatibleWith(h2))
    throw Exception("Operation invalid on Histogram of different dimensions: h1 and h2",__FUNCTION__);
  HistogramSyst result(h1);
  result.scale(coefficient1);
  result.add(h2,coefficient2);
  assignFrom(result);
}

void HistogramSyst::setAsSumOf(const std::vector<const Histogram *> & histograms, const std::vector<double> & coefficients)
{
  if (histograms.empty()) throw Exception("No histograms given",__FUNCTION__);
  if (histograms.size()!=coefficients.size()) throw Exception("histograms and coefficients sizes differ",__FUNCTION__);
  for (auto h : histograms) if (!h) throw NullPointerException("histograms",__FUNCTION__);
  HistogramSyst result(*histograms[0]);
  result.scale(coefficients[0]);
  for (unsigned int i=1; i<histograms.size(); i++) result.add(*histograms[i],coefficients[i]);
  assignFrom(result);
}

void HistogramSyst::setAsProductOf(const Histogram & h1, const Histogram & h2, double coefficient1, double coefficient2)
{
  if (!h1.compatibleWith(h2))
    throw Exception("Operation invalid on Histogram of different dimensions: h1 and h2",__FUNCTION__);
  HistogramSyst result(h1);
  result.scale(coefficient1);
  result.multiplyBy(h2,coefficient2);
  assignFrom(result);
}

void HistogramSyst::setAsProductOf(const std::vector<const Histogram *> & histograms, const std::vector<double> & coefficients)
{
  if (histograms.empty()) throw Exception("No histograms given",__FUNCTION__);
  if (histograms.size()!=coefficients.size()) throw Exception("histograms and coefficients sizes differ",__FUNCTION__);
  for (auto h : histograms) if (!h) throw NullPointerException("histograms",__FUNCTION__);
  HistogramSyst result(*histograms[0]);
  result.scale(coefficients[0]);
  for (unsigned int i=1; i<histograms.size(); i++) result.multiplyBy(*histograms[i],coefficients[i]);
  assignFrom(result);
}

void HistogramSyst::setAsRatioOf(const Histogram & h1, const Histogram & h2, double coefficient1, double coefficient2)
{
  if (!h1.compatibleWith(h2))
    throw Exception("Operation invalid on Histogram of different dimensions: h1 and h2",__FUNCTION__);
  HistogramSyst result(h1);
  result.scale(coefficient1);
  result.divideBy(h2,coefficient2);
  assignFrom(result);
}

HistogramSyst & HistogramSyst::operator/=(double value)
{
  if (value==0.0) throw Exception("Attempting division by zero",__FUNCTION__);
  scale(1.0/value);
  return *this;
}

//////////////////////////////////////////////////////////////////////////
// Rebinning
//////////////////////////////////////////////////////////////////////////

HistogramSyst HistogramSyst::rebin(unsigned int n) const
{
  return rebin(std::vector<unsigned int>(rank(),n));
}

HistogramSyst HistogramSyst::rebin(const std::vector<unsigned int> & factors) const
{
  HistogramSyst result(Histogram::rebin(factors));   // checks the factors; values and statistical uncertainties
  syncSyst();
  Partition partition = rebinnedPartition(factors);
  unsigned int nTarget = partition.nBinsTotal();
  std::vector<double> plus(nTarget,0.0), minus(nTarget,0.0);
  for (unsigned int k=0; k<nBinsTotal(); k++)
    {
    unsigned int t = rebinnedBin(k,factors,partition);
    plus[t]  += _systPlus[k];    // correlated from bin to bin: added linearly
    minus[t] += _systMinus[k];
    }
  // the last bin of an axis where the factor does not divide the number of bins covers r < n bins: scaled by n/r,
  // as its value (see Histogram::rebin())
  for (unsigned int t=0; t<nTarget; t++)
    {
    double scale = 1.0;
    unsigned int rest = t;
    for (unsigned int a=0; a<rank(); a++)
      {
      unsigned int nNew = partition.nBins(a), i = rest%nNew;
      rest /= nNew;
      unsigned int r = nBins(a)%factors[a];
      if (r!=0 && i==nNew-1) scale *= double(factors[a])/double(r);
      }
    result.setBinSystAtGlobal(t,scale*plus[t],scale*minus[t]);
    }
  return result;
}

Histogram * HistogramSyst::rebinned(const std::vector<unsigned int> & factors) const
{
  return new HistogramSyst(rebin(factors));
}

//////////////////////////////////////////////////////////////////////////
// Reset, clear, print
//////////////////////////////////////////////////////////////////////////

void HistogramSyst::reset()
{
  Histogram::reset();
  syncSyst();
  _systPlus.fill(0.0);
  _systMinus.fill(0.0);
}

void HistogramSyst::clear()
{
  Histogram::clear();
  _systPlus.clear();
  _systMinus.clear();
  syncSyst();
}

void HistogramSyst::print() const
{
  Histogram::print();
  syncSyst();
  std::cout << " Bin Index    Syst+    Syst-" << std::endl;
  for (unsigned int k=0; k<nBinsTotal(); k++)
    std::cout << k << "   " << _systPlus[k] << "  " << _systMinus[k] << std::endl;
  if (hasCommonSyst())
    std::cout << " Common systematic uncertainty: +" << getCommonSystPlus() << " -" << getCommonSystMinus()
              << " (relative)" << std::endl;
}

//////////////////////////////////////////////////////////////////////////
// Binary I/O
//////////////////////////////////////////////////////////////////////////

void HistogramSyst::encodeBinary(BinaryWriter & w) const
{
  Histogram::encodeBinary(w);
  syncSyst();
  unsigned int n = nBinsTotal();
  bool isDouble = precision()==kDouble;
  bool asymmetric = hasAsymmetricSyst();
  unsigned int nNonZero = 0;
  for (unsigned int k=0; k<n; k++) if (_systPlus[k]!=0.0 || _systMinus[k]!=0.0) nNonZero++;
  w.writeUInt8(nNonZero==0 ? 0 : (asymmetric ? 2 : 1));
  if (nNonZero==0) return;
  w.writeVarUInt(nNonZero);
  unsigned int previous = 0;
  bool first = true;
  for (unsigned int k=0; k<n; k++)
    {
    if (_systPlus[k]==0.0 && _systMinus[k]==0.0) continue;
    w.writeVarUInt(first ? k : k-previous-1);
    first = false;
    previous = k;
    if (isDouble) w.writeFloat64(_systPlus[k]); else w.writeFloat32(float(_systPlus[k]));
    if (asymmetric) { if (isDouble) w.writeFloat64(_systMinus[k]); else w.writeFloat32(float(_systMinus[k])); }
    }
}

void HistogramSyst::decodeBinary(BinaryReader & r, const String & histogramName, unsigned int /*version*/)
{
  Histogram::decodeBinary(r,histogramName,5);   // format version 1: the payload of a Histogram record of version 5
  _systPlus.clear();
  _systMinus.clear();
  syncSyst();
  uint8_t flag = r.readUInt8();
  if (flag>2) throw Exception("Invalid systematic uncertainty flag","HistogramSyst::decodeBinary()");
  if (flag==0) return;
  unsigned int n = nBinsTotal();
  bool isDouble = precision()==kDouble;
  uint64_t nNonZero = r.readVarUInt();
  if (nNonZero>n) throw Exception("More bins with systematic uncertainties than bins","HistogramSyst::decodeBinary()");
  uint64_t k = 0;
  for (uint64_t i=0; i<nNonZero; i++)
    {
    uint64_t gap = r.readVarUInt();
    k = (i==0) ? gap : k+gap+1;
    if (k>=n) throw Exception("Bin index out of range","HistogramSyst::decodeBinary()");
    double plus  = isDouble ? r.readFloat64() : double(r.readFloat32());
    double minus = flag==2 ? (isDouble ? r.readFloat64() : double(r.readFloat32())) : plus;
    if (!(plus>=0.0) || !(minus>=0.0)) throw Exception("Invalid systematic uncertainty","HistogramSyst::decodeBinary()");
    _systPlus[k]  = plus;
    _systMinus[k] = minus;
    }
}

void HistogramSyst::writeBinary(std::ostream & out) const                                    { writeBinaryObject(out,*this); }
bool HistogramSyst::readBinary(std::istream & in)                                            { return readBinaryObject(in,*this); }
void HistogramSyst::saveBinary(const String & fileName, bool append) const                   { saveBinaryObject(fileName,*this,append); }
void HistogramSyst::loadBinary(const String & fileName, const String & histogramName)        { loadBinaryObject(fileName,*this,histogramName); }
void HistogramSyst::saveBinary(const String & fileName, const std::vector<HistogramSyst> & hs){ saveBinaryObjects(fileName,hs); }
std::vector<HistogramSyst> HistogramSyst::loadAllBinary(const String & fileName)             { return loadAllBinaryObjects<HistogramSyst>(fileName); }

//////////////////////////////////////////////////////////////////////////
// Free operators
//////////////////////////////////////////////////////////////////////////

namespace
{
HistogramSyst sum(const Histogram & left, const Histogram & right, double c)
{
  HistogramSyst result(left);
  result.add(right,c);
  return result;
}
HistogramSyst product(const Histogram & left, const Histogram & right)
{
  HistogramSyst result(left);
  result.multiplyBy(right,1.0);
  return result;
}
HistogramSyst ratio(const Histogram & left, const Histogram & right)
{
  HistogramSyst result(left);
  result.divideBy(right,1.0);
  return result;
}
}

HistogramSyst operator+(const HistogramSyst & left, const HistogramSyst & right) { return sum(left,right,1.0); }
HistogramSyst operator+(const HistogramSyst & left, const Histogram & right)     { return sum(left,right,1.0); }
HistogramSyst operator+(const Histogram & left, const HistogramSyst & right)     { return sum(left,right,1.0); }
HistogramSyst operator-(const HistogramSyst & left, const HistogramSyst & right) { return sum(left,right,-1.0); }
HistogramSyst operator-(const HistogramSyst & left, const Histogram & right)     { return sum(left,right,-1.0); }
HistogramSyst operator-(const Histogram & left, const HistogramSyst & right)     { return sum(left,right,-1.0); }
HistogramSyst operator*(const HistogramSyst & left, const HistogramSyst & right) { return product(left,right); }
HistogramSyst operator*(const HistogramSyst & left, const Histogram & right)     { return product(left,right); }
HistogramSyst operator*(const Histogram & left, const HistogramSyst & right)     { return product(left,right); }
HistogramSyst operator/(const HistogramSyst & left, const HistogramSyst & right) { return ratio(left,right); }
HistogramSyst operator/(const HistogramSyst & left, const Histogram & right)     { return ratio(left,right); }
HistogramSyst operator/(const Histogram & left, const HistogramSyst & right)     { return ratio(left,right); }

HistogramSyst operator+(const HistogramSyst & left, double right) { HistogramSyst r(left);  r.add(right);  return r; }
HistogramSyst operator+(double left, const HistogramSyst & right) { HistogramSyst r(right); r.add(left);   return r; }
HistogramSyst operator-(const HistogramSyst & left, double right) { HistogramSyst r(left);  r.add(-right); return r; }
HistogramSyst operator-(double left, const HistogramSyst & right) { HistogramSyst r(right); r.scale(-1.0); r.add(left); return r; }
HistogramSyst operator*(const HistogramSyst & left, double right) { HistogramSyst r(left);  r.scale(right); return r; }
HistogramSyst operator*(double left, const HistogramSyst & right) { HistogramSyst r(right); r.scale(left);  return r; }
HistogramSyst operator/(const HistogramSyst & left, double right)
{
  if (right==0.0) throw Exception("Attempting division by zero",__FUNCTION__);
  HistogramSyst r(left);
  r.scale(1.0/right);
  return r;
}
HistogramSyst operator/(double left, const HistogramSyst & right) { HistogramSyst r(right); r.inverse(); r.scale(left); return r; }

} // namespace MATH
} // namespace Datum
