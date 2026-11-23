/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
#include <algorithm>
#include <cmath>
#include <string>
#include "PlotTemplate.hpp"
#include "CanvasRegistry.hpp"
#include "DataPoints.hpp"
#include "Folder.hpp"
#include "Histogram.hpp"

namespace Datum
{
  namespace GRAPHICS
  {

  int PlotTemplate::addInput(const String & id, const String & file)
  {
  _inputs.push_back(Input{id, file});
  return int(_inputs.size()) - 1;
  }

  int PlotTemplate::addFolder(int input, const Folder & top)
  {
  int n = 0;
  for (const String & path : top.paths())
    {
    std::string type = top.typeName(path);
    if (type.find("Histogram") == std::string::npos && type.find("Profile") == std::string::npos
        && type.find("DataPoints") == std::string::npos) continue;
    const MATH::DataPoints * data = nullptr;
    try { data = top.get<MATH::DataPoints>(path); } catch (...) { data = nullptr; }
    if (!data) continue;
    const MATH::Histogram * histogram = dynamic_cast<const MATH::Histogram *>(data);
    int rank = int(histogram ? histogram->rank() : data->rank());
    if (rank > 2) continue;
    // task / combination / name
    std::vector<std::string> parts;
    std::string p = path.str();
    for (std::size_t start = 0; start <= p.size();)
      {
      std::size_t end = p.find('/', start);
      if (end == std::string::npos) end = p.size();
      if (end > start) parts.push_back(p.substr(start, end - start));
      start = end + 1;
      }
    if (parts.empty()) continue;
    Entry e;
    e.input = input;
    e.path = path;
    e.rank = rank;
    e.name = String(parts.back());
    if (parts.size() >= 2) e.task = String(parts.front());
    if (parts.size() >= 3)
      {
      std::string c;
      for (std::size_t k=1; k+1<parts.size(); k++) { if (!c.empty()) c += "/"; c += parts[k]; }
      e.combination = String(c);
      }
    auto found = _byPath.find(p);
    if (found != _byPath.end()) { _entries[found->second].input = input; _entries[found->second].rank = rank; }
    else
      {
      _byPath[p] = _entries.size();
      _entries.push_back(e);
      if (std::find(_tasks.begin(), _tasks.end(), e.task) == _tasks.end()) _tasks.push_back(e.task);
      }
    n++;
    }
  return n;
  }

  std::vector<String> PlotTemplate::names(const String & task) const
  {
  std::vector<String> result;
  std::set<std::string> seen;
  for (const auto & e : _entries) if (e.task == task && seen.insert(e.name.str()).second) result.push_back(e.name);
  return result;
  }

  std::vector<String> PlotTemplate::combinations(const String & task) const
  {
  std::vector<String> result;
  std::set<std::string> seen;
  for (const auto & e : _entries) if (e.task == task && seen.insert(e.combination.str()).second) result.push_back(e.combination);
  return result;
  }

  int PlotTemplate::rankOf(const String & task, const String & name) const
  {
  for (const auto & e : _entries) if (e.task == task && e.name == name) return e.rank;
  return 1;
  }

  void PlotTemplate::setNameSelected(const String & task, const String & name, bool v)
  {
  if (v) _unselectedNames.erase(key(task, name)); else _unselectedNames.insert(key(task, name));
  }

  void PlotTemplate::setCombinationSelected(const String & task, const String & c, bool v)
  {
  if (v) _unselectedCombinations.erase(key(task, c)); else _unselectedCombinations.insert(key(task, c));
  }

  void PlotTemplate::selectAll(const String & task, bool v)
  {
  for (const String & t : _tasks)
    {
    if (!task.isNull() && !(t == task)) continue;
    for (const String & n : names(t)) setNameSelected(t, n, v);
    for (const String & c : combinations(t)) setCombinationSelected(t, c, v);
    }
  }

  bool PlotTemplate::matches(const String & text, const String & pattern)
  {
  const std::string & s = text.str(), & p = pattern.str();
  std::size_t i = 0, j = 0, star = std::string::npos, mark = 0;
  while (i < s.size())
    {
    if (j < p.size() && (p[j] == '?' || p[j] == s[i])) { i++; j++; }
    else if (j < p.size() && p[j] == '*') { star = j++; mark = i; }
    else if (star != std::string::npos) { j = star + 1; i = ++mark; }
    else return false;
    }
  while (j < p.size() && p[j] == '*') j++;
  return j == p.size();
  }

