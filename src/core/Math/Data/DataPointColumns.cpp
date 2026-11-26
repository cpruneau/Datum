/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
#include "DataPointColumns.hpp"
#include "DataPoint3D.hpp"
#include "DataPointND.hpp"
#include "MathExceptions.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>


namespace Datum
{
  namespace MATH
  {

  namespace
  {
  // Largest rank accepted when decoding binary data (sanity limit against corrupted input).
  constexpr uint64_t kMaximumDecodedRank = 1000000;

  // Check that column has n elements (or is empty when allowEmpty) and, for uncertainties, no negative element.
  void checkColumn(const std::vector<double> & column, std::size_t n, bool allowEmpty, bool isError,
                   const char * what, const char * source)
  {
  if (column.empty() && allowEmpty) return;
  if (column.size()!=n) throw MathException(String("Column size differs from the number of points: ")+what,source);
  if (isError)
    for (double e : column) if (e<0) throw MathException(String("Negative uncertainty in column ")+what,source);
  }

  std::vector<float> toFloat(const std::vector<double> & column, std::size_t n)
  {
  if (column.empty()) return std::vector<float>(n,0.0f);
  return std::vector<float>(column.begin(),column.end());
  }
  }

  //////////////////////////////////////////////////////////////////////////
  // Construction
  //////////////////////////////////////////////////////////////////////////

  DataPointColumns::DataPointColumns(unsigned int rank)
  :
  DataPoints(),
  _rank(1), _x(1), _xError(1), _value(), _stat(), _syst(), _systMinus()
  {
  setRank(rank);
  }

  DataPointColumns::DataPointColumns(const std::vector<double> & x, const std::vector<double> & y)
  :
  DataPointColumns(1)
  {
  setColumns({x},{},y,{},{});
  }

  DataPointColumns::DataPointColumns(const std::vector<double> & x,
                                     const std::vector<double> & xError,
                                     const std::vector<double> & y,
                                     const std::vector<double> & yStatError,
                                     const std::vector<double> & ySysError)
  :
  DataPointColumns(1)
  {
  if (xError.empty()) setColumns({x},{},y,yStatError,ySysError);
  else                setColumns({x},{xError},y,yStatError,ySysError);
  }

  DataPointColumns::DataPointColumns(const std::vector< std::vector<double> > & x,
                                     const std::vector< std::vector<double> > & xError,
                                     const std::vector<double> & value,
                                     const std::vector<double> & stat,
                                     const std::vector<double> & syst)
  :
  DataPointColumns(1)
  {
  setColumns(x,xError,value,stat,syst);
  }

  //////////////////////////////////////////////////////////////////////////
  // Checks
  //////////////////////////////////////////////////////////////////////////

  void DataPointColumns::checkIndex(unsigned int index, const char * source) const
  {
  if (index>=_value.size()) throw MathException("Point index out of range (index>=size())",source);
  }

  void DataPointColumns::checkVariable(unsigned int variable, const char * source) const
  {
  if (variable>=_rank) throw MathException("Independent variable index out of range (variable>=rank())",source);
  }

  //////////////////////////////////////////////////////////////////////////
  // DataPoints interface
  //////////////////////////////////////////////////////////////////////////

  DataPoint2D DataPointColumns::point(unsigned int index) const
  {
  checkIndex(index,__FUNCTION__);
  if (_rank!=1) throw MathException("point() requires rank 1; use createPoint()",__FUNCTION__);
  DataPoint2D p(_x[0][index],_xError[0][index],_value[index],_stat[index],_syst[index]);
  p.setSyst(_syst[index],_systMinus[index]);
  applyCovariance(p,index);
  return p;
  }

  void DataPointColumns::applyCovariance(DataPoint2D & p, unsigned int index) const
  {
  if (_xyCovariance.empty() || _xyCovariance[index]==0.0f) return;
  float xError = _xError[0][index], stat = _stat[index];
  try
    {
    p.setErrorMatrix(double(xError)*xError, _xyCovariance[index], double(stat)*stat);
    p.setXError(xError);         // the uncertainties as stored, the covariance kept
    p.setYStatError(stat);
    }
  catch (const std::exception &) { }   // not a valid covariance (checked when set): the uncertainties alone
  }

