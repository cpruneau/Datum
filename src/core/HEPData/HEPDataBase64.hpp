/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file HEPDataBase64.hpp
//! @brief Base64 encoding (RFC 4648) of the binary payloads kept in the datum blocks of HEPData tables (internal to the
//! HEPData library: binary records of functions and fits, from which they are read back exactly).
#ifndef DATUM_HEPDataBase64
#define DATUM_HEPDataBase64
#include <string>
#include "Exceptions.hpp"

namespace Datum
{
namespace HEPDataBase64
{
//! @brief Base64 text of bytes. @param bytes bytes @return text (with '=' padding)
inline std::string encode(const std::string & bytes)
{
  static const char * table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string text;
  text.reserve(4*((bytes.size()+2)/3));
  std::size_t i = 0;
  for (; i+2<bytes.size(); i+=3)
    {
    unsigned int v = (unsigned char)(bytes[i])<<16 | (unsigned char)(bytes[i+1])<<8 | (unsigned char)(bytes[i+2]);
    text += table[(v>>18)&63]; text += table[(v>>12)&63]; text += table[(v>>6)&63]; text += table[v&63];
    }
  if (i<bytes.size())
    {
    unsigned int v = (unsigned char)(bytes[i])<<16;
    if (i+1<bytes.size()) v |= (unsigned char)(bytes[i+1])<<8;
    text += table[(v>>18)&63]; text += table[(v>>12)&63];
    text += i+1<bytes.size() ? table[(v>>6)&63] : '=';
    text += '=';
    }
  return text;
}

//! @brief Bytes of base64 text (white space ignored). @param text text @return bytes
//! @throws Exception if the text is not base64
inline std::string decode(const std::string & text)
{
  std::string bytes;
  unsigned int v = 0;
  int bits = 0, padding = 0;
  for (char c : text)
    {
    int d;
    if (c>='A' && c<='Z') d = c-'A';
    else if (c>='a' && c<='z') d = c-'a'+26;
    else if (c>='0' && c<='9') d = c-'0'+52;
    else if (c=='+') d = 62;
    else if (c=='/') d = 63;
    else if (c=='=') { padding++; continue; }
    else if (c==' ' || c=='\n' || c=='\r' || c=='\t') continue;
    else throw Exception("Invalid base64 text","HEPDataBase64::decode()");
    if (padding>0) throw Exception("Invalid base64 text","HEPDataBase64::decode()");
    v = (v<<6) | unsigned(d);
    bits += 6;
    if (bits>=8) { bits -= 8; bytes += char((v>>bits)&0xFF); }
    }
  if (padding>2) throw Exception("Invalid base64 text","HEPDataBase64::decode()");
  return bytes;
}
} // namespace HEPDataBase64
} // namespace Datum

#endif