  void PlotTemplate::select(const std::vector<String> & include, const std::vector<String> & exclude)
  {
  auto matchOne = [](const Entry & e, const String & pattern)
    {
    return pattern.str().find('/') == std::string::npos ? matches(e.name, pattern) : matches(e.path, pattern);
    };
  selectAll("", false);
  for (const auto & e : _entries)
    {
    bool in = include.empty();
    for (const String & p : include) if (matchOne(e, p)) { in = true; break; }
    for (const String & p : exclude) if (matchOne(e, p)) { in = false; break; }
    if (!in) continue;
    setNameSelected(e.task, e.name, true);
    setCombinationSelected(e.task, e.combination, true);
    }
  }

  int PlotTemplate::nSelected() const
  {
  int n = 0;
  for (const auto & e : _entries) if (isSelected(e)) n++;
  return n;
  }

  namespace
  {
  //! A canvas to write: name, title, graphs (each a list of entries, overlaid)
  struct CanvasPlan { int group = 0; String groupTitle; String name; String title; std::vector<String> graphTitles; std::vector<std::vector<const PlotTemplate::Entry *>> graphs; };

  int clampInt(int v, int low, int high) { return std::max(low, std::min(high, v)); }
  } // namespace

  XmlDocument PlotTemplate::makeSpec(const Options & options) const
  {
  XmlDocument document("PlotHistos");
  XmlElement & root = document.root();
  root.setAttribute("version", 1);
  // the inputs of the entries plotted (a workflow stage whose histograms are all written again by a later one is not one)
  std::vector<bool> used(_inputs.size(), false);
  for (const auto & e : _entries) if (isSelected(e) && e.input >= 0 && e.input < int(_inputs.size())) used[std::size_t(e.input)] = true;
  for (std::size_t k=0; k<_inputs.size(); k++)
    {
    if (!used[k]) continue;
    const Input & input = _inputs[k];
    XmlElement & e = root.addChild("Input");
    e.setAttribute("id", input.id);
    e.setAttribute("file", input.file);
    }
  XmlElement & output = root.addChild("Output");
  output.setAttribute("directory", options.outputDirectory);
  output.setAttribute("pdf", "plots.pdf");
  output.setAttribute("pptx", "plots.pptx");
  output.setAttribute("latex", "plots_latex.tex");
  output.setAttribute("latexStyle", "beamer");
  output.setAttribute("title", options.title);

  const int maxGraphs = std::max(1, options.maxGraphs), maxOverlay = std::max(1, options.maxOverlay);
  const int maxCanvases = std::max(1, options.maxCanvases);
  std::set<std::string> canvasNames;
  auto uniqueName = [&](const String & wanted)
    {
    String base = CanvasRegistry::fileName(wanted.isNull() ? String("canvas") : wanted), name = base;
    for (int k=2; canvasNames.count(name.str()); k++) name = base + String("_") + String(std::to_string(k));
    canvasNames.insert(name.str());
    return name;
    };
  for (const String & task : _tasks)
    {
    // the entries of the task selected, by name and by combination
    std::vector<String> names = this->names(task), combos = combinations(task);
    std::map<std::string, const Entry *> byKey;
    for (const auto & e : _entries) if (e.task == task && isSelected(e)) byKey[key(e.name, e.combination)] = &e;
    if (byKey.empty()) continue;
    std::vector<CanvasPlan> canvases;
    int groupIndex = 0;   // related canvases (a family, a histogram name or a combination): kept in one window if possible
    // canvases of a list of graphs, at most maxGraphs each
    auto addCanvases = [&](const String & title, const String & name, const std::vector<std::vector<const Entry *>> & graphs,
                           const std::vector<String> & titles, const String & groupTitle)
      {
      int nCanvases = (int(graphs.size()) + maxGraphs - 1)/maxGraphs;
      for (int c=0; c<nCanvases; c++)
        {
        CanvasPlan plan;
        plan.group = groupIndex;
        plan.groupTitle = groupTitle;
        String suffix = nCanvases > 1 ? String(" (") + String(std::to_string(c + 1)) + String("/") + String(std::to_string(nCanvases)) + String(")") : String("");
        plan.title = title + suffix;
        plan.name = uniqueName((task.isNull() ? String("") : task + String("_")) + name
                               + (nCanvases > 1 ? String("_") + String(std::to_string(c + 1)) : String("")));
        for (int g=c*maxGraphs; g<std::min(int(graphs.size()), (c + 1)*maxGraphs); g++)
          {
          plan.graphs.push_back(graphs[std::size_t(g)]);
          plan.graphTitles.push_back(titles[std::size_t(g)]);
          }
        canvases.push_back(plan);
        }
      };
    if (options.grouping == Options::kByFamily)
      {
      // families of related names: same rank and same first word (the text before the first '_', e.g. "n1" of
      // "n1_pt"); the names alone in their family share the family "others" of their rank
      std::vector<String> selectedCombos;
      for (const String & cb : combos)
        for (const String & n : names) if (byKey.count(key(n, cb))) { selectedCombos.push_back(cb); break; }
      std::vector<std::string> order;
      std::map<std::string, std::vector<String>> members;
      for (const String & n : names)
        {
        bool any = false;
        for (const String & cb : selectedCombos) if (byKey.count(key(n, cb))) { any = true; break; }
        if (!any) continue;
        std::string s = n.str(), word = s.substr(0, s.find('_'));
        std::string k = std::to_string(rankOf(task, n)) + "|" + word;
        if (!members.count(k)) order.push_back(k);
        members[k].push_back(n);
        }
      std::vector<std::string> families;
      std::map<std::string, std::vector<String>> grouped;
      for (const std::string & k : order)
        {
        std::string f = members[k].size() > 1 || order.size() == 1 ? k : k.substr(0, k.find('|') + 1);   // "rank|": others
        if (!grouped.count(f)) families.push_back(f);
        for (const String & n : members[k]) grouped[f].push_back(n);
        }
      bool severalRanks = false;
      for (const std::string & f : families) if (f.substr(0, f.find('|')) != families.front().substr(0, families.front().find('|'))) severalRanks = true;
      const bool severalCombos = selectedCombos.size() > 1;
      for (const std::string & f : families)
        {
        const std::vector<String> & group = grouped[f];
        std::string rank = f.substr(0, f.find('|')), word = f.substr(f.find('|') + 1);
        const bool others = word.empty() && members.size() > 1;
        if (word.empty()) word = group.size() == 1 ? group.front().str() : (others ? "others" : "all");
        if (others && severalRanks && group.size() > 1) word += rank == "2" ? " 2D" : " 1D";
        String familyTitle = task.isNull() ? String(word) : task + String(": ") + String(word);
        String familyName = String(word);
        bool overlay = options.overlay && rank == "1" && severalCombos;
        std::vector<std::vector<const Entry *>> graphs;
        std::vector<String> titles;
        if (overlay)
          for (const String & n : group)
            {
            std::vector<const Entry *> all;
            for (const String & cb : selectedCombos) { auto found = byKey.find(key(n, cb)); if (found != byKey.end()) all.push_back(found->second); }
            for (std::size_t k=0; k<all.size(); k += std::size_t(maxOverlay))
              {
              graphs.emplace_back(all.begin() + long(k), all.begin() + long(std::min(all.size(), k + std::size_t(maxOverlay))));
              titles.push_back(n);
              }
            }
        else
          {
          // a graph per name and combination; too many for a canvas: a canvas (or more) per combination
          int n = 0;
          for (const String & name : group) for (const String & cb : selectedCombos) if (byKey.count(key(name, cb))) n++;
          bool byCombo = severalCombos && n > maxGraphs;
          for (const String & cb : selectedCombos)
            {
            for (const String & name : group)
              {
              auto found = byKey.find(key(name, cb));
              if (found == byKey.end()) continue;
              graphs.push_back({ found->second });
              titles.push_back(severalCombos && !byCombo ? name + String(" ") + cb : name);
              }
            if (byCombo && !graphs.empty())
              {
              addCanvases(familyTitle + String(" ") + cb, familyName + String("_") + cb, graphs, titles, familyTitle);
              graphs.clear();
              titles.clear();
              }
            }
          if (!byCombo && severalCombos)
            {
            // name first, then combination
            std::vector<std::vector<const Entry *>> sorted;
            std::vector<String> sortedTitles;
            for (const String & name : group)
              for (std::size_t k=0; k<graphs.size(); k++)
                if (graphs[k].front()->name == name) { sorted.push_back(graphs[k]); sortedTitles.push_back(titles[k]); }
            graphs.swap(sorted);
            titles.swap(sortedTitles);
            }
          }
        if (!graphs.empty()) addCanvases(familyTitle, familyName, graphs, titles, familyTitle);
        groupIndex++;
        }
      }
    else
    {
    const std::vector<String> & outer = options.grouping == Options::kByHistogram ? names : combos;
    const std::vector<String> & inner = options.grouping == Options::kByHistogram ? combos : names;
    for (const String & o : outer)
      {
      std::vector<const Entry *> group;
      for (const String & i : inner)
        {
        auto found = byKey.find(options.grouping == Options::kByHistogram ? key(o, i) : key(i, o));
        if (found != byKey.end()) group.push_back(found->second);
        }
      if (group.empty()) continue;
      bool overlay = options.overlay && options.grouping == Options::kByHistogram && group.front()->rank == 1;
      // the graphs: one entry each, or overlays of up to maxOverlay entries
      std::vector<std::vector<const Entry *>> graphs;
      std::vector<String> titles;
      int step = overlay ? maxOverlay : 1;
      for (std::size_t k=0; k<group.size(); k += std::size_t(step))
        {
        std::vector<const Entry *> g(group.begin() + long(k), group.begin() + long(std::min(group.size(), k + std::size_t(step))));
        graphs.push_back(g);
        titles.push_back(overlay ? o : (options.grouping == Options::kByHistogram ? g.front()->combination : g.front()->name));
        }
      String title = o.isNull() ? (task.isNull() ? String("histograms") : task) : o;
      addCanvases(title, o.isNull() ? String("all") : o, graphs, titles, task.isNull() || o.isNull() ? title : task + String(": ") + o);
      groupIndex++;
      }
    }
    // windows of at most maxCanvases canvases; a group of related canvases is not split across two windows unless it
    // fills more than one window alone
    std::vector<std::pair<int,int>> ranges;   // first canvas, number of canvases
    for (std::size_t k=0; k<canvases.size(); )
      {
      std::size_t end = k;
      while (end < canvases.size() && canvases[end].group == canvases[k].group) end++;
      int n = int(end - k);
      if (!ranges.empty() && ranges.back().second + n <= maxCanvases && (ranges.back().second < maxCanvases))
        ranges.back().second += n;
      else if (!ranges.empty() && ranges.back().second < maxCanvases && n > maxCanvases)
        { int fill = maxCanvases - ranges.back().second; ranges.back().second += fill; k += std::size_t(fill); continue; }
      else
        for (int first=0; first<n; first += maxCanvases) ranges.push_back({ int(k) + first, std::min(maxCanvases, n - first) });
      k = end;
      }
    const int nWindows = int(ranges.size());
    for (int w=0; w<nWindows; w++)
      {
      int first = ranges[std::size_t(w)].first, n = ranges[std::size_t(w)].second;
      int columns = n <= 3 ? std::max(1, n) : (n == 4 ? 2 : 3), rows = (n + columns - 1)/columns;
      XmlElement & window = root.addChild("Window");
      String title = task.isNull() ? String("Histograms") : task;
      bool oneGroup = nWindows > 1 && canvases[std::size_t(first)].group == canvases[std::size_t(first + n - 1)].group
                      && (first == 0 || canvases[std::size_t(first - 1)].group != canvases[std::size_t(first)].group)
                      && (first + n == int(canvases.size()) || canvases[std::size_t(first + n)].group != canvases[std::size_t(first)].group);
      if (oneGroup) title = canvases[std::size_t(first)].groupTitle;   // a window of its own for related canvases
      else if (nWindows > 1) title += String(" (") + String(std::to_string(w + 1)) + String("/") + String(std::to_string(nWindows)) + String(")");
      window.setAttribute("title", title);
      window.setAttribute("width", std::min(1700, 620*columns));
      window.setAttribute("height", std::min(1050, 470*rows + 50));
      window.setAttribute("columns", columns);
      window.setAttribute("rows", rows);
      for (int k=first; k<first + n; k++)
        {
        const CanvasPlan & plan = canvases[std::size_t(k)];
        int nGraphs = int(plan.graphs.size());
        int gc = int(std::ceil(std::sqrt(double(nGraphs)))), gr = (nGraphs + gc - 1)/gc;
        XmlElement & canvas = window.addChild("Canvas");
        canvas.setAttribute("name", plan.name);
        canvas.setAttribute("title", plan.title);
        canvas.setAttribute("width", clampInt(400*gc, 700, 1600));
        canvas.setAttribute("height", clampInt(330*gr, 500, 1200));
        canvas.setAttribute("columns", gc);
        canvas.setAttribute("rows", gr);
        if (options.printFiles) canvas.setAttribute("file", plan.name + String(".pdf"));
        for (int g=0; g<nGraphs; g++)
          {
          XmlElement & graph = canvas.addChild("Graph");
          graph.setAttribute("title", plan.graphTitles[std::size_t(g)]);
          for (const Entry * e : plan.graphs[std::size_t(g)])
            {
            XmlElement & item = graph.addChild("Histogram");
            item.setAttribute("input", _inputs.empty() ? String("") : _inputs[std::size_t(e->input)].id);
            item.setAttribute("path", e->path);
            bool several = plan.graphs[std::size_t(g)].size() > 1;
            item.setAttribute("label", several && options.grouping != Options::kByCombination ? e->combination : e->name);
            }
          }
        }
      }
    }
  return document;
  }

  } // namespace GRAPHICS
} // namespace Datum