  double DataPointColumns::covarianceAt(unsigned int index) const
  {
  checkIndex(index,__FUNCTION__);
  return _xyCovariance.empty() ? 0.0 : _xyCovariance[index];
  }

  void DataPointColumns::storeCovariance(unsigned int index, float covariance)
  {
  if (_xyCovariance.empty())
    {
    if (covariance==0.0f) return;
    _xyCovariance.assign(_value.size(),0.0f);
    }
  _xyCovariance[index] = covariance;
  }

  void DataPointColumns::setCovarianceColumn(const std::vector<double> & covariance)
  {
  if (covariance.empty()) { _xyCovariance.clear(); return; }
  if (_rank!=1) throw MathException("Covariances of x and y need points of rank 1",__FUNCTION__);
  if (covariance.size()!=_value.size()) throw MathException("The covariance column must have one value per point",__FUNCTION__);
  std::vector<float> column(covariance.size());
  bool any = false;
  for (std::size_t k=0; k<covariance.size(); k++)
    {
    double c = covariance[k], varX = double(_xError[0][k])*_xError[0][k], varY = double(_stat[k])*_stat[k];
    if (!std::isfinite(c)) throw MathException("Covariance not finite",__FUNCTION__);
    if (c*c > varX*varY*(1.0+1.0e-5) + 1.0e-30)
      throw MathException("Covariance of point "+std::to_string(k)+" is not valid: cov(x,y)^2 > var(x) var(y)",__FUNCTION__);
    column[k] = float(c);
    if (column[k]!=0.0f) any = true;
    }
  if (any) _xyCovariance.swap(column); else _xyCovariance.clear();
  }

  std::vector<double> DataPointColumns::coordinatesAt(unsigned int index) const
  {
  checkIndex(index,__FUNCTION__);
  std::vector<double> c(_rank);
  for (unsigned int i=0; i<_rank; i++) c[i] = _x[i][index];
  return c;
  }

  std::vector<double> DataPointColumns::coordinateErrorsAt(unsigned int index) const
  {
  checkIndex(index,__FUNCTION__);
  std::vector<double> c(_rank);
  for (unsigned int i=0; i<_rank; i++) c[i] = _xError[i][index];
  return c;
  }

  double DataPointColumns::valueAt(unsigned int index) const     { checkIndex(index,__FUNCTION__); return _value[index]; }
  double DataPointColumns::statErrorAt(unsigned int index) const { checkIndex(index,__FUNCTION__); return _stat[index];  }
  double DataPointColumns::sysErrorAt(unsigned int index) const  { checkIndex(index,__FUNCTION__); return std::max(_syst[index],_systMinus[index]); }
  double DataPointColumns::sysErrorPlusAt(unsigned int index) const  { checkIndex(index,__FUNCTION__); return _syst[index]; }
  double DataPointColumns::sysErrorMinusAt(unsigned int index) const { checkIndex(index,__FUNCTION__); return _systMinus[index]; }

  void DataPointColumns::addPoint(const DataPoint2D & dataPoint)
  {
  addPoint(static_cast<const DataPoint &>(dataPoint));
  storeCovariance(size()-1,dataPoint.xyCovariance());
  }

  void DataPointColumns::addPoint(float x, float xError, float y, float yStatError, float ySysError)
  {
  addPoint(std::vector<double>{x},std::vector<double>{xError},y,yStatError,ySysError);
  }

  void DataPointColumns::print() const
  {
  for (unsigned int k=0; k<size(); k++) std::cout << k << ": " << *createPoint(k) << std::endl;
  }

  //////////////////////////////////////////////////////////////////////////
  // Points of any rank
  //////////////////////////////////////////////////////////////////////////

  void DataPointColumns::addPoint(const DataPoint & dataPoint)
  {
  addPoint(dataPoint.xValues(),dataPoint.xErrors(),dataPoint.value(),dataPoint.stat(),dataPoint.systPlus(),dataPoint.systMinus());
  }

  void DataPointColumns::addPoint(const std::vector<double> & x, const std::vector<double> & xError,
                                  double value, double stat, double syst)
  {
  addPoint(x,xError,value,stat,syst,syst);
  }

