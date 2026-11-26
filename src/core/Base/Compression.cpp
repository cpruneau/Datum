/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file Compression.cpp
//! @brief Implementation of Compression.

#include "Compression.hpp"
#include <algorithm>
#include <limits>
#ifdef DATUM_WITH_ZLIB
#include <zlib.h>
#endif

namespace Datum
{

namespace
{
#ifdef DATUM_WITH_ZLIB
bool compressing = true;
#else
bool compressing = false;
#endif
std::size_t smallest = 4096;
int compressionLevel = 6;
} // namespace

bool Compression::available()
{
#ifdef DATUM_WITH_ZLIB
  return true;
#else
  return false;
#endif
}

bool Compression::enabled()                    { return compressing && available(); }
void Compression::setEnabled(bool v)           { compressing = v; }
std::size_t Compression::threshold()           { return smallest; }
void Compression::setThreshold(std::size_t b)  { smallest = b; }
int Compression::level()                       { return compressionLevel; }
void Compression::setLevel(int l)              { compressionLevel = std::max(1, std::min(9, l)); }

bool Compression::deflate(const char * data, std::size_t size, std::string & out)
{
#ifdef DATUM_WITH_ZLIB
  if (size > std::size_t(std::numeric_limits<uLong>::max())) return false;
  uLongf length = compressBound(uLong(size));
  out.resize(std::size_t(length));
  int status = compress2(reinterpret_cast<Bytef *>(&out[0]), &length, reinterpret_cast<const Bytef *>(data), uLong(size),
                         compressionLevel);
  if (status != Z_OK) { out.clear(); return false; }
  out.resize(std::size_t(length));
  return true;
#else
  (void)data; (void)size; out.clear();
  return false;
#endif
}

bool Compression::inflate(const char * data, std::size_t size, std::size_t expected, std::string & out)
{
#ifdef DATUM_WITH_ZLIB
  if (size > std::size_t(std::numeric_limits<uLong>::max()) || expected > std::size_t(std::numeric_limits<uLong>::max())) return false;
  out.resize(expected);
  uLongf length = uLongf(expected);
  int status = uncompress(reinterpret_cast<Bytef *>(expected ? &out[0] : nullptr), &length,
                          reinterpret_cast<const Bytef *>(data), uLong(size));
  if (status != Z_OK || std::size_t(length) != expected) { out.clear(); return false; }
  return true;
#else
  (void)data; (void)size; (void)expected; out.clear();
  return false;
#endif
}

} // namespace Datum
