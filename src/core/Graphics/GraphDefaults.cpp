/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file GraphDefaults.cpp
//! @brief Implementation of GraphDefaults.

#include "GraphDefaults.hpp"
#include "Graph.hpp"
#include "XmlElement.hpp"

namespace Datum
{
  namespace GRAPHICS
  {

  namespace
  {
  const char * justificationName(TextAttributes::Justification j)
  {
  return j == TextAttributes::kLeft ? "left" : (j == TextAttributes::kRight ? "right" : "center");
  }

  TextAttributes::Justification justificationOf(const String & name, TextAttributes::Justification otherwise)
  {
  if (name == "left") return TextAttributes::kLeft;
  if (name == "right") return TextAttributes::kRight;
  if (name == "center" || name == "centre") return TextAttributes::kCenter;
  return otherwise;
  }

  void applyText(const GraphDefaults::Text & t, TextAttributes & text)
  {
  if (t.font > 0) text.setFont(t.font);
  if (t.color >= 0) text.setColor(t.color, text.alpha());
  }

  void readText(const XmlElement * e, GraphDefaults::Text & t)
  {
  if (!e) return;
  t.font  = int(e->attributeInt("font", t.font));
  t.size  = float(e->attributeDouble("size", t.size));
  t.color = int(e->attributeInt("color", t.color));
  }

  void writeText(XmlElement & e, const GraphDefaults::Text & t)
  {
  e.setAttribute("font", t.font);
  e.setAttribute("size", double(t.size));
  e.setAttribute("color", t.color);
  }
  } // namespace

  GraphDefaults & GraphDefaults::session()
  {
  static GraphDefaults defaults;
  return defaults;
  }

  void GraphDefaults::apply(Graph & graph) const
  {
  applyText(title, graph.titleAttributes());
  graph.setTitlePointSize(title.size);
  for (Axis * axis : { &graph.xAxis(), &graph.yAxis(), &graph.zAxis(), &graph.residualAxis() })
    {
    applyText(axisTitles, axis->titleAttributes());
    axis->setTitlePointSize(axisTitles.size);
    applyText(axisLabels, axis->textAttributes());
    axis->setLabelPointSize(axisLabels.size);
    axis->setNMajorTicks(nDivisions < 1 ? 1 : nDivisions);
    axis->setTitleAlignment(axisTitleAlignment);
    }
  applyText(legend, graph.legendTextAttributes());
  graph.legendTextAttributes().setJustification(legendJustification);
  graph.setLegendPointSize(legend.size);
  graph.legendFill().set(legendFillColor, 1.0, legendBackground ? 1001 : 0);
  graph.legendLine().set(legendLineColor, 1.0, legendBorder ? (legendLineStyle < 1 ? 1 : legendLineStyle) : 0, legendLineWidth);
  }

  void GraphDefaults::read(const XmlElement & e)
  {
  readText(e.child("Title"), title);
  readText(e.child("AxisTitles"), axisTitles);
  readText(e.child("AxisLabels"), axisLabels);
  readText(e.child("Legend"), legend);
  if (const XmlElement * axes = e.child("Axes"))
    {
    nDivisions = int(axes->attributeInt("divisions", nDivisions));
    axisTitleAlignment = justificationOf(axes->attribute("titlePlacement", ""), axisTitleAlignment);
    }
  if (const XmlElement * box = e.child("LegendBox"))
    {
    legendBackground    = box->attributeBool("background", legendBackground);
    legendFillColor     = int(box->attributeInt("backgroundColor", legendFillColor));
    legendBorder        = box->attributeBool("border", legendBorder);
    legendLineStyle     = int(box->attributeInt("borderStyle", legendLineStyle));
    legendLineWidth     = float(box->attributeDouble("borderWidth", legendLineWidth));
    legendLineColor     = int(box->attributeInt("borderColor", legendLineColor));
    legendJustification = justificationOf(box->attribute("justification", ""), legendJustification);
    }
  }

  void GraphDefaults::write(XmlElement & e) const
  {
  writeText(e.addChild("Title"), title);
  writeText(e.addChild("AxisTitles"), axisTitles);
  writeText(e.addChild("AxisLabels"), axisLabels);
  writeText(e.addChild("Legend"), legend);
  XmlElement & axes = e.addChild("Axes");
  axes.setAttribute("divisions", nDivisions);
  axes.setAttribute("titlePlacement", justificationName(axisTitleAlignment));
  XmlElement & box = e.addChild("LegendBox");
  box.setAttribute("background", legendBackground);
  box.setAttribute("backgroundColor", legendFillColor);
  box.setAttribute("border", legendBorder);
  box.setAttribute("borderStyle", legendLineStyle);
  box.setAttribute("borderWidth", double(legendLineWidth));
  box.setAttribute("borderColor", legendLineColor);
  box.setAttribute("justification", justificationName(legendJustification));
  }

  } // namespace GRAPHICS
} // namespace Datum