  void DataPointColumns::addPoint(const std::vector<double> & x, const std::vector<double> & xError,
                                  double value, double stat, double systPlus, double systMinus)
  {
  if (x.empty()) throw MathException("A point needs at least one independent variable",__FUNCTION__);
  if (!_value.empty() && x.size()!=_rank) throw MathException("Point rank differs from the rank of the container",__FUNCTION__);
  if (!xError.empty() && xError.size()!=x.size()) throw MathException("x and xError sizes differ",__FUNCTION__);
  for (double e : xError) if (e<0) throw MathException("xError<0",__FUNCTION__);
  if (stat<0) throw MathException("stat<0",__FUNCTION__);
  if (systPlus<0)  throw MathException("systPlus<0",__FUNCTION__);
  if (systMinus<0) throw MathException("systMinus<0",__FUNCTION__);
  if (_value.empty() && x.size()!=_rank) setRank(x.size());
  // Make room in every column first (geometric growth), so that the push_backs below cannot throw part way
  // through and leave columns of different sizes.
  std::size_t n = _value.size();
  bool full = _value.capacity()==n || _stat.capacity()==n || _syst.capacity()==n || _systMinus.capacity()==n;
  for (unsigned int i=0; i<_rank && !full; i++) full = _x[i].capacity()==n || _xError[i].capacity()==n;
  if (full) reserve(std::max<std::size_t>(16,2*n));
  for (unsigned int i=0; i<_rank; i++)
    {
    _x[i].push_back(x[i]);
    _xError[i].push_back(xError.empty() ? 0.0f : float(xError[i]));
    }
  _value.push_back(value);
  _stat.push_back(stat);
  _syst.push_back(systPlus);
  _systMinus.push_back(systMinus);
  if (!_xyCovariance.empty()) _xyCovariance.push_back(0.0f);
  }

  std::unique_ptr<DataPoint> DataPointColumns::createPoint(unsigned int index) const
  {
  checkIndex(index,__FUNCTION__);
  std::unique_ptr<DataPoint> p;
  if (_rank==1)
    {
    DataPoint2D * q = new DataPoint2D(_x[0][index],_xError[0][index],_value[index],_stat[index],_syst[index]);
    p.reset(q);
    q->setSyst(_syst[index],_systMinus[index]);
    applyCovariance(*q,index);
    return p;
    }
  else if (_rank==2)
    p.reset(new DataPoint3D(_x[0][index],_xError[0][index],_x[1][index],_xError[1][index],
                            _value[index],_stat[index],_syst[index]));
  else
    p.reset(new DataPointND(coordinatesAt(index),coordinateErrorsAt(index),_value[index],_stat[index],_syst[index]));
  p->setSyst(_syst[index],_systMinus[index]);
  return p;
  }

  //////////////////////////////////////////////////////////////////////////
  // Columns
  //////////////////////////////////////////////////////////////////////////

  const std::vector<float> & DataPointColumns::xColumn(unsigned int variable) const
  {
  checkVariable(variable,__FUNCTION__);
  return _x[variable];
  }

  const std::vector<float> & DataPointColumns::xErrorColumn(unsigned int variable) const
  {
  checkVariable(variable,__FUNCTION__);
  return _xError[variable];
  }

  void DataPointColumns::setColumns(const std::vector< std::vector<double> > & x,
                                    const std::vector< std::vector<double> > & xError,
                                    const std::vector<double> & value,
                                    const std::vector<double> & stat,
                                    const std::vector<double> & syst)
  {
  setColumns(x,xError,value,stat,syst,syst);
  }

