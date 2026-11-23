/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestYamlCpp.cpp
//! @brief Test program of the yaml-cpp library used by Datum (run by ctest).
//
// TestYamlCpp -- yaml-cpp is found or built and linked, and reads what HEPData files hold: a data file in flow style
// (bins with low/high, infinite edges, symmetric and asymmetric errors, percentages, labels, qualifiers, the
// non-numeric value '-'), a multi-document submission.yaml, and JSON; and writes YAML read back identically.
// Prints each failed check and exits with the number of failures (0: all passed).
//
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#include "yaml-cpp/yaml.h"

namespace
{
int nChecks = 0, nFailures = 0;

void check(bool ok, const std::string & what, int line)
{
  nChecks++;
  if (ok) return;
  nFailures++;
  std::printf("FAILED (line %d): %s\n", line, what.c_str());
}

#define CHECK(c) check((c), #c, __LINE__)

const char * dataFile = R"(
independent_variables:
- header: {name: '$p_{T}$', units: GEV}
  values:
  - {low: 0.2, high: 0.4}
  - {low: 0.4, high: 0.6}
  - {low: 0.6, high: .inf}
dependent_variables:
- header: {name: '$dN/dp_{T}$', units: 'GEV**-1'}
  qualifiers:
  - {name: SQRT(S)/NUCLEON, units: GEV, value: 5020}
  - {name: CENTRALITY, value: 0-5%}
  values:
  - value: 12.3
    errors:
    - {symerror: 0.2, label: stat}
    - {asymerror: {plus: 0.5, minus: -0.4}, label: 'sys,tracking'}
  - value: 9.1e-1
    errors:
    - {symerror: 2%, label: stat}
    - {asymerror: {plus: '', minus: -0.3}, label: sys}
  - value: '-'
)";

const char * submission = R"(---
comment: A test submission
additional_resources:
- {location: 'https://example.org', description: web page}
---
name: Table 1
description: Spectrum of charged particles.
keywords:
- {name: reactions, values: [PB PB --> CHARGED X]}
- {name: cmenergies, values: [5020]}
data_file: data1.yaml
---
name: Table 2
description: Another table.
keywords: []
data_file: data2.yaml
)";
}

int main()
{
  try
    {
    // ---- a data file
    YAML::Node data = YAML::Load(dataFile);
    YAML::Node x = data["independent_variables"][0];
    CHECK(x["header"]["units"].as<std::string>() == "GEV");
    CHECK(x["values"].size() == 3);
    CHECK(x["values"][1]["low"].as<double>() == 0.4 && x["values"][1]["high"].as<double>() == 0.6);
    CHECK(std::isinf(x["values"][2]["high"].as<double>()));
    YAML::Node y = data["dependent_variables"][0];
    CHECK(y["qualifiers"][0]["value"].as<double>() == 5020.0);
    CHECK(y["qualifiers"][1]["value"].as<std::string>() == "0-5%");
    YAML::Node v = y["values"];
    CHECK(v.size() == 3);
    CHECK(v[0]["value"].as<double>() == 12.3);
    CHECK(v[0]["errors"][0]["symerror"].as<double>() == 0.2);
    CHECK(v[0]["errors"][1]["label"].as<std::string>() == "sys,tracking");
    CHECK(v[0]["errors"][1]["asymerror"]["plus"].as<double>() == 0.5);
    CHECK(v[0]["errors"][1]["asymerror"]["minus"].as<double>() == -0.4);
    CHECK(std::fabs(v[1]["value"].as<double>() - 0.91) < 1e-15);
    CHECK(v[1]["errors"][0]["symerror"].as<std::string>() == "2%");          // a percentage: read as text
    CHECK(v[1]["errors"][1]["asymerror"]["plus"].as<std::string>() == "");   // a one-sided error
    CHECK(v[2]["value"].as<std::string>() == "-");                           // a non-numeric value
    CHECK(!v[2]["errors"]);                                                   // no errors
    bool threw = false;
    try { v[2]["value"].as<double>(); } catch (const YAML::Exception &) { threw = true; }
    CHECK(threw);

    // ---- a multi-document submission.yaml
    std::vector<YAML::Node> documents = YAML::LoadAll(submission);
    CHECK(documents.size() == 3);
    CHECK(documents[0]["comment"].as<std::string>() == "A test submission");
    CHECK(documents[1]["name"].as<std::string>() == "Table 1");
    CHECK(documents[1]["keywords"][0]["values"][0].as<std::string>() == "PB PB --> CHARGED X");
    CHECK(documents[2]["data_file"].as<std::string>() == "data2.yaml");

    // ---- JSON is YAML too
    YAML::Node json = YAML::Load(R"({"independent_variables": [], "dependent_variables": [{"values": [{"value": 1.5}]}]})");
    CHECK(json["dependent_variables"][0]["values"][0]["value"].as<double>() == 1.5);

    // ---- writing, in the flow style of HEPData for bins and errors, and reading back
    YAML::Emitter out;
    out << YAML::BeginMap << YAML::Key << "independent_variables" << YAML::Value << YAML::BeginSeq;
    out << YAML::BeginMap << YAML::Key << "header" << YAML::Value << YAML::Flow << YAML::BeginMap
        << YAML::Key << "name" << YAML::Value << "x" << YAML::Key << "units" << YAML::Value << "GEV" << YAML::EndMap;
    out << YAML::Key << "values" << YAML::Value << YAML::BeginSeq;
    for (int k = 0; k < 3; k++)
      out << YAML::Flow << YAML::BeginMap << YAML::Key << "low" << YAML::Value << 0.1 * k
          << YAML::Key << "high" << YAML::Value << 0.1 * (k + 1) << YAML::EndMap;
    out << YAML::EndSeq << YAML::EndMap << YAML::EndSeq;
    out << YAML::Key << "dependent_variables" << YAML::Value << YAML::BeginSeq << YAML::BeginMap
        << YAML::Key << "values" << YAML::Value << YAML::BeginSeq;
    for (int k = 0; k < 3; k++)
      {
      out << YAML::BeginMap << YAML::Key << "value" << YAML::Value << 1.0 / 3.0 * (k + 1)
          << YAML::Key << "errors" << YAML::Value << YAML::BeginSeq
          << YAML::Flow << YAML::BeginMap << YAML::Key << "asymerror" << YAML::Value
          << YAML::Flow << YAML::BeginMap << YAML::Key << "plus" << YAML::Value << 0.25
          << YAML::Key << "minus" << YAML::Value << -0.125 << YAML::EndMap
          << YAML::Key << "label" << YAML::Value << "sys" << YAML::EndMap << YAML::EndSeq << YAML::EndMap;
      }
    out << YAML::EndSeq << YAML::EndMap << YAML::EndSeq << YAML::EndMap;
    CHECK(out.good());
    YAML::Node back = YAML::Load(out.c_str());
    CHECK(back["independent_variables"][0]["values"].size() == 3);
    CHECK(back["independent_variables"][0]["values"][2]["high"].as<double>() == 0.1 * 3);
    CHECK(back["dependent_variables"][0]["values"][1]["value"].as<double>() == 1.0 / 3.0 * 2);   // full precision
    CHECK(back["dependent_variables"][0]["values"][0]["errors"][0]["asymerror"]["minus"].as<double>() == -0.125);
    CHECK(std::string(out.c_str()).find("{low: 0, high: 0.1") != std::string::npos);            // flow style
    }
  catch (const YAML::Exception & e)
    {
    std::printf("FAILED: yaml-cpp exception: %s\n", e.what());
    nFailures++;
    }
  std::printf("TestYamlCpp: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
