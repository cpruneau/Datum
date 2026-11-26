/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file TestHistogramStorage.cpp
//! @brief Test program of the storage of histograms in Datum files (run by ctest).
//
// TestHistogramStorage -- histograms of counts (unit weights) written as their numbers of entries only and read back
// with averages 1 and uncertainties 1/sqrt(n); other histograms (weights, scaled, double precision) written in full
// and read back exactly; 32-bit numbers of entries (more is held at the largest, the value kept; 64-bit counts of
// older files); compressed records (zlib) read back exactly, eagerly and lazily, folders not compressed, small or
// incompressible records not compressed, and records written without compression. Prints each failed check and exits
// with the number of failures (0: all passed).
//
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include "BinaryIO.hpp"
#include "Compression.hpp"
#include "DatumFile.hpp"
#include "FitRecord.hpp"
#include "Histogram.hpp"
#include "Profile.hpp"

using namespace Datum;
using namespace Datum::MATH;

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
#define CHECK(condition, what) check((condition), (what), __LINE__)

template <class T> std::string record(const T & object) { std::ostringstream out; object.writeBinary(out); return out.str(); }
template <class T> T readBack(const std::string & bytes) { T object; std::istringstream in(bytes); object.readBinary(in); return object; }

//! Whether two histograms have the same entries, averages and uncertainties, bit for bit.
bool identical(const Histogram & a, const Histogram & b)
{
  if (a.size()!=b.size() || a.precision()!=b.precision()) return false;
  for (unsigned int k=0; k<a.size(); k++)
    if (a.binEntriesAtGlobal(k)!=b.binEntriesAtGlobal(k) || a.binAvgValueAtGlobal(k)!=b.binAvgValueAtGlobal(k)
        || a.binAvgValueUncertaintyAtGlobal(k)!=b.binAvgValueUncertaintyAtGlobal(k)) return false;
  return true;
}

Histogram gaussian(const char * name, long n, Histogram::Precision precision, double weight=1.0)
{
  std::mt19937 g(7);
  std::normal_distribution<double> gauss(0,1);
  Histogram h(name, name, 40, -4.0, 4.0, 36, -3.2, 3.2);
  h.setPrecision(precision);
  for (long k=0; k<n; k++) h.fill(gauss(g), gauss(g), weight);
  return h;
}

void testCounts()
{
  for (Histogram::Precision precision : { Histogram::kFloat, Histogram::kDouble })
    {
    Histogram h = gaussian("counts", 200000, precision);
    CHECK(h.holdsCounts(), "unit weights: counts");
    std::string bytes = record(h);
    Histogram back = readBack<Histogram>(bytes);
    bool entries = true, values = true, uncertainties = true;
    for (unsigned int k=0; k<h.size(); k++)
      {
      long n = h.binEntriesAtGlobal(k);
      if (back.binEntriesAtGlobal(k)!=n) entries = false;
      if (back.binValueAtGlobal(k)!=h.binValueAtGlobal(k)) values = false;
      double expected = n>0 ? std::sqrt(double(n)) : 0.0;
      if (std::fabs(back.binUncertaintyAtGlobal(k)-expected) > 1e-6*std::max(1.0,expected)) uncertainties = false;
      }
    CHECK(entries && values, "counts: entries and values read back exactly");
    CHECK(uncertainties, "counts: uncertainty sqrt(n)");
    CHECK(back.precision()==precision, "counts: precision kept");
    // ~1440 bins: a few bytes each, compressed further
    CHECK(bytes.size() < h.size()*4, "counts: a few bytes per bin (" + std::to_string(bytes.size()) + " bytes)");
    }
  // empty: no bin written
  Histogram empty("e", "e", 10, 0.0, 1.0);
  CHECK(identical(empty, readBack<Histogram>(record(empty))), "empty histogram");
}

void testFull()
{
  // weights, a scaled histogram, double precision: written in full, read back bit for bit
  Histogram weighted = gaussian("weighted", 50000, Histogram::kFloat, 0.7);
  CHECK(!weighted.holdsCounts(), "weights: not counts");
  CHECK(identical(weighted, readBack<Histogram>(record(weighted))), "weighted histogram read back exactly");
  Histogram scaled = gaussian("scaled", 50000, Histogram::kDouble);
  scaled.scale(1.0/3.0);
  CHECK(!scaled.holdsCounts(), "scaled: not counts");
  CHECK(identical(scaled, readBack<Histogram>(record(scaled))), "scaled histogram read back exactly");
  Histogram mixed("mixed", "mixed", 3, 0.0, 3.0);
  mixed.fill(0.5); mixed.fill(1.5, 2.0);
  CHECK(!mixed.holdsCounts() && identical(mixed, readBack<Histogram>(record(mixed))), "one weighted bin: written in full");
  // a profile (averages of values) keeps its content
  Profile p("p", "p", 4, 0.0, 1.0);
  for (int k=0; k<100; k++) p.fill(0.01*k, 1.0 + 0.1*k, 1.0);
  Profile pb = readBack<Profile>(record(p));
  CHECK(pb.binValueAtGlobal(2)==p.binValueAtGlobal(2) && pb.binUncertaintyAtGlobal(3)==p.binUncertaintyAtGlobal(3), "profile read back");
}