  void DataPointColumns::setColumns(const std::vector< std::vector<double> > & x,
                                    const std::vector< std::vector<double> > & xError,
                                    const std::vector<double> & value,
                                    const std::vector<double> & stat,
                                    const std::vector<double> & systPlus,
                                    const std::vector<double> & systMinus)
  {
  if (x.empty()) throw MathException("At least one independent variable column is required",__FUNCTION__);
  if (!xError.empty() && xError.size()!=x.size()) throw MathException("x and xError have different numbers of columns",__FUNCTION__);
  std::size_t n = value.size();
  for (const auto & c : x)      checkColumn(c,n,false,false,"x",__FUNCTION__);
  for (const auto & c : xError) checkColumn(c,n,true,true,"xError",__FUNCTION__);
  checkColumn(stat,n,true,true,"stat",__FUNCTION__);
  checkColumn(systPlus,n,true,true,"systPlus",__FUNCTION__);
  checkColumn(systMinus,n,true,true,"systMinus",__FUNCTION__);
  // Build the new columns, then swap them in: the container is unchanged if anything above or below throws.
  std::vector< std::vector<float> > newX, newXError;
  newX.reserve(x.size());
  newXError.reserve(x.size());
  for (std::size_t i=0; i<x.size(); i++)
    {
    newX.push_back(toFloat(x[i],n));
    newXError.push_back(toFloat(xError.empty() ? std::vector<double>() : xError[i],n));
    }
  std::vector<float> newValue = toFloat(value,n);
  std::vector<float> newStat  = toFloat(stat,n);
  std::vector<float> newSyst  = toFloat(systPlus,n);
  std::vector<float> newSystMinus = systMinus.empty() ? newSyst : toFloat(systMinus,n);
  _rank = x.size();
  _xyCovariance.clear();   // new columns: no covariance (setCovarianceColumn())
  _x.swap(newX);
  _xError.swap(newXError);
  _value.swap(newValue);
  _stat.swap(newStat);
  _syst.swap(newSyst);
  _systMinus.swap(newSystMinus);
  }

  //////////////////////////////////////////////////////////////////////////
  // Setters
  //////////////////////////////////////////////////////////////////////////

  void DataPointColumns::setRank(unsigned int rank)
  {
  if (rank==0) throw MathException("rank must be at least 1",__FUNCTION__);
  if (!_value.empty()) throw MathException("The rank can only be changed while the container is empty",__FUNCTION__);
  _x.assign(rank,std::vector<float>());
  _xError.assign(rank,std::vector<float>());
  _xyCovariance.clear();
  _rank = rank;
  }

  void DataPointColumns::storePoint(unsigned int index, const DataPoint & dataPoint)
  {
  for (unsigned int i=0; i<_rank; i++)
    {
    _x[i][index]      = dataPoint.x(i);
    _xError[i][index] = dataPoint.xError(i);
    }
  _value[index] = dataPoint.value();
  _stat[index]  = dataPoint.stat();
  _syst[index]  = dataPoint.systPlus();
  _systMinus[index] = dataPoint.systMinus();
  const DataPoint2D * q = dynamic_cast<const DataPoint2D *>(&dataPoint);
  storeCovariance(index,q ? q->xyCovariance() : 0.0f);
  }

  void DataPointColumns::setPoint(unsigned int index, const DataPoint & dataPoint)
  {
  checkIndex(index,__FUNCTION__);
  if (dataPoint.rank()!=_rank) throw MathException("Point rank differs from the rank of the container",__FUNCTION__);
  storePoint(index,dataPoint);
  }

  void DataPointColumns::removePoint(unsigned int index)
  {
  checkIndex(index,__FUNCTION__);
  for (unsigned int i=0; i<_rank; i++)
    {
    _x[i].erase(_x[i].begin()+index);
    _xError[i].erase(_xError[i].begin()+index);
    }
  _value.erase(_value.begin()+index);
  _stat.erase(_stat.begin()+index);
  _syst.erase(_syst.begin()+index);
  _systMinus.erase(_systMinus.begin()+index);
  if (!_xyCovariance.empty()) _xyCovariance.erase(_xyCovariance.begin()+index);
  }

  void DataPointColumns::clear()
  {
  for (unsigned int i=0; i<_rank; i++) { _x[i].clear(); _xError[i].clear(); }
  _value.clear();
  _stat.clear();
  _syst.clear();
  _systMinus.clear();
  _xyCovariance.clear();
  }

  void DataPointColumns::reserve(unsigned int n)
  {
  for (unsigned int i=0; i<_rank; i++) { _x[i].reserve(n); _xError[i].reserve(n); }
  _value.reserve(n);
  _stat.reserve(n);
  _syst.reserve(n);
  _systMinus.reserve(n);
  }

  void DataPointColumns::sortByX(unsigned int variable)
  {
  checkVariable(variable,__FUNCTION__);
  std::vector<std::size_t> order(_value.size());
  std::iota(order.begin(),order.end(),std::size_t(0));
  const std::vector<float> & key = _x[variable];
  std::stable_sort(order.begin(),order.end(),[&key](std::size_t a, std::size_t b) { return key[a] < key[b]; });
  auto permute = [&order](std::vector<float> & column)
    {
    std::vector<float> sorted(column.size());
    for (std::size_t k=0; k<order.size(); k++) sorted[k] = column[order[k]];
    column.swap(sorted);
    };
  for (unsigned int i=0; i<_rank; i++) { permute(_x[i]); permute(_xError[i]); }
  permute(_value);
  permute(_stat);
  permute(_syst);
  permute(_systMinus);
  if (!_xyCovariance.empty()) permute(_xyCovariance);
  }

