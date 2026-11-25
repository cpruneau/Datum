/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file ParameterizedComposite.cpp
//! @brief Implementation of ParameterizedComposite.
#include "ParameterizedComposite.hpp"
#include <memory>
#include "BinaryIO.hpp"
#include "Exceptions.hpp"
#include "MathExceptions.hpp"

namespace Datum
{
  namespace MATH
  {

  ParameterizedComposite::ParameterizedComposite(Operation operation)
  :
  ParameterizedFunction(1),
  _operation(operation),
  _offsets(),
  _components(),
  _labels(),
  _coefficients(),
  _first(),
  _registryName(),
  _synchronized()
  { }

  void ParameterizedComposite::setOperation(Operation operation)
  {
  if (!_components.empty()) throw MathException("The operation of a composite is set before its components are added", __FUNCTION__);
  _operation = operation;
  }

  ParameterizedComposite::ParameterizedComposite(const ParameterizedComposite & source)
  :
  ParameterizedFunction(source),
  _operation(source._operation),
  _offsets(source._offsets),
  _components(),
  _labels(source._labels),
  _coefficients(source._coefficients),
  _first(source._first),
  _registryName(source._registryName),
  _synchronized()
  {
  for (const auto & component : source._components) _components.emplace_back(component->clone());
  }

  ParameterizedComposite & ParameterizedComposite::operator=(const ParameterizedComposite & rhs)
  {
  if (this == &rhs) return *this;
  std::vector<std::unique_ptr<ParameterizedFunction>> components;
  for (const auto & component : rhs._components) components.emplace_back(component->clone());
  ParameterizedFunction::operator=(rhs);
  _components.swap(components);
  _operation = rhs._operation;
  _offsets = rhs._offsets;
  _labels = rhs._labels;
  _coefficients = rhs._coefficients;
  _first = rhs._first;
  _registryName = rhs._registryName;
  _synchronized.clear();
  return *this;
  }

  void ParameterizedComposite::add(const String & label, const ParameterizedFunction & function)
  {
  add(label, function, 1.0);
  }

  double ParameterizedComposite::coefficient(unsigned int k) const
  {
  if (k >= _coefficients.size()) throw MathException("Component index out of range", __FUNCTION__);
  return _coefficients[k];
  }

  void ParameterizedComposite::setCoefficient(unsigned int k, double c)
  {
  if (k >= _coefficients.size()) throw MathException("Component index out of range", __FUNCTION__);
  _coefficients[k] = c;
  }

  void ParameterizedComposite::add(const String & label, const ParameterizedFunction & function, double coefficient)
  {
  if (label.isNull()) throw MathException("A component of a composite needs a label", __FUNCTION__);
  for (const auto & existing : _labels)
    if (existing == label) throw MathException(String("The label ") + label + String(" is already used"), __FUNCTION__);
  if (_operation != kProduct && !_components.empty() && function.rank() != rank())
    throw MathException("The components of a sum, a pointwise product or a ratio must have the same rank", __FUNCTION__);
  if (_operation == kRatio && _components.size() >= 2)
    throw MathException("A ratio has two components", __FUNCTION__);
  std::unique_ptr<ParameterizedFunction> copy(function.clone());
  if (_operation == kProduct)
    {
    _offsets.push_back(_components.empty() ? 0u : rank());
    setRank(_components.empty() ? function.rank() : rank() + function.rank());
    }
  else if (_components.empty()) setRank(function.rank());
  _first.push_back(nParameters());
  for (unsigned int k=0; k<copy->nParameters(); k++)
    {
    Parameter p = copy->parameter(k);
    p.setName(label + String(".") + p.name());
    addParameter(p);
    }
  _labels.push_back(label);
  _coefficients.push_back(coefficient);
  _components.push_back(std::move(copy));
  _synchronized.clear();
  }

  const ParameterizedFunction & ParameterizedComposite::component(unsigned int k) const
  {
  if (k >= _components.size()) throw MathException("Component index out of range", __FUNCTION__);
  synchronize();
  return *_components[k];
  }

  const String & ParameterizedComposite::label(unsigned int k) const
  {
  if (k >= _labels.size()) throw MathException("Component index out of range", __FUNCTION__);
  return _labels[k];
  }