void testCountsLimit()
{
  Histogram h("big", "big", 2, 0.0, 2.0);
  h.setBinAtGlobal(0, 3000000000L, 6.0e9, 1.0e5);   // more entries than 32 bits: the largest count, the value kept
  CHECK(h.binEntriesAtGlobal(0)==Histogram::kMaximumCount, "entries held at the largest count");
  CHECK(std::fabs(h.binValueAtGlobal(0)-6.0e9)/6.0e9 < 1.0e-6, "value of the bin kept (to the precision of the average)");
  // a record of an older file with a 64-bit count (version 6)
  BinaryWriter w;
  w.writeString("old");                      // name
  w.writeString("old");                      // title
  w.writeVarUInt(0); w.writeVarUInt(1);      // type, rank
  w.writeVarUInt(2); w.writeFloat32(0.0f); w.writeFloat32(2.0f); w.writeString("x");
  w.writeUInt8(1);                           // double precision
  w.writeVarUInt(1);                         // one bin
  w.writeVarUInt(1); w.writeVarInt(5000000000LL); w.writeFloat64(2.0); w.writeFloat64(1.0e-5);
  w.writeVarUInt(0);                         // labels
  FitList().encodeBinary(w);                 // no fit
  w.writeUInt8(0);                           // common systematic
  w.writeVarUInt(0);                         // variable axes
  std::ostringstream out;
  writeBinaryRecord(out, "DTMH", 6, w.buffer(), "old");
  Histogram old;
  bool read = false;
  try { old = readBack<Histogram>(out.str()); read = true; } catch (const std::exception & e) { std::printf("%s\n", e.what()); }
  CHECK(read && old.binEntriesAtGlobal(1)==Histogram::kMaximumCount && std::fabs(old.binValueAtGlobal(1)-1.0e10)/1.0e10 < 1e-12,
        "64-bit count of an older file: the largest count, the value kept");
}

void testCompression()
{
  CHECK(Compression::available(), "zlib available");
  Histogram weighted = gaussian("w", 200000, Histogram::kFloat, 0.5);
  Compression::setEnabled(false);
  std::string plain = record(weighted);
  Compression::setEnabled(true);
  std::string packed = record(weighted);
  CHECK(packed.size() < plain.size(), "compressed: " + std::to_string(plain.size()) + " -> " + std::to_string(packed.size()) + " bytes");
  CHECK((unsigned char)packed[4] & 0x80, "version byte marks the compression");
  CHECK(identical(weighted, readBack<Histogram>(packed)), "compressed record read back exactly");
  CHECK(identical(weighted, readBack<Histogram>(plain)), "uncompressed record read back");
  Histogram small("small", "small", 10, 0.0, 1.0);
  small.fill(0.3, 0.5);
  CHECK(!((unsigned char)record(small)[4] & 0x80), "small record not compressed");

  // a Datum file: folders not compressed, objects read eagerly and lazily
  const char * path = "storage_test.datum";
  {
  DatumFile file(path, File::kWriteOnly);
  Folder * f = file.addFolder("results");
  f->add(new Histogram(weighted), "w");
  f->add(new Histogram(gaussian("c", 100000, Histogram::kDouble)), "c");
  file.write();
  }
  {
  DatumFile eager(path, File::kReadOnly);
  eager.read();
  const Histogram * w = eager.get<Histogram>("results/w");
  CHECK(w && identical(*w, weighted), "Datum file, eager: compressed histogram read back");
  CHECK(eager.get<Histogram>("results/c") && eager.get<Histogram>("results/c")->holdsCounts(), "Datum file, eager: counts read back");
  }
  {
  DatumFile lazy(path, File::kReadOnly);
  lazy.load(File::Loading::kLazy);
  const Histogram * w = lazy.get<Histogram>("results/w");
  CHECK(w && identical(*w, weighted), "Datum file, lazy: compressed histogram read back");
  }
  // a corrupted compressed record is refused
  std::string corrupted = packed;
  corrupted[corrupted.size()/2] ^= 0x55;
  bool refused = false;
  try { readBack<Histogram>(corrupted); } catch (const std::exception &) { refused = true; }
  CHECK(refused, "corrupted compressed record refused");
  std::remove(path);
}
} // namespace

int main()
{
  testCounts();
  testFull();
  testCountsLimit();
  testCompression();
  std::printf("TestHistogramStorage: %d checks, %d failed\n", nChecks, nFailures);
  return nFailures;
}