  //////////////////////////////////////////////////////////////////////////
  // Arithmetic
  //////////////////////////////////////////////////////////////////////////

  namespace
  {
  DataPointColumns combineColumns(const DataPointColumns & left, const DataPointColumns & right, bool subtract, const char * source)
  {
  if (left.rank()!=right.rank()) throw MathException("Data sets have different ranks",source);
  if (left.size()!=right.size()) throw MathException("Data sets have different numbers of points",source);
  DataPointColumns result(left);
  for (unsigned int k=0; k<left.size(); k++)
    {
    std::unique_ptr<DataPoint> p = left.createPoint(k);
    std::unique_ptr<DataPoint> q = right.createPoint(k);
    if (subtract) p->subtract(*q); else p->add(*q);
    result.setPoint(k,*p);
    }
  result.setCommonSystOf(left,right,subtract);
  return result;
  }
  }

  DataPointColumns & DataPointColumns::operator+=(const DataPointColumns & rhs)
  {
  DataPointColumns result = combineColumns(*this,rhs,false,__FUNCTION__);
  *this = std::move(result);
  return *this;
  }

  DataPointColumns & DataPointColumns::operator-=(const DataPointColumns & rhs)
  {
  DataPointColumns result = combineColumns(*this,rhs,true,__FUNCTION__);
  *this = std::move(result);
  return *this;
  }

  DataPointColumns operator+(const DataPointColumns & left, const DataPointColumns & right) { DataPointColumns r(left); r += right; return r; }
  DataPointColumns operator-(const DataPointColumns & left, const DataPointColumns & right) { DataPointColumns r(left); r -= right; return r; }

  //////////////////////////////////////////////////////////////////////////
  // Binary I/O
  //////////////////////////////////////////////////////////////////////////

  namespace
  {
  //! Whether every value of a column is +0 (bit for bit: -0 is kept).
  bool allZero(const std::vector<float> & column)
  {
  for (float v : column) { uint32_t u; std::memcpy(&u,&v,4); if (u!=0) return false; }
  return true;
  }
  }

  void DataPointColumns::encodeBinary(BinaryWriter & w) const
  {
  // format version 4: a flag per column (0: all zero, not written; 1: written; for the lower systematic, 2: the
  // same as the upper one), then the columns written: x and its uncertainty for each variable, the value, the
  // statistical, upper and lower systematic uncertainties, and the covariance of x and y (rank 1)
  w.writeVarUInt(_rank);
  w.writeVarUInt(_value.size());
  std::vector<const std::vector<float> *> columns;
  for (unsigned int i=0; i<_rank; i++) { columns.push_back(&_x[i]); columns.push_back(&_xError[i]); }
  columns.push_back(&_value);
  columns.push_back(&_stat);
  columns.push_back(&_syst);
  std::vector<uint8_t> flags;
  for (const auto * c : columns) flags.push_back(allZero(*c) ? 0 : 1);
  flags.push_back(_systMinus==_syst ? 2 : (allZero(_systMinus) ? 0 : 1));
  flags.push_back(_xyCovariance.empty() || allZero(_xyCovariance) ? 0 : 1);
  columns.push_back(&_systMinus);
  columns.push_back(&_xyCovariance);
  for (uint8_t f : flags) w.writeUInt8(f);
  for (std::size_t k=0; k<columns.size(); k++)
    if (flags[k]==1) for (float v : *columns[k]) w.writeFloat32(v);
  _fits.encodeBinary(w);
  encodeCommonSyst(w);
  w.writeString(title());
  }