  unsigned int ParameterizedComposite::firstParameter(unsigned int k) const
  {
  if (k >= _first.size()) throw MathException("Component index out of range", __FUNCTION__);
  return _first[k];
  }

  void ParameterizedComposite::synchronize() const
  {
  std::vector<double> values = parameterValues();
  if (values == _synchronized) return;
  for (std::size_t c=0; c<_components.size(); c++)
    for (unsigned int k=0; k<_components[c]->nParameters(); k++)
      _components[c]->setParameter(k, _parameters[_first[c] + k]);   // value, type and bounds of the composite
  _synchronized = values;
  }

  double ParameterizedComposite::evaluateAt(double x) const
  {
  checkRank(1, __FUNCTION__);
  synchronize();
  if (_operation == kRatio)
    {
    if (_components.size() < 2) return _components.empty() ? 0.0 : _coefficients[0]*_components[0]->evaluateAt(x);
    return (_coefficients[0]*_components[0]->evaluateAt(x))/(_coefficients[1]*_components[1]->evaluateAt(x));
    }
  if (_operation == kProduct || _operation == kPointwiseProduct)
    {
    double product = 1;
    for (std::size_t k=0; k<_components.size(); k++) product *= _coefficients[k]*_components[k]->evaluateAt(x);
    return product;
    }
  double sum = 0;
  for (std::size_t k=0; k<_components.size(); k++) sum += _coefficients[k]*_components[k]->evaluateAt(x);
  return sum;
  }

  std::vector<double> ParameterizedComposite::slice(const std::vector<double> & x, unsigned int k) const
  {
  return std::vector<double>(x.begin() + _offsets[k], x.begin() + _offsets[k] + _components[k]->rank());
  }

  double ParameterizedComposite::evaluateAt(const std::vector<double> & x) const
  {
  checkRank((unsigned int)(x.size()), __FUNCTION__);
  synchronize();
  if (_operation == kProduct)
    {
    double product = 1;
    for (unsigned int k=0; k<_components.size(); k++)
      {
      const ParameterizedFunction & c = *_components[k];
      product *= _coefficients[k]*(c.rank() == 1 ? c.evaluateAt(x[_offsets[k]]) : c.evaluateAt(slice(x, k)));
      }
    return product;
    }
  if (_operation == kRatio)
    {
    if (_components.size() < 2) return _components.empty() ? 0.0 : _coefficients[0]*_components[0]->evaluateAt(x);
    return (_coefficients[0]*_components[0]->evaluateAt(x))/(_coefficients[1]*_components[1]->evaluateAt(x));
    }
  if (_operation == kPointwiseProduct)
    {
    double product = 1;
    for (std::size_t k=0; k<_components.size(); k++) product *= _coefficients[k]*_components[k]->evaluateAt(x);
    return product;
    }
  double sum = 0;
  for (std::size_t k=0; k<_components.size(); k++) sum += _coefficients[k]*_components[k]->evaluateAt(x);
  return sum;
  }

  bool ParameterizedComposite::hasAnalyticalDerivative() const
  {
  if (rank() != 1 || _components.empty() || _operation != kSum) return false;
  for (const auto & component : _components) if (!component->hasAnalyticalDerivative()) return false;
  return true;
  }

  double ParameterizedComposite::derivativeAt(double x) const
  {
  checkRank(1, __FUNCTION__);
  if (_operation != kSum) return Function::derivativeAt(x);   // numerical
  synchronize();
  double sum = 0;
  for (std::size_t k=0; k<_components.size(); k++) sum += _coefficients[k]*_components[k]->derivativeAt(x);
  return sum;
  }

  double ParameterizedComposite::integralInRange(double low, double high) const
  {
  checkRank(1, __FUNCTION__);
  if (_operation != kSum) return Function::integralInRange(low, high);   // numerical
  synchronize();
  double sum = 0;
  for (std::size_t k=0; k<_components.size(); k++) sum += _coefficients[k]*_components[k]->integralInRange(low, high);
  return sum;
  }

