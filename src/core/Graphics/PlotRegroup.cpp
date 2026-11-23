/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file PlotRegroup.cpp
//! @brief PlotDocument::planRegroup() and PlotDocument::regroup(): histograms and functions gathered into common graphs
//! by the pattern of their names.
#include <cmath>
#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <string>
#include "PlotDocument.hpp"

namespace Datum
{
  namespace GRAPHICS
  {

  namespace
  {
  //! One pattern: a regular expression, and whether it is matched against the whole path.
  struct Matcher
  {
    std::regex expression;
    bool wholePath;
    bool search;   // regular expression of the user: searched; wildcards: matched entirely
  };

  std::string trim(const std::string & s)
  {
  std::size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
  }

  //! Regular expression of a wildcard pattern: each * and ? a group, the rest literal.
  std::string wildcardExpression(const std::string & pattern)
  {
  std::string r = "^";
  for (char c : pattern)
    {
    if (c == '*') r += "(.*)";
    else if (c == '?') r += "(.)";
    else
      {
      if (std::string("\\^$.|+()[]{}").find(c) != std::string::npos) r += '\\';
      r += c;
      }
    }
  return r + "$";
  }

  std::vector<Matcher> matchers(const PlotDocument::RegroupOptions & options, String & error)
  {
  std::vector<Matcher> result;
  auto flags = std::regex::ECMAScript;
  if (!options.caseSensitive) flags |= std::regex::icase;
  try
    {
    if (options.regularExpression)
      {
      std::string p = trim(options.pattern.str());
      if (!p.empty()) result.push_back({ std::regex(p, flags), true, true });
      }
    else
      {
      std::string all = options.pattern.str(), piece;
      for (std::size_t i=0; i<=all.size(); i++)
        {
        if (i == all.size() || all[i] == ';')
          {
          std::string p = trim(piece);
          if (!p.empty()) result.push_back({ std::regex(wildcardExpression(p), flags), p.find('/') != std::string::npos, false });
          piece.clear();
          }
        else piece += all[i];
        }
      }
    }
  catch (const std::regex_error & e)
    {
    error = String("Not a valid pattern: ") + String(e.what());
    return {};
    }
  if (result.empty()) error = "Type a pattern, e.g. *_pt";
  return result;
  }

  std::string lastPart(const std::string & path)
  {
  std::size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
  }

  std::vector<std::string> split(const std::string & s, char separator)
  {
  std::vector<std::string> parts;
  std::string current;
  for (char c : s) { if (c == separator) { parts.push_back(current); current.clear(); } else current += c; }
  parts.push_back(current);
  return parts;
  }

  bool isItem(const XmlElement & e)
  {
  PlotDocument::Kind k = PlotDocument::kind(e);
  return k == PlotDocument::kHistogram || k == PlotDocument::kFunction;
  }

  //! An item found: its path, element, and those of its graph, canvas and window.
  struct Found
  {
    PlotDocument::Path path;
    const XmlElement * item;
    const XmlElement * graph;
    const XmlElement * canvas;
    const XmlElement * window;
    int graphIndex;   // among the graphs of the canvas
  };

  void collect(const XmlElement & e, PlotDocument::Path & path, const XmlElement * window, const XmlElement * canvas,
               const XmlElement * graph, int graphIndex, std::vector<Found> & found)
  {
  PlotDocument::Kind k = PlotDocument::kind(e);
  if (k == PlotDocument::kWindow) window = &e;
  if (k == PlotDocument::kCanvas) canvas = &e;
  if (k == PlotDocument::kGraph) graph = &e;
  if (isItem(e) && graph && canvas)
    {
    found.push_back({ path, &e, graph, canvas, window, graphIndex });
    return;
    }
  int graphs = 0;
  for (unsigned int i=0; i<e.nChildren(); i++)
    {
    const XmlElement & c = e.child(i);
    PlotDocument::Kind ck = PlotDocument::kind(c);
    if (ck != PlotDocument::kWindow && ck != PlotDocument::kCanvas && ck != PlotDocument::kGraph && !isItem(c)) continue;
    path.push_back(int(i));
    collect(c, path, window, canvas, graph, ck == PlotDocument::kGraph ? graphs : graphIndex, found);
    path.pop_back();
    if (ck == PlotDocument::kGraph) graphs++;
    }
  }

