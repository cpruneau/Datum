/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file GraphDefaults.hpp
//! @brief GraphDefaults: the settings every new graph starts with (fonts and sizes of its texts, axes, legend box).
#ifndef DATUM__GRAPHICS__GraphDefaults
#define DATUM__GRAPHICS__GraphDefaults

#include "TextAttributes.hpp"

namespace Datum
{
  class XmlElement;

  namespace GRAPHICS
  {
  class Graph;

  //!
  //! @brief The settings every new graph starts with: font, size and color of its title, axis titles, axis labels and
  //! legend; number of divisions of the axes and placement of their titles; background, border and justification of
  //! the legend. Graph's constructor applies those of the session (session()), which programs and DatumGui
  //! (Preferences > Canvases) change; a plot file or a program can still set any of them for a graph.
  //!
  //! Fonts are ROOT font codes (see TextAttributes: 42 Helvetica, 132 Times, 82 Courier, ...; 0: the default text
  //! attributes of the program); sizes are in points, 0 meaning the automatic size (proportional to the canvas);
  //! colors are color indices (-1: the default text attributes). The defaults are those of Graph before this class
  //! existed: nothing changes until they are changed.
  //!
  class GraphDefaults
  {
  public:
  //! @brief Font, size and color of a kind of text.
  struct Text
  {
    int   font  = 0;     //!< ROOT font code (42: Helvetica, ...; 0: that of TextAttributes::defaultTextAttributes())
    float size  = 0.0f;  //!< size in points (0: automatic)
    int   color = -1;    //!< color index (-1: that of TextAttributes::defaultTextAttributes())
  };

  //! @brief The defaults of the session (used by the new graphs). @return defaults
  static GraphDefaults & session();

  Text title;        //!< title of the graph
  Text axisTitles;   //!< titles of the axes
  Text axisLabels;   //!< labels of the axes
  Text legend;       //!< texts of the legend
  int  nDivisions = 5;   //!< number of divisions of the axes (major ticks, about)
  TextAttributes::Justification axisTitleAlignment = TextAttributes::kCenter;   //!< axis titles: start, center or end of the axis
  // the legend box
  bool  legendBackground = false;   //!< a background (otherwise transparent)
  int   legendFillColor  = 0;       //!< its color (0: white)
  bool  legendBorder     = false;   //!< a border
  int   legendLineStyle  = 1;       //!< its line style (1 solid, 2 dashed, ...)
  float legendLineWidth  = 1.0f;    //!< its width (points)
  int   legendLineColor  = 1;       //!< its color
  TextAttributes::Justification legendJustification = TextAttributes::kLeft;   //!< labels of the legend

  //! @brief Give a graph these settings (fonts, sizes, axes, legend). @param graph graph
  void apply(Graph & graph) const;
  //! @brief Read the settings from an XML element written by write() (missing attributes unchanged). @param e element
  void read(const XmlElement & e);
  //! @brief Write the settings to an XML element (attributes and children). @param e element
  void write(XmlElement & e) const;
  };

  } // namespace GRAPHICS
} // namespace Datum

#endif // !DATUM__GRAPHICS__GraphDefaults
