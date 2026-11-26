/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file Compression.hpp
//! @brief Compression of the records of Datum files (zlib).
#ifndef DATUM__Compression
#define DATUM__Compression
#include <cstddef>
#include <string>

namespace Datum
{

//!
//! @brief Compression of the payloads of the records of Datum files, with zlib (deflate).
//!
//! A record whose payload is at least threshold() bytes long is written compressed when that makes it at least 10%
//! smaller; its version byte then has its high bit set (0x80), and its stored payload is the length of the payload as
//! a varint followed by the deflated payload. The checksum of the record is that of the stored bytes, so a file is
//! checked without decompressing it. Folders are never compressed (their entries are read one by one), only the
//! objects in them. Records are decompressed when they are read, whatever the settings.
//!
//! Datum built without zlib writes uncompressed records and refuses compressed ones with an exception.
//!
class Compression
{
public:
  //! @brief Whether zlib is available. @return true if records can be compressed and decompressed
  static bool available();
  //! @brief Whether records are written compressed (true by default when available()). @return true if so
  static bool enabled();
  //! @brief Write records compressed or not. @param enabled compress (ignored when zlib is not available)
  static void setEnabled(bool enabled);
  //! @brief Smallest payload compressed, in bytes (4096 by default). @return threshold
  static std::size_t threshold();
  //! @brief Set the smallest payload compressed. @param bytes threshold
  static void setThreshold(std::size_t bytes);
  //! @brief Compression level, 1 (fastest) to 9 (smallest) (6 by default). @return level
  static int level();
  //! @brief Set the compression level. @param level 1 to 9
  static void setLevel(int level);

  //!
  //! @brief Deflate bytes (zlib format). @param data bytes @param size number of bytes @param out compressed bytes
  //! @return false if zlib is not available or the compression failed
  //!
  static bool deflate(const char * data, std::size_t size, std::string & out);
  //!
  //! @brief Inflate bytes (zlib format) into exactly expected bytes. @param data compressed bytes @param size their
  //! number @param expected size of the result @param out the bytes @return false if the data is invalid, its size is
  //! not the one expected, or zlib is not available
  //!
  static bool inflate(const char * data, std::size_t size, std::size_t expected, std::string & out);
};

} // namespace Datum

#endif // DATUM__Compression