  //! Labels made of the parts of the "input/path" of the items that differ (common leading and trailing parts removed).
  std::vector<std::string> distinctParts(const std::vector<std::string> & keys)
  {
  std::vector<std::vector<std::string>> parts;
  for (const auto & k : keys) parts.push_back(split(k, '/'));
  // all of the same depth: the parts equal at a position for all are left out ("a/Ana/PiP/n1_pt" -> "a/PiP")
  bool sameDepth = !parts.empty();
  for (const auto & p : parts) if (p.size() != parts[0].size()) sameDepth = false;
  if (sameDepth)
    {
    std::vector<bool> common(parts[0].size(), true);
    for (std::size_t j=0; j<parts[0].size(); j++)
      for (const auto & p : parts) if (p[j] != parts[0][j]) { common[j] = false; break; }
    std::vector<std::string> labels;
    std::set<std::string> distinct;
    for (std::size_t i=0; i<parts.size(); i++)
      {
      std::string label;
      for (std::size_t j=0; j<parts[i].size(); j++) if (!common[j]) label += (label.empty() ? "" : "/") + parts[i][j];
      labels.push_back(label.empty() ? keys[i] : label);
      distinct.insert(labels.back());
      }
    if (distinct.size() == labels.size()) return labels;
    }
  std::size_t shortest = parts.empty() ? 0 : parts[0].size();
  for (const auto & p : parts) shortest = std::min(shortest, p.size());
  std::size_t lead = 0, trail = 0;
  while (lead < shortest)
    {
    bool same = true;
    for (const auto & p : parts) if (p[lead] != parts[0][lead]) { same = false; break; }
    if (!same) break;
    lead++;
    }
  while (trail + lead < shortest)
    {
    bool same = true;
    for (const auto & p : parts) if (p[p.size() - 1 - trail] != parts[0][parts[0].size() - 1 - trail]) { same = false; break; }
    if (!same) break;
    trail++;
    }
  std::vector<std::string> labels;
  for (std::size_t i=0; i<parts.size(); i++)
    {
    std::string label;
    for (std::size_t j=lead; j+trail<parts[i].size(); j++) label += (label.empty() ? "" : "/") + parts[i][j];
    labels.push_back(label.empty() ? keys[i] : label);
    }
  return labels;
  }

  //! Name of a canvas made from a pattern: its letters, digits and '_' ("group" if none).
  std::string nameFromPattern(const std::string & pattern)
  {
  std::string name;
  for (char c : pattern)
    {
    if (std::isalnum((unsigned char)c)) name += c;
    else if (!name.empty() && name.back() != '_') name += '_';
    }
  while (!name.empty() && name.back() == '_') name.pop_back();
  return name.empty() ? std::string("group") : name;
  }

  std::string text(const XmlElement & e, const char * attribute)
  {
  return e.hasAttribute(attribute) ? e.attribute(attribute).str() : std::string();
  }

  unsigned int indexOf(const XmlElement & parent, const XmlElement * child)
  {
  for (unsigned int i=0; i<parent.nChildren(); i++) if (&parent.child(i) == child) return i;
  return parent.nChildren();
  }
  //! Path of an element below a root (empty if not found).
  bool pathTo(const XmlElement & root, const XmlElement * target, PlotDocument::Path & path)
  {
  if (&root == target) return true;
  for (unsigned int i=0; i<root.nChildren(); i++)
    {
    path.push_back(int(i));
    if (pathTo(root.child(i), target, path)) return true;
    path.pop_back();
    }
  return false;
  }
  } // namespace

  void PlotDocument::fitGrid(XmlElement & e, int n)
  {
  if (n < 1 || (!e.hasAttribute("columns") && !e.hasAttribute("rows"))) return;
  long columns = e.hasAttribute("columns") ? std::max(1L, e.attributeInt("columns", 1)) : 0;
  long rows = e.hasAttribute("rows") ? std::max(1L, e.attributeInt("rows", 1)) : 0;
  long oldColumns = columns, oldRows = rows;
  if (columns == 0) columns = (n + rows - 1)/rows;   // rows only: columns that hold them
  if (oldColumns == 0) oldColumns = columns;
  if (oldRows == 0) oldRows = (n + columns - 1)/columns;
  if (columns > n) columns = n;
  if (n > oldColumns*oldRows)   // more than the grid holds: as square as the columns allow
    columns = std::max(columns, long(std::ceil(std::sqrt(double(n)) - 1e-9)));
  rows = (n + columns - 1)/columns;
  e.setAttribute("columns", columns);
  e.setAttribute("rows", rows);
  // the size follows the grid: each cell keeps its size
  auto scale = [&e](const char * name, long from, long to, long limit)
    {
    if (from == to || from < 1 || !e.hasAttribute(name)) return;
    long size = e.attributeInt(name, 0);
    if (size <= 0) return;
    long scaled = (size*to + from/2)/from;
    if (to > from) scaled = std::min(scaled, std::max(size, limit));
    e.setAttribute(name, std::max(100L, scaled));
    };
  // a window: its title bar is not part of the cells
  const long bar = e.isNamed("Window") ? 50 : 0;
  scale("width", oldColumns, columns, 1800);
  if (bar > 0 && e.hasAttribute("height") && oldRows != rows && oldRows > 0)
    {
    long height = e.attributeInt("height", 0);
    if (height > bar)
      {
      long scaled = ((height - bar)*rows + oldRows/2)/oldRows + bar;
      if (rows > oldRows) scaled = std::min(scaled, std::max(height, 1100L));
      e.setAttribute("height", std::max(100L, scaled));
      }
    }
  else if (bar == 0) scale("height", oldRows, rows, 1100);
  }