  double ParameterizedComposite::integralInRange(const std::vector<double> & low, const std::vector<double> & high) const
  {
  checkRank((unsigned int)(low.size()), __FUNCTION__);
  checkRank((unsigned int)(high.size()), __FUNCTION__);
  if (_operation == kPointwiseProduct || _operation == kRatio) return Function::integralInRange(low, high);   // numerical
  synchronize();
  if (_operation == kProduct)
    {
    double product = 1;
    for (unsigned int k=0; k<_components.size(); k++)
      product *= _coefficients[k]*_components[k]->integralInRange(slice(low, k), slice(high, k));
    return product;
    }
  double sum = 0;
  for (std::size_t k=0; k<_components.size(); k++) sum += _coefficients[k]*_components[k]->integralInRange(low, high);
  return sum;
  }

  void ParameterizedComposite::pullFixedParameters()
  {
  for (std::size_t c=0; c<_components.size(); c++)
    for (unsigned int k=0; k<_components[c]->nParameters(); k++)
      {
      Parameter & p = _parameters[_first[c] + k];
      if (p.type() == Parameter::Fixed) p.setValue(_components[c]->parameter(k).value());
      }
  _synchronized.clear();
  }

  void ParameterizedComposite::adaptToRange(double low, double high)
  {
  synchronize();
  for (auto & component : _components) component->adaptToRange(low, high);
  pullFixedParameters();
  }

  void ParameterizedComposite::adaptToRange(const std::vector<double> & low, const std::vector<double> & high)
  {
  if (low.size() != rank() || high.size() != rank()) return;
  synchronize();
  for (unsigned int k=0; k<_components.size(); k++)
    {
    if (_operation == kProduct) _components[k]->adaptToRange(slice(low, k), slice(high, k));
    else _components[k]->adaptToRange(low, high);
    }
  pullFixedParameters();
  }

  ParameterizedComposite ParameterizedComposite::product(const String & labelX, const ParameterizedFunction & f,
                                                         const String & labelY, const ParameterizedFunction & g)
  {
  ParameterizedComposite c(kProduct);
  c.add(labelX, f);
  c.add(labelY, g);
  return c;
  }

  // ---- the state of a composite: its operation, its name in the registry, and its components

  void ParameterizedComposite::encodeState(BinaryWriter & w) const
  {
  w.writeUInt8(uint8_t(_operation));   // 0 sum, 1 product, 2 pointwise product, 3 ratio
  w.writeString(_registryName);
  w.writeVarUInt(nComponents());
  for (unsigned int k=0; k<nComponents(); k++)
    {
    const ParameterizedFunction & f = component(k);
    w.writeString(label(k));
    BinaryWriter block;                       // each component as a whole function payload, preceded by its length
    block.writeString(String(f.name()));
    f.encodeBinary(block);
    w.writeVarUInt(block.buffer().size());
    w.writeBytes(block.buffer().data(), block.buffer().size());
    }
  // the coefficients, after the components (written only if one is not 1: older readers ignore them)
  bool ones = true;
  for (double c : _coefficients) if (c != 1.0) ones = false;
  if (!ones) for (double c : _coefficients) w.writeFloat64(c);
  }

  void ParameterizedComposite::decodeState(BinaryReader & r, unsigned int version)
  {
  if (nComponents() != 0) throw Exception("A composite is read into an empty composite only", __FUNCTION__);
  uint8_t operation = r.readUInt8();
  if (operation > 3) throw Exception("Invalid operation of a composite", __FUNCTION__);
  setOperation(Operation(operation));
  _registryName = r.readString();
  uint64_t n = r.readVarUInt();
  if (n > r.remaining()) throw Exception("More components than data", __FUNCTION__);
  for (uint64_t k=0; k<n; k++)
    {
    String componentLabel = r.readString();
    uint64_t length = r.readVarUInt();
    if (length > r.remaining()) throw Exception("Component larger than the record", __FUNCTION__);
    BinaryReader block(r.readSpan(std::size_t(length)), std::size_t(length));
    String componentName = block.readString();
    std::unique_ptr<ParameterizedFunction> f(decodeNewBinary(block, componentName, version));
    add(componentLabel, *f);
    }
  // the coefficients, if written (1 otherwise)
  if (r.remaining() >= 8*std::size_t(n))
    for (uint64_t k=0; k<n; k++) _coefficients[std::size_t(k)] = r.readFloat64();
  }

  } // namespace MATH
} // namespace Datum