  void DataPointColumns::decodeBinary(BinaryReader & r, const String & name, unsigned int version)
  {
  uint64_t rank = r.readVarUInt();
  uint64_t n    = r.readVarUInt();
  if (rank==0 || rank>kMaximumDecodedRank) throw Exception("Invalid rank","DataPointColumns::decodeBinary()");
  if (n>r.remaining()) throw Exception("More points than data","DataPointColumns::decodeBinary()");
  auto readColumn = [&r,n]() { std::vector<float> column(n); for (auto & v : column) v = r.readFloat32(); return column; };
  std::vector< std::vector<float> > x, xError;
  std::vector<float> value, stat, syst, systMinus, covariance;
  double commonPlus = 0.0, commonMinus = 0.0;
  String newTitle = title();
  DataPointColumns probe;            // reads the fits and the common systematic without changing this object before the end
  if (version>=4)
    {
    std::size_t nColumns = 2*rank+5;
    if (nColumns > r.remaining()) throw Exception("More columns than data","DataPointColumns::decodeBinary()");
    std::vector<uint8_t> flags(nColumns);
    uint64_t written = 0;
    for (std::size_t k=0; k<nColumns; k++)
      {
      flags[k] = r.readUInt8();
      if (flags[k]>(k==nColumns-2 ? 2 : 1)) throw Exception("Invalid column flag","DataPointColumns::decodeBinary()");
      if (k==nColumns-1 && flags[k] && rank!=1) throw Exception("Covariance of points of rank > 1","DataPointColumns::decodeBinary()");
      if (flags[k]==1) written++;
      }
    if (written*4*n > r.remaining()) throw Exception("More points than data","DataPointColumns::decodeBinary()");
    std::size_t k = 0;
    auto next = [&]() { uint8_t f = flags[k++]; return f==1 ? readColumn() : std::vector<float>(n,0.0f); };
    for (uint64_t i=0; i<rank; i++) { x.push_back(next()); xError.push_back(next()); }
    value = next();
    stat  = next();
    syst  = next();
    if (flags[k]==2) { systMinus = syst; k++; } else systMinus = next();
    if (flags[k]==1) covariance = next(); else k++;
    probe._fits.decodeBinary(r);
    probe.decodeCommonSyst(r);
    newTitle = r.readString();
    }
  else
    {
    if (4*n*(2*rank+3)>r.remaining()) throw Exception("More points than data","DataPointColumns::decodeBinary()");
    for (uint64_t i=0; i<rank; i++) { x.push_back(readColumn()); xError.push_back(readColumn()); }
    value = readColumn();
    stat  = readColumn();
    syst  = readColumn();
    if (version>=2) probe._fits.decodeBinary(r);
    systMinus = syst;
    if (version>=3)
      {
      probe.decodeCommonSyst(r);
      uint8_t asymmetric = r.readUInt8();
      if (asymmetric>1) throw Exception("Invalid systematic column flag","DataPointColumns::decodeBinary()");
      if (asymmetric) systMinus = readColumn();
      }
    }
  for (float v : systMinus) if (!(v>=0)) throw Exception("Invalid lower systematic uncertainty","DataPointColumns::decodeBinary()");
  commonPlus  = probe.getCommonSystPlus();
  commonMinus = probe.getCommonSystMinus();
  if (version>=2) _fits = probe._fits;
  _commonSystPlus  = commonPlus;
  _commonSystMinus = commonMinus;
  _rank = rank;
  _x.swap(x);
  _xError.swap(xError);
  _value.swap(value);
  _stat.swap(stat);
  _syst.swap(syst);
  _systMinus.swap(systMinus);
  _xyCovariance.swap(covariance);
  setTitle(newTitle);
  if (!name.isEmpty()) setName(name);   // the name of the record
  }

  void DataPointColumns::writeBinary(std::ostream & out) const                     { writeBinaryObject(out,*this); }
  bool DataPointColumns::readBinary(std::istream & in)                             { return readBinaryObject(in,*this); }
  void DataPointColumns::saveBinary(const String & fileName, bool append) const    { saveBinaryObject(fileName,*this,append); }
  void DataPointColumns::loadBinary(const String & fileName, unsigned int index)   { loadBinaryObjectAt(fileName,*this,index); }
  void DataPointColumns::saveBinary(const String & fileName, const std::vector<DataPointColumns> & objects) { saveBinaryObjects(fileName,objects); }
  std::vector<DataPointColumns> DataPointColumns::loadAllBinary(const String & fileName)       { return loadAllBinaryObjects<DataPointColumns>(fileName); }

  } // namespace MATH
} // namespace Datum