  PlotDocument::RegroupPlan PlotDocument::planRegroup(const RegroupOptions & options) const
  {
  RegroupPlan plan;
  std::vector<Matcher> patterns = matchers(options, plan.error);
  if (patterns.empty()) return plan;
  const XmlElement * scope = element(options.scope);
  if (!scope) { plan.error = "The element searched no longer exists"; return plan; }

  // the window, canvas and graph holding the scope, then the items below it
  const XmlElement * window = nullptr, * canvas = nullptr, * graph = nullptr;
  int graphIndex = 0;
  for (std::size_t n=0; n<=options.scope.size(); n++)
    {
    Path p(options.scope.begin(), options.scope.begin() + long(n));
    const XmlElement * e = element(p);
    if (!e) break;
    if (kind(*e) == kWindow) window = e;
    if (kind(*e) == kCanvas) canvas = e;
    if (kind(*e) == kGraph)
      {
      graph = e;
      graphIndex = 0;
      const XmlElement * c = element(parent(p));
      for (unsigned int i=0; c && i<unsigned(p.back()); i++) if (kind(c->child(i)) == kGraph) graphIndex++;
      }
    }
  std::vector<Found> found;
  Path path = options.scope;
  collect(*scope, path, window, canvas, graph, graphIndex, found);

  std::vector<std::vector<std::string>> captures;
  std::vector<const Found *> matched;
  for (const Found & f : found)
    {
    std::string name = kind(*f.item) == kFunction ? (f.item->hasAttribute("label") ? text(*f.item, "label") : text(*f.item, "class"))
                                                    : text(*f.item, "path");
    for (const Matcher & m : patterns)
      {
      // a wildcard pattern without '/': the name, then the whole path (e.g. "*KP*" matches "Ana/KP/n1_pt")
      std::string last = lastPart(name);
      std::smatch match;
      bool ok = false;
      if (m.search) ok = std::regex_search(name, match, m.expression);
      else if (!m.wholePath) ok = std::regex_match(last, match, m.expression);
      if (!ok && !m.search) ok = std::regex_match(name, match, m.expression);
      if (!ok) continue;
      std::vector<std::string> groups;
      for (std::size_t g=1; g<match.size(); g++)
        {
        // what a wildcard matched, without separators at its ends ("Ana/" -> "Ana")
        std::string group = match[g].str();
        std::size_t a = group.find_first_not_of("/_-. "), b = group.find_last_not_of("/_-. ");
        if (a != std::string::npos) groups.push_back(group.substr(a, b - a + 1));
        }
      captures.push_back(groups);
      matched.push_back(&f);
      RegroupEntry entry;
      entry.path = f.path;
      entry.name = String(name);
      std::string where = f.window ? text(*f.window, "title") : std::string();
      std::string canvasName = text(*f.canvas, "name");
      if (canvasName.empty()) canvasName = text(*f.canvas, "title");
      entry.location = String((where.empty() ? std::string() : where + " / ") + canvasName + " / graph "
                              + std::to_string(f.graphIndex + 1));
      plan.entries.push_back(entry);
      break;
      }
    }
  if (plan.entries.empty()) { plan.error = "Nothing matches the pattern"; return plan; }

  // labels of the items without one: what the wildcards matched, or the parts of input/path that differ
  if (options.labelItems)
    {
    std::vector<std::size_t> unlabeled;
    for (std::size_t i=0; i<matched.size(); i++) if (!matched[i]->item->hasAttribute("label")) unlabeled.push_back(i);
    std::vector<std::string> labels;
    std::set<std::string> distinct;
    bool usable = true;
    for (std::size_t i : unlabeled)
      {
      std::string label;
      for (const auto & g : captures[i]) label += (label.empty() ? "" : " ") + g;
      if (label.empty() || !distinct.insert(label).second) usable = false;
      labels.push_back(label);
      }
    if (!usable)
      {
      std::set<std::string> inputs;
      for (const Found * f : matched) inputs.insert(text(*f->item, "input"));
      std::vector<std::string> keys;
      for (std::size_t i : unlabeled)
        {
        const XmlElement & item = *matched[i]->item;
        std::string key = kind(item) == kFunction ? plan.entries[i].name.str() : text(item, "path");
        if (inputs.size() > 1) key = text(item, "input") + "/" + key;
        keys.push_back(key);
        }
      labels = distinctParts(keys);
      }
    for (std::size_t j=0; j<unlabeled.size(); j++) plan.entries[unlabeled[j]].label = String(labels[j]);
    }

  // graphs of at most itemsPerGraph items, canvases of at most graphsPerCanvas graphs
  int n = int(plan.entries.size());
  int perGraph = options.itemsPerGraph > 0 ? options.itemsPerGraph : n;
  for (int i=0; i<n; i+=perGraph)
    {
    std::vector<int> g;
    for (int j=i; j<std::min(n, i + perGraph); j++) g.push_back(j);
    plan.graphs.push_back(g);
    }
  int nGraphs = int(plan.graphs.size());
  int perCanvas = options.graphsPerCanvas > 0 ? options.graphsPerCanvas : nGraphs;
  for (int i=0; i<nGraphs; i+=perCanvas)
    {
    std::vector<int> c;
    for (int j=i; j<std::min(nGraphs, i + perCanvas); j++) c.push_back(j);
    plan.canvases.push_back(c);
    }

  // the window of the scope, or that of the first item
  const Found & first = *matched.front();
  const XmlElement * targetWindow = window ? window : first.window;
  if (!targetWindow) { plan.error = "The items are not in a window"; plan.entries.clear(); return plan; }
  for (std::size_t i=0; i<=first.path.size(); i++)
    {
    Path p(first.path.begin(), first.path.begin() + long(i));
    if (element(p) == targetWindow) { plan.window = p; break; }
    }

  // names of the new canvases, distinct from those of the document and from each other
  std::string base = options.canvasName.isNull() ? nameFromPattern(options.pattern.str()) : options.canvasName.str();
  std::vector<String> taken;
  for (std::size_t c=0; c<plan.canvases.size(); c++)
    {
    std::string wanted = c == 0 ? base : base + "_" + std::to_string(c + 1);
    String name = uniqueCanvasName(String(wanted));
    for (int k=2; std::find(taken.begin(), taken.end(), name) != taken.end(); k++)
      name = uniqueCanvasName(String(wanted + "_" + std::to_string(k)));
    taken.push_back(name);
    }
  plan.canvasNames = taken;

  // new windows of at most canvasesPerWindow canvases
  if (options.canvasesPerWindow > 0)
    {
    int nCanvases = int(plan.canvases.size());
    for (int i=0; i<nCanvases; i+=options.canvasesPerWindow)
      {
      std::vector<int> w;
      for (int j=i; j<std::min(nCanvases, i + options.canvasesPerWindow); j++) w.push_back(j);
      plan.windows.push_back(w);
      std::string title = base + (plan.windows.size() > 1 ? " " + std::to_string(plan.windows.size()) : std::string());
      plan.windowTitles.push_back(String(title));
      }
    }
  return plan;
  }

