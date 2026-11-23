/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//
// HEPDataConverter: tables of functions and fits (functionTable(), fitTable(), fitTables()) and back (toFunction(),
// toFit()).
//
#include "HEPDataConverter.hpp"
#include "HEPDataBase64.hpp"
#include "BinaryIO.hpp"
#include "Exceptions.hpp"
#include "FitRecord.hpp"
#include "ParameterizedFunction.hpp"
#include "FunctionRegistry.hpp"
#include "yaml-cpp/yaml.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace Datum
{

using namespace MATH;

namespace
{
const char * kSource = "HEPDataConverter";

std::string number(double v)
{
  if (std::isinf(v)) return v>0 ? ".inf" : "-.inf";
  if (std::isnan(v)) return ".nan";
  char buffer[64];
  int minimum = 1;
  double a = std::fabs(v);
  if (a>=1.0 && a<1.0e15) minimum = int(std::floor(std::log10(a)))+1;
  for (int p=minimum; p<=17; p++)
    {
    std::snprintf(buffer,sizeof(buffer),"%.*g",p,v);
    if (std::strtod(buffer,nullptr)==v) break;
    }
  return buffer;
}

//! Name of the class of a function in FunctionRegistry ("" if it is not registered).
String className(const ParameterizedFunction & f)
{
  try { return FunctionRegistry::typeName(f); }
  catch (const Exception &) { return f.registryName(); }
}

std::string shortNumber(double v)
{
  char buffer[64];
  std::snprintf(buffer,sizeof(buffer),"%.6g",v);
  return buffer;
}

const char * typeName(Parameter::ParameterType t)
{
  switch (t)
    {
    case Parameter::Fixed:   return "fixed";
    case Parameter::LowHigh: return "lowhigh";
    case Parameter::Low:     return "low";
    case Parameter::High:    return "high";
    default:                 return "free";
    }
}

//! The rows of a table of a function: points (x values) or bins (edges).
struct Rows
{
  std::vector<double> x, low, high;
  bool bins = false;
};

Rows rowsOf(double low, double high, const HEPDataFunctionOptions & options)
{
  Rows rows;
  if (!options.edges.empty())
    {
    if (options.edges.size()<2) throw Exception("Bins need at least 2 edges",kSource);
    for (std::size_t i=1; i<options.edges.size(); i++)
      if (!(options.edges[i]>options.edges[i-1])) throw Exception("The edges of the bins must increase",kSource);
    rows.bins = true;
    for (std::size_t i=0; i+1<options.edges.size(); i++)
      {
      rows.low.push_back(options.edges[i]);
      rows.high.push_back(options.edges[i+1]);
      rows.x.push_back(0.5*(options.edges[i]+options.edges[i+1]));
      }
    return rows;
    }
  if (!(high>low) || !std::isfinite(low) || !std::isfinite(high)) throw Exception("Empty or infinite range",kSource);
  unsigned int n = std::max(2u,options.nPoints);
  for (unsigned int i=0; i<n; i++) rows.x.push_back(low+(high-low)*double(i)/double(n-1));
  return rows;
}

//! Value of a row: the function at the point, or its average over the bin.
double rowValue(const ParameterizedFunction & f, const Rows & rows, unsigned int i, bool averages)
{
  if (rows.bins && averages) return f.integralInRange(rows.low[i],rows.high[i])/(rows.high[i]-rows.low[i]);
  return f.evaluateAt(rows.x[i]);
}

//! Uncertainties of the rows from the covariance of the free parameters of a fit (first order, numerical derivatives).
std::vector<double> band(const FitRecord & fit, const Rows & rows, bool averages)
{
  std::vector<double> errors(rows.x.size(),0.0);
  const FitResult & r = fit.result;
  unsigned int nFree = (unsigned int) r.freeParameters.size();
  if (!r.validErrors || nFree==0 || r.covariance.nRows()!=nFree || r.covariance.nColumns()!=nFree) return errors;
  std::vector<std::vector<double>> gradient(nFree,std::vector<double>(rows.x.size(),0.0));
  for (unsigned int j=0; j<nFree; j++)
    {
    unsigned int p = r.freeParameters[j];
    double v = fit.function->parameter(p).value();
    double e = p<r.errors.size() ? r.errors[p] : 0.0;
    double h = e>0 ? 1.0E-3*e : 1.0E-6*(std::fabs(v)+1.0E-6);
    std::unique_ptr<ParameterizedFunction> up(fit.function->clone()), down(fit.function->clone());
    double hUp = h, hDown = h;
    try { up->setParameterValue(p,v+h); } catch (const Exception &) { up.reset(fit.function->clone()); hUp = 0; }
    try { down->setParameterValue(p,v-h); } catch (const Exception &) { down.reset(fit.function->clone()); hDown = 0; }
    if (hUp+hDown==0) continue;
    for (unsigned int i=0; i<rows.x.size(); i++)
      gradient[j][i] = (rowValue(*up,rows,i,averages)-rowValue(*down,rows,i,averages))/(hUp+hDown);
    }
  for (unsigned int i=0; i<rows.x.size(); i++)
    {
    double variance = 0;
    for (unsigned int a=0; a<nFree; a++)
      for (unsigned int b=0; b<nFree; b++) variance += gradient[a][i]*r.covariance(a,b)*gradient[b][i];
    errors[i] = variance>0 ? std::sqrt(variance) : 0.0;
    }
  return errors;
}

void emitFunction(YAML::Emitter & out, const ParameterizedFunction & f, const FitRecord * fit)
{
  out << YAML::Key << "function" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "class" << YAML::Value << className(f).str();
  out << YAML::Key << "name" << YAML::Value << f.name().str();
  out << YAML::Key << "rank" << YAML::Value << f.rank();
  out << YAML::Key << "parameters" << YAML::Value << YAML::BeginSeq;
  for (unsigned int k=0; k<f.nParameters(); k++)
    {
    const Parameter & p = f.parameter(k);
    out << YAML::Flow << YAML::BeginMap << YAML::Key << "name" << YAML::Value << p.name().str()
        << YAML::Key << "value" << YAML::Value << number(p.value());
    if (fit && k<fit->result.errors.size()) out << YAML::Key << "error" << YAML::Value << number(fit->result.errors[k]);
    out << YAML::Key << "type" << YAML::Value << typeName(p.type());
    if (p.isLowConstrained())  out << YAML::Key << "min" << YAML::Value << number(p.minimum());
    if (p.isHighConstrained()) out << YAML::Key << "max" << YAML::Value << number(p.maximum());
    out << YAML::EndMap;
    }
  out << YAML::EndSeq;
  BinaryWriter w;
  f.encodeBinary(w);
  out << YAML::Key << "record" << YAML::Value << HEPDataBase64::encode(w.buffer());
  out << YAML::EndMap;
}

HEPDataTable makeTable(const ParameterizedFunction & f, const FitRecord * fit, double low, double high,
                       const HEPDataFunctionOptions & options)
{
  if (f.rank()!=1) throw Exception(String("Function ")+f.name()+" is not of one variable: no table",kSource);
  Rows rows = rowsOf(low,high,options);
  HEPDataTable t;
  HEPDataVariable x, y;
  x.name = options.xName;
  x.units = options.xUnits;
  for (unsigned int i=0; i<rows.x.size(); i++)
    x.values.push_back(rows.bins ? HEPDataValue::bin(rows.low[i],rows.high[i]) : HEPDataValue::number(rows.x[i]));
  // the title, unless it is a default one ("Function", the class name), else the name
  bool titled = !f.title().isNull() && f.title().str()!="Function" && f.title().str()!=f.className().str();
  y.name = options.yName.isNull() ? (titled ? f.title() : f.name()) : options.yName;
  y.units = options.yUnits;
  // the parameters, readable on HEPData
  for (unsigned int k=0; k<f.nParameters(); k++)
    {
    const Parameter & p = f.parameter(k);
    std::string value = shortNumber(p.value());
    if (fit && k<fit->result.errors.size() && fit->result.errors[k]>0) value += " +- " + shortNumber(fit->result.errors[k]);
    if (p.type()==Parameter::Fixed) value += " (fixed)";
    String name = p.name().isNull() ? String("p"+std::to_string(k)) : p.name();
    y.qualifiers.push_back({name,String(value),String()});
    }
  String cls = className(f);
  if (!cls.isNull()) y.qualifiers.push_back({String("FUNCTION"),cls,String()});
  if (fit && fit->result.nDegreesOfFreedom>0)
    y.qualifiers.push_back({String("CHI2/NDF"),String(shortNumber(fit->result.chiSquare)+"/"+std::to_string(fit->result.nDegreesOfFreedom)),String()});
  std::vector<double> errors = fit && options.band ? band(*fit,rows,options.averages) : std::vector<double>(rows.x.size(),0.0);
  for (unsigned int i=0; i<rows.x.size(); i++)
    {
    HEPDataValue v = HEPDataValue::number(rowValue(f,rows,i,options.averages));
    if (errors[i]>0) v.errors.push_back(HEPDataError::symmetric(errors[i],"fit"));
    y.values.push_back(v);
    }
  t.independent.push_back(x);
  t.dependent.push_back(y);
  t.name = fit && !fit->label.isNull() ? fit->label : f.name();
  t.description = String(fit ? "Fit of " : "Function ") + (cls.isNull() ? f.name() : cls)
                + String(rows.bins ? (options.averages ? ": averages over the bins." : ": values at the centers of the bins.") : ": values.");
  if (options.lossless)
    {
    YAML::Emitter out;
    out << YAML::BeginMap << YAML::Key << "version" << YAML::Value << 1;
    emitFunction(out,f,fit);
    if (fit)
      {
      out << YAML::Key << "fit" << YAML::Value << YAML::BeginMap;
      out << YAML::Key << "label" << YAML::Value << fit->label.str();
      out << YAML::Key << "statistic" << YAML::Value << fit->result.statistic.str();
      out << YAML::Key << "chiSquare" << YAML::Value << number(fit->result.chiSquare);
      out << YAML::Key << "nDegreesOfFreedom" << YAML::Value << fit->result.nDegreesOfFreedom;
      out << YAML::Key << "pValue" << YAML::Value << number(fit->result.pValue);
      FitList list;
      list.add(*fit);
      BinaryWriter w;
      list.encodeBinary(w);
      out << YAML::Key << "record" << YAML::Value << HEPDataBase64::encode(w.buffer());
      out << YAML::EndMap;
      }
    out << YAML::EndMap;
    t.datumBlock = String(std::string(out.c_str())+"\n");
    }
  return t;
}

YAML::Node block(const HEPDataTable & table)
{
  if (table.datumBlock.isNull()) return YAML::Node();
  try { return YAML::Load(table.datumBlock.str()); }
  catch (const YAML::Exception & e) { throw Exception(String("Invalid datum block: ")+e.what(),kSource); }
}
}

HEPDataTable HEPDataConverter::functionTable(const ParameterizedFunction & function, double low, double high,
                                             const FunctionOptions & options)
{
  return makeTable(function,nullptr,low,high,options);
}

HEPDataTable HEPDataConverter::fitTable(const FitRecord & fit, const FunctionOptions & options)
{
  if (!fit.function) throw Exception("A fit without function",kSource);
  double low = fit.low.empty() ? 0.0 : fit.low[0], high = fit.high.empty() ? 0.0 : fit.high[0];
  return makeTable(*fit.function,&fit,low,high,options);
}

std::vector<HEPDataTable> HEPDataConverter::fitTables(const DataPoints & object, const FunctionOptions & options)
{
  std::vector<HEPDataTable> tables;
  for (unsigned int k=0; k<object.nFits(); k++)
    {
    HEPDataTable t = fitTable(object.fit(k),options);
    t.name = String(object.name().str()+" fit "+std::to_string(k));
    if (!object.fit(k).label.isNull()) t.name = String(t.name.str()+": "+object.fit(k).label.str());
    tables.push_back(t);
    }
  return tables;
}

std::shared_ptr<ParameterizedFunction> HEPDataConverter::toFunction(const HEPDataTable & table)
{
  YAML::Node b = block(table);
  if (!b || !b["function"] || !b["function"]["record"]) return nullptr;
  try
    {
    std::string name = b["function"]["name"] ? b["function"]["name"].as<std::string>() : std::string();
    std::string bytes = HEPDataBase64::decode(b["function"]["record"].as<std::string>());
    BinaryReader r(bytes.data(),bytes.size());
    std::shared_ptr<ParameterizedFunction> f(ParameterizedFunction::decodeNewBinary(r,String(name),1));
    if (!f) throw Exception("No function read",kSource);
    return f;
    }
  catch (const YAML::Exception & e) { throw Exception(String("Invalid datum block: ")+e.what(),kSource); }
}

std::shared_ptr<const FitRecord> HEPDataConverter::toFit(const HEPDataTable & table)
{
  YAML::Node b = block(table);
  if (!b || !b["fit"] || !b["fit"]["record"]) return nullptr;
  try
    {
    std::string bytes = HEPDataBase64::decode(b["fit"]["record"].as<std::string>());
    BinaryReader r(bytes.data(),bytes.size());
    FitList list;
    list.decodeBinary(r);
    if (list.size()==0) throw Exception("The fit could not be read (its function is not available)",kSource);
    return list.pointer(0);
    }
  catch (const YAML::Exception & e) { throw Exception(String("Invalid datum block: ")+e.what(),kSource); }
}

} // namespace Datum