  PlotDocument::Path PlotDocument::regroup(const RegroupOptions & options, RegroupPlan * planOut)
  {
  RegroupPlan plan = planRegroup(options);
  if (planOut) *planOut = plan;
  if (!plan.error.isNull() || plan.entries.empty()) return Path();
  record();

  // the elements (pointers stay valid when other children are added or removed)
  std::vector<XmlElement *> items, graphs, canvases;
  for (const RegroupEntry & e : plan.entries)
    {
    items.push_back(element(e.path));
    graphs.push_back(element(parent(e.path)));
    canvases.push_back(element(parent(parent(e.path))));
    }
  XmlElement * window = element(plan.window);

  // style of the new graphs: the graph of the first item without its items, cell, file and title
  XmlElement graphStyle = *graphs.front();
  for (int i=int(graphStyle.nChildren())-1; i>=0; i--) if (isItem(graphStyle.child(unsigned(i)))) graphStyle.removeChild(unsigned(i));
  for (const char * a : { "cell", "file", "title" }) graphStyle.removeAttribute(a);
  if (!options.graphTitle.isNull()) graphStyle.setAttribute("title", options.graphTitle);
  // style of the new canvases: the canvas of the first item without its graphs and layout
  XmlElement canvasStyle = *canvases.front();
  for (int i=int(canvasStyle.nChildren())-1; i>=0; i--) if (kind(canvasStyle.child(unsigned(i))) == kGraph) canvasStyle.removeChild(unsigned(i));
  for (const char * a : { "name", "title", "file", "columns", "rows" }) canvasStyle.removeAttribute(a);

  // the new canvases
  std::vector<XmlElement> newCanvases;
  for (std::size_t c=0; c<plan.canvases.size(); c++)
    {
    XmlElement canvas = canvasStyle;
    canvas.setAttribute("name", plan.canvasNames[c]);
    canvas.setAttribute("title", plan.canvasNames[c]);
    canvas.setAttribute("file", plan.canvasNames[c] + String(".pdf"));
    for (int g : plan.canvases[c])
      {
      XmlElement graph = graphStyle;
      for (int i : plan.graphs[std::size_t(g)])
        {
        XmlElement item = *items[std::size_t(i)];
        if (options.labelItems && !item.hasAttribute("label") && !plan.entries[std::size_t(i)].label.isNull())
          item.setAttribute("label", plan.entries[std::size_t(i)].label);
        graph.addChild(item);
        }
      canvas.addChild(graph);
      }
    newCanvases.push_back(canvas);
    }
  // placed after the canvas of the first item (or at the end of its window), or in new windows after its window
  std::vector<XmlElement *> changedWindows;
  XmlElement * firstCanvas = nullptr;
  XmlElement & root = _document.root();
  if (plan.windows.empty())
    {
    unsigned int at = window->nChildren();
    if (canvases.front() && indexOf(*window, canvases.front()) < window->nChildren()) at = indexOf(*window, canvases.front()) + 1;
    unsigned int first = at;
    for (const XmlElement & canvas : newCanvases) window->insertChild(at++, canvas);
    firstCanvas = &window->child(first);
    changedWindows.push_back(window);
    }
  else
    {
    XmlElement windowStyle = *window;
    for (int i=int(windowStyle.nChildren())-1; i>=0; i--) if (kind(windowStyle.child(unsigned(i))) == kCanvas) windowStyle.removeChild(unsigned(i));
    for (const char * a : { "title", "columns", "rows" }) windowStyle.removeAttribute(a);
    unsigned int at = indexOf(root, window) + 1;
    for (std::size_t w=0; w<plan.windows.size(); w++)
      {
      XmlElement newWindow = windowStyle;
      newWindow.setAttribute("title", plan.windowTitles[w]);
      for (int c : plan.windows[w]) newWindow.addChild(newCanvases[std::size_t(c)]);
      XmlElement & added = root.insertChild(at++, newWindow);
      if (!firstCanvas) firstCanvas = &added.child(added.nChildren() - unsigned(plan.windows[w].size()));
      }
    }

  // the items leave their graphs; graphs and canvases left empty are removed
  std::set<XmlElement *> removedGraphs, removedCanvases;
  for (std::size_t i=0; i<items.size(); i++) graphs[i]->removeChild(indexOf(*graphs[i], items[i]));
  if (options.removeEmpty)
    {
    for (std::size_t i=0; i<graphs.size(); i++)
      {
      XmlElement * g = graphs[i];
      if (removedGraphs.count(g)) continue;
      bool empty = true;
      for (unsigned int j=0; j<g->nChildren() && empty; j++) if (isItem(g->child(j))) empty = false;
      if (!empty) continue;
      canvases[i]->removeChild(indexOf(*canvases[i], g));
      removedGraphs.insert(g);
      }
    for (std::size_t i=0; i<canvases.size(); i++)
      {
      XmlElement * c = canvases[i];
      if (removedCanvases.count(c) || !c->children("Graph").empty()) continue;
      for (unsigned int j=0; j<root.nChildren(); j++)   // its window
        {
        XmlElement & w = root.child(j);
        unsigned int k = indexOf(w, c);
        if (k < w.nChildren()) { w.removeChild(k); changedWindows.push_back(&w); break; }
        }
      removedCanvases.insert(c);
      }
    }

  // grids fitted to what the windows and canvases changed hold
  for (std::size_t i=0; i<canvases.size(); i++)
    if (!removedCanvases.count(canvases[i])) fitGrid(*canvases[i], int(canvases[i]->children("Graph").size()));
  for (XmlElement * w : changedWindows) fitGrid(*w, int(w->children("Canvas").size()));
  changed();
  Path result;
  pathTo(root, firstCanvas, result);
  return result;
  }

  } // namespace GRAPHICS
} // namespace Datum
