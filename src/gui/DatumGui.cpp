/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file DatumGui.cpp
//! @brief Implementation of DatumGui.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include "imgui.h"
#include "FileSystem.hpp"
#include "imgui_internal.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
//! Silence the deprecation warnings of OpenGL on macOS (also defined by CMake for DatumImGui).
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>
#include "DatumGui.hpp"
#include "CanvasRegistry.hpp"
#include "ImGuiPainter.hpp"
#include "LatexRenderer.hpp"
#include "LatexTextures.hpp"
#include "SvgPainter.hpp"
#include "PdfPainter.hpp"
#include <functional>

#ifndef DATUM_SOURCE_ROOT
//! Top directory of the Datum sources (set by the build; empty if not).
#define DATUM_SOURCE_ROOT ""
#endif

namespace Datum
{
  namespace GUI
  {

  DatumGui::DatumGui()
  :
  onNewPane(),
  beforePanesChange(),
  onFrame(),
  onCloseRequest(),
  _window(nullptr),
  _settings(),
  _font(nullptr),
  _menuBar(),
  _fileDialog(),
  _panes(),
  _columns(0), _rows(0), _currentPane(0),
  _paneCounter(0),
  _status(),
  _statusTime(0),
  _messageTitle(), _messageText(),
  _messageRequested(false),
  _exitRequested(false),
  _areaX(0), _areaY(0), _areaWidth(0), _areaHeight(0),
  _iniFile(),
  _hoverText(),
  _browsers(),
  _activeBrowser(0),
  _browserCounter(1),
  _firstFrame(true),
  _layoutRequested(false),
  _undockRequested(false),
  _gridColumns(1),
  _dockBeside(),
  _closeRequests(),
  _dataPanes(),
  _dataCounter(0),
  _addRequested(false),
  _focusedPane(-1),
  _focusRequested(false)
  {
  _fitPane.onNewFit = [this]() { openFitDialog(); };
  _browsers.emplace_back(new FileBrowser(1));          // the first browser, always there
  }

  DatumGui::~DatumGui()
  {
  if (_window)
    {
    GRAPHICS::LatexRenderer::instance().setReadyCallback(nullptr);
    LatexTextures::instance().clear();                  // while the OpenGL context exists
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(_window);
    glfwTerminate();
    }
  }

  namespace
  {
  void glfwError(int code, const char * description)
  {
  std::fprintf(stderr, "DatumGui: GLFW error %d: %s\n", code, description);
  }

  //! First existing file among the candidates, or "" (see FileSystem::firstExisting()).
  std::string firstExisting(const std::vector<std::string> & candidates)
  {
  return FileSystem::firstExisting(std::vector<String>(candidates.begin(), candidates.end())).str();
  }
  } // namespace

  bool DatumGui::initialize(const GuiSettings & settings)
  {
  _settings = settings;
  glfwSetErrorCallback(glfwError);
#ifdef __APPLE__
  glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);   // keep the current folder (DatumGui.app starts in the home folder)
#endif
  if (!glfwInit()) return false;

#if defined(__APPLE__)
  const char * glslVersion = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
  const char * glslVersion = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
  _window = glfwCreateWindow(_settings.width, _settings.height, _settings.title.data(), nullptr, nullptr);
  if (!_window) { glfwTerminate(); return false; }
  glfwMakeContextCurrent(_window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO & io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
  if (_settings.viewports) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  _iniFile = _settings.iniFile.str();
  io.IniFilename = _iniFile.empty() ? nullptr : _iniFile.c_str();
  setTheme(_settings.theme);
  loadFonts();
  ImGui_ImplGlfw_InitForOpenGL(_window, true);
  ImGui_ImplOpenGL3_Init(glslVersion);
  // texts typeset by LaTeX in the background: wake up the event loop to draw them
  GRAPHICS::LatexRenderer::instance().setReadyCallback([]() { glfwPostEmptyEvent(); });
  if (_settings.startEmpty) { _columns = _rows = _gridColumns = 1; }   // the browser only: canvases made when needed
  else setLayout(_settings.columns, _settings.rows);
  return true;
  }

  void DatumGui::loadFonts()
  {
  ImGuiIO & io = ImGui::GetIO();
  std::vector<std::string> candidates;
  candidates.push_back(_settings.fontFile.str());
  candidates.push_back(FileSystem::expand("$DATUM_ROOT_PATH/external/imgui/misc/fonts/Roboto-Medium.ttf").str());
  candidates.push_back(std::string(DATUM_SOURCE_ROOT) + "/external/imgui/misc/fonts/Roboto-Medium.ttf");
  std::string file = firstExisting(candidates);
  _font = nullptr;
  if (!file.empty()) _font = io.Fonts->AddFontFromFileTTF(file.c_str(), _settings.fontSize);
  if (!_font)
    {
    ImFontConfig config;
    config.SizePixels = _settings.fontSize;
    _font = io.Fonts->AddFontDefault(&config);
    }

  // fonts of the plots, by ROOT font index (see TextAttributes): Helvetica is the interface font, its other styles
  // and the Times and Courier families come from the system (macOS, or Liberation fonts on Linux) when available
  std::vector<std::string> directories{ "/System/Library/Fonts/Supplemental/", "/Library/Fonts/",
                                        "/usr/share/fonts/truetype/liberation/", "/usr/share/fonts/liberation-sans/",
                                        "/usr/share/fonts/liberation-serif/", "/usr/share/fonts/liberation-mono/",
                                        "/usr/share/fonts/truetype/msttcorefonts/" };
  struct PlotFont { int index; std::vector<std::string> names; };
  const std::vector<PlotFont> plotFonts = {
    {  5, { "Arial Italic.ttf",                "LiberationSans-Italic.ttf" } },
    {  6, { "Arial Bold.ttf",                  "LiberationSans-Bold.ttf" } },
    {  7, { "Arial Bold Italic.ttf",           "LiberationSans-BoldItalic.ttf" } },
    { 13, { "Times New Roman.ttf",             "LiberationSerif-Regular.ttf" } },
    {  1, { "Times New Roman Italic.ttf",      "LiberationSerif-Italic.ttf" } },
    {  2, { "Times New Roman Bold.ttf",        "LiberationSerif-Bold.ttf" } },
    {  3, { "Times New Roman Bold Italic.ttf", "LiberationSerif-BoldItalic.ttf" } },
    {  8, { "Courier New.ttf",                 "LiberationMono-Regular.ttf" } },
    {  9, { "Courier New Italic.ttf",          "LiberationMono-Italic.ttf" } },
    { 10, { "Courier New Bold.ttf",            "LiberationMono-Bold.ttf" } },
    { 11, { "Courier New Bold Italic.ttf",     "LiberationMono-BoldItalic.ttf" } } };
  ImGuiPainter::setFont(4, _font);
  for (const auto & plotFont : plotFonts)
    {
    std::vector<std::string> files;
    for (const auto & name : plotFont.names) for (const auto & directory : directories) files.push_back(directory + name);
    std::string fontFile = firstExisting(files);
    ImFont * font = fontFile.empty() ? nullptr : io.Fonts->AddFontFromFileTTF(fontFile.c_str(), _settings.fontSize);
    ImGuiPainter::setFont(plotFont.index, font);
    }
  // Courier without a system font: the monospaced font distributed with Dear ImGui
  if (!ImGuiPainter::fontFor(82))
    {
    std::string cousine = firstExisting({ FileSystem::expand("$DATUM_ROOT_PATH/external/imgui/misc/fonts/Cousine-Regular.ttf").str(),
                                          std::string(DATUM_SOURCE_ROOT) + "/external/imgui/misc/fonts/Cousine-Regular.ttf" });
    if (!cousine.empty()) ImGuiPainter::setFont(8, io.Fonts->AddFontFromFileTTF(cousine.c_str(), _settings.fontSize));
    }
  }

  void DatumGui::setTheme(const String & theme)
  {
  _settings.theme = theme;
  if      (theme == "dark")    ImGui::StyleColorsDark();
  else if (theme == "classic") ImGui::StyleColorsClassic();
  else                         ImGui::StyleColorsLight();
  ImGuiStyle & style = ImGui::GetStyle();
  style.WindowRounding = 0.0f;
  style.FrameRounding  = 3.0f;
  style.ChildBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;   // check boxes and input fields stay visible on white backgrounds
  if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }

  CanvasPane & DatumGui::newPane()
  {
  char name[32];
  std::snprintf(name, sizeof(name), "Canvas %d", ++_paneCounter);
  _panes.emplace_back(new CanvasPane(name));
  CanvasPane & pane = *_panes.back();
  pane.setShowToolbar(_settings.toolbars);
  pane.onAddPane = [this]() { _addRequested = true; };
  CanvasPane * added = &pane;
  pane.onClosePane = [this, added]()
    {
    for (int k=0; k<nPanes(); k++) if (_panes[std::size_t(k)].get() == added) { requestClosePane(k); break; }
    };
  if (onNewPane) onNewPane(pane);
  return pane;
  }

  void DatumGui::setLayout(int nColumns, int nRows)
  {
  nColumns = nColumns < 1 ? 1 : (nColumns > 6 ? 6 : nColumns);
  nRows    = nRows    < 1 ? 1 : (nRows    > 6 ? 6 : nRows);
  int n = nColumns*nRows;
  // the panes of the current window (main or new window; those of the windows of canvases stay)
  const int h = layoutHost();
  auto nMain = [this, h]() { int m = 0; for (const auto & p : _panes) if (p->host() == h) m++; return m; };
  while (nMain() > n)
    for (int k=nPanes()-1; k>=0; k--) if (_panes[k]->host() == h) { _panes.erase(_panes.begin() + k); break; }
  while (nMain() < n) newPane().setHost(h);
  _columns = nColumns;
  _rows    = nRows;
  _gridColumns = nColumns;
  if (h == kMainWindow) _layoutRequested = true;
  else { _windows[std::size_t(-2 - h)].columns = nColumns; _windows[std::size_t(-2 - h)].layoutPending = true; }
  if (_currentPane >= nPanes()) _currentPane = 0;
  }

  void DatumGui::setPaneCount(int n)
  {
  n = n < 0 ? 0 : (n > 36 ? 36 : n);
  const int h = layoutHost();
  auto nMain = [this, h]() { int m = 0; for (const auto & p : _panes) if (p->host() == h) m++; return m; };
  if (n == nMain()) return;
  while (nMain() > n)
    for (int k=nPanes()-1; k>=0; k--) if (_panes[k]->host() == h) { _panes.erase(_panes.begin() + k); break; }
  while (nMain() < n) newPane().setHost(h);
  _gridColumns = std::max(1, int(std::ceil(std::sqrt(double(n)))));
  _columns = _gridColumns;
  _rows    = (n + _gridColumns - 1)/_gridColumns;
  if (h == kMainWindow) _layoutRequested = true;
  else { _windows[std::size_t(-2 - h)].columns = _gridColumns; _windows[std::size_t(-2 - h)].layoutPending = true; }
  if (_currentPane >= nPanes()) _currentPane = 0;
  }

  void DatumGui::setPaneGrid(int nColumns, int n)
  {
  n = n < 1 ? 1 : (n > 36 ? 36 : n);
  nColumns = std::min(nColumns < 1 ? 1 : (nColumns > 6 ? 6 : nColumns), n);
  int rows = (n + nColumns - 1)/nColumns;
  const int h = layoutHost();
  auto nMain = [this, h]() { int m = 0; for (const auto & p : _panes) if (p->host() == h) m++; return m; };
  if (n == nMain() && _gridColumns == nColumns && _columns == nColumns && _rows == rows) return;
  while (nMain() > n)
    for (int k=nPanes()-1; k>=0; k--) if (_panes[k]->host() == h) { _panes.erase(_panes.begin() + k); break; }
  while (nMain() < n) newPane().setHost(h);
  _gridColumns = nColumns;
  _columns = nColumns;
  _rows = rows;
  if (h == kMainWindow) _layoutRequested = true;
  else { _windows[std::size_t(-2 - h)].columns = nColumns; _windows[std::size_t(-2 - h)].layoutPending = true; }
  if (_currentPane >= nPanes()) _currentPane = 0;
  }

  CanvasPane & DatumGui::addPane()
  {
  validateCurrentWindow();
  std::string beside = paneOfCurrentWindow();
  if (beside.empty()) return addPaneIn(_currentWindow);
  CanvasPane & pane = newPane();
  if (_currentWindow <= -2) pane.setHost(_currentWindow);
  _dockBeside.push_back(std::make_pair(std::string(pane.windowId()), beside));
  setCurrentPane(nPanes() - 1);
  return pane;
  }

  CanvasPane & DatumGui::currentPane()
  {
  if (_panes.empty()) return targetPane();
  if (_currentPane < 0 || _currentPane >= nPanes()) _currentPane = 0;
  return *_panes[std::size_t(_currentPane)];
  }

  // ------------------------------------------------------------------------------------------------ DatumGui windows

  namespace
  {
  ImGuiViewport * gActiveViewport = nullptr;   //!< viewport of the current DatumGui window (this frame)
  }

  ImGuiViewport * DatumGui::activeViewport()
  {
  if (gActiveViewport) return gActiveViewport;
  return ImGui::GetMainViewport();
  }

  unsigned int DatumGui::dockspaceOf(int window) const
  {
  if (window == kMainWindow) return ImHashStr("DatumDockSpace");
  if (window >= 0) return ImHashStr((std::string("DatumPaneWindowDock") + std::to_string(window)).c_str());
  std::size_t j = std::size_t(-2 - window);
  int number = j < _windows.size() ? _windows[j].number : 0;
  return ImHashStr((std::string("DatumWindowDock") + std::to_string(number)).c_str());
  }

  ImGuiViewport * DatumGui::viewportOf(int window) const
  {
  if (!ImGui::GetCurrentContext()) return nullptr;
  if (window == kMainWindow) return ImGui::GetMainViewport();
  ImGuiWindow * w = nullptr;
  if (window >= 0 && window < nPaneWindows()) w = ImGui::FindWindowByName(_paneWindows[std::size_t(window)].label.c_str());
  else if (window <= -2 && std::size_t(-2 - window) < _windows.size())
    w = ImGui::FindWindowByName(_windows[std::size_t(-2 - window)].label.c_str());
  return w ? w->Viewport : nullptr;
  }

  int DatumGui::windowOfImGui(const ImGuiWindow * window) const
  {
  if (!window || !window->Viewport) return kOutside;
  auto found = _viewportWindows.find(window->Viewport->ID);
  return found == _viewportWindows.end() ? kOutside : found->second;
  }

  int DatumGui::windowOf(const char * name) const
  {
  if (!ImGui::GetCurrentContext() || !name) return kOutside;
  return windowOfImGui(ImGui::FindWindowByName(name));
  }

  int DatumGui::windowOfPane(int k) const
  {
  if (k < 0 || k >= nPanes()) return kOutside;
  const CanvasPane & pane = *_panes[std::size_t(k)];
  ImGuiWindow * window = ImGui::GetCurrentContext() ? ImGui::FindWindowByName(pane.windowId()) : nullptr;
  if (!window || !window->Viewport) return pane.host();   // not drawn yet: where it was made
  return windowOfImGui(window);
  }

  bool DatumGui::paneWindowShown(int k) const
  {
  for (const auto & p : _panes) if (p->host() == k) return true;
  return false;
  }

  bool DatumGui::windowOpen(int window) const
  {
  if (window == kMainWindow) return !_mainHidden;
  if (window >= 0) return window < nPaneWindows() && _paneWindows[std::size_t(window)].gui && paneWindowShown(window);
  std::size_t j = std::size_t(-2 - window);
  return j < _windows.size() && _windows[j].open;
  }

  int DatumGui::nOpenWindows() const
  {
  int n = _mainHidden ? 0 : 1;
  for (const auto & w : _windows) if (w.open) n++;
  for (int k=0; k<nPaneWindows(); k++) if (windowOpen(k)) n++;
  return n;
  }

  void DatumGui::validateCurrentWindow()
  {
  if (windowOpen(_currentWindow)) return;
  if (!_mainHidden) { _currentWindow = kMainWindow; return; }
  for (std::size_t j=0; j<_windows.size(); j++) if (_windows[j].open) { _currentWindow = -2 - int(j); return; }
  for (int k=0; k<nPaneWindows(); k++) if (windowOpen(k)) { _currentWindow = k; return; }
  _currentWindow = kMainWindow;
  }

  void DatumGui::setCurrentWindow(int window)
  {
  if (!windowOpen(window)) return;
  _currentWindow = window;
  if (_panes.empty() || windowOfPane(_currentPane) == window) return;
  for (int k=0; k<nPanes(); k++) if (windowOfPane(k) == window) { _currentPane = k; break; }
  }

  int DatumGui::layoutHost() const
  {
  if (_currentWindow <= -2 && windowOpen(_currentWindow)) return _currentWindow;
  return kMainWindow;
  }

  std::string DatumGui::paneOfCurrentWindow() const
  {
  if (_currentPane >= 0 && _currentPane < nPanes() && windowOfPane(_currentPane) == _currentWindow)
    return _panes[std::size_t(_currentPane)]->windowId();
  for (int k=0; k<nPanes(); k++) if (windowOfPane(k) == _currentWindow) return _panes[std::size_t(k)]->windowId();
  return std::string();
  }

  void DatumGui::noteFocus(const char * name)
  {
  int w = windowOf(name);
  if (w != kOutside && windowOpen(w)) _currentWindow = w;
  }

  CanvasPane & DatumGui::addPaneIn(int window)
  {
  CanvasPane & pane = newPane();
  if (window <= -2) pane.setHost(window);
  _dockInto.push_back(DockRequest{ pane.windowId(), window, false, 0 });
  _currentPane = nPanes() - 1;
  _focusRequested = true;
  return pane;
  }

  CanvasPane & DatumGui::targetPane()
  {
  validateCurrentWindow();
  if (!_panes.empty() && _currentPane >= 0 && _currentPane < nPanes() && windowOfPane(_currentPane) == _currentWindow)
    return *_panes[std::size_t(_currentPane)];
  for (int k=0; k<nPanes(); k++) if (windowOfPane(k) == _currentWindow) { _currentPane = k; return *_panes[std::size_t(k)]; }
  return addPaneIn(_currentWindow);
  }

  int DatumGui::toolWindow()
  {
  // the window of canvases of a plot file is laid out again when its canvases change: tools go elsewhere
  validateCurrentWindow();
  if (_currentWindow < 0) return _currentWindow;
  if (!_mainHidden) return kMainWindow;
  for (std::size_t j=0; j<_windows.size(); j++) if (_windows[j].open) return -2 - int(j);
  return _currentWindow;
  }

  void DatumGui::dockInCurrentWindow(const std::string & name, bool left)
  {
  _dockInto.push_back(DockRequest{ name, toolWindow(), left, 0 });
  }

  bool DatumGui::needsPlace(const char * name) const
  {
  ImGuiWindow * w = ImGui::GetCurrentContext() ? ImGui::FindWindowByName(name) : nullptr;
  if (!w) return true;
  if (w->DockNode)
    {
    ImGuiDockNode * root = ImGui::DockNodeGetRootNode(w->DockNode);
    if (root && root->IsDockSpace())
      {
      // docked in the dock space of a DatumGui window: lost if that window is closed (or the main one hidden)
      if (root->ID == dockspaceOf(kMainWindow)) return _mainHidden;
      for (std::size_t j=0; j<_windows.size(); j++) if (root->ID == dockspaceOf(-2 - int(j))) return !_windows[j].open;
      for (int k=0; k<nPaneWindows(); k++) if (root->ID == dockspaceOf(k)) return !paneWindowShown(k);
      }
    return false;
    }
  return _mainHidden && w->Viewport == ImGui::GetMainViewport();   // floating over the hidden main window
  }

  void DatumGui::setPaneWindowGui(int k, bool gui)
  {
  if (k >= 0 && k < nPaneWindows()) _paneWindows[std::size_t(k)].gui = gui;
  }

  int DatumGui::newWindow()
  {
  GuiWindow window;
  window.number = ++_windowCounter;
  window.title = String("Datum ") + String(std::to_string(window.number));
  window.label = window.title.str() + "###DatumWindow" + std::to_string(window.number);
  window.width = std::max(600, int(0.8*_settings.width));
  window.height = std::max(400, int(0.8*_settings.height));
  FileBrowser & browser = addBrowser();
  window.browser = browser.windowName();
  _browsersToPlace.erase(std::remove(_browsersToPlace.begin(), _browsersToPlace.end(), window.browser), _browsersToPlace.end());
  _windows.push_back(window);
  _currentWindow = -2 - int(_windows.size() - 1);
  return _currentWindow;
  }

  String DatumGui::windowTitle(int window) const
  {
  if (window == kMainWindow) return _settings.title;
  if (window >= 0 && window < nPaneWindows()) return _paneWindows[std::size_t(window)].title;
  std::size_t j = std::size_t(-2 - window);
  return j < _windows.size() ? _windows[j].title : String();
  }

  void DatumGui::setWindowTitleOf(int window, const String & title)
  {
  if (title.isNull() || title.str().empty()) return;
  if (window == kMainWindow) { setWindowTitle(title); return; }
  std::size_t j = std::size_t(-2 - window);
  if (window > -2 || j >= _windows.size()) return;
  _windows[j].title = title;
  _windows[j].label = title.str() + "###DatumWindow" + std::to_string(_windows[j].number);   // same ImGui window
  }

  void DatumGui::layoutGuiWindow(std::size_t j)
  {
  GuiWindow & window = _windows[j];
  int code = -2 - int(j);
  ImGuiID dock = dockspaceOf(code);
  ImVec2 area = ImGui::GetContentRegionAvail();
  ImGui::DockBuilderRemoveNode(dock);
  ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dock, ImVec2(std::max(area.x, 100.0f), std::max(area.y - ImGui::GetFrameHeight(), 100.0f)));
  ImGuiID canvases = dock;
  bool hasBrowser = false;
  for (const auto & b : _browsers) if (window.browser == b->windowName()) hasBrowser = true;
  if (hasBrowser)
    {
    ImGuiID left = 0;
    ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.24f, &left, &canvases);
    ImGui::DockBuilderDockWindow(window.browser.c_str(), left);
    }
  std::vector<CanvasPane *> panes;
  for (const auto & p : _panes) if (p->host() == code) panes.push_back(p.get());
  dockGrid(canvases, panes, window.columns);
  ImGui::DockBuilderFinish(dock);
  window.layoutPending = false;
  }

  void DatumGui::renderGuiWindows(const MenuItem *& hovered)
  {
  std::vector<std::size_t> closed;
  for (std::size_t j=0; j<_windows.size(); j++)
    {
    GuiWindow & window = _windows[j];
    if (!window.open) continue;
    int code = -2 - int(j);
    if (!window.placed)
      {
      // cascaded from the window used last
      ImGuiViewport * from = activeViewport();
      float step = 36.0f*float((window.number - 2) % 8);
      ImGui::SetNextWindowPos(ImVec2(from->Pos.x + 60.0f + step, from->Pos.y + 50.0f + step), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(float(window.width), float(window.height)), ImGuiCond_Always);
      ImGui::SetNextWindowFocus();
      window.placed = true;
      }
    ImGuiWindowClass windowClass;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = true;
    bool visible = ImGui::Begin(window.label.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking
                                | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (ImGui::IsWindowFocused()) _currentWindow = code;
    bool menuOpen = false;
    const MenuItem * item = nullptr;
    if (visible && ImGui::BeginMenuBar())
      {
      item = _menuBar.renderMenus(menuOpen);
      if (item) hovered = item;
      ImGui::EndMenuBar();
      }
    if (menuOpen) setCurrentWindow(code);
    ImVec2 size = ImGui::GetWindowSize();
    window.width = int(size.x);
    window.height = int(size.y);
    if (window.layoutPending && visible) layoutGuiWindow(j);
    float status = ImGui::GetFrameHeight();
    if (visible)
      {
      ImGui::DockSpace(dockspaceOf(code), ImVec2(0, -status));
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
      renderStatusBar(item, _hoverText);
      }
    else ImGui::DockSpace(dockspaceOf(code), ImVec2(0, 0), ImGuiDockNodeFlags_KeepAliveOnly);
    ImGui::End();
    if (!open) closed.push_back(j);
    }
  for (std::size_t j : closed) closeGuiWindow(j);
  }

  bool DatumGui::closeWindowContents(int window)
  {
  if (onWindowClosing) onWindowClosing(window);
  bool done = true;
  // canvases: those of a plot file go back to their window of canvases, the others are closed
  for (int k=nPanes()-1; k>=0; k--)
    {
    if (windowOfPane(k) != window) continue;
    int host = _panes[std::size_t(k)]->host();
    if (host >= 0)
      {
      // a canvas of a plot file: removed with its window by the application, otherwise back to its window
      if (host != window && host < nPaneWindows()) _paneWindows[std::size_t(host)].layoutPending = true;
      continue;
      }
    _closeRequests.push_back(k);
    }
  // file browsers: the first one is hidden; the others are closed (after their trees are saved, if asked)
  for (std::size_t k=0; k<_browsers.size(); k++)
    {
    FileBrowser & browser = *_browsers[k];
    if (_hiddenPanes.count(&browser) || windowOf(browser.windowName()) != window) continue;   // hidden: stays hidden
    if (k == 0) { _settings.browserVisible = false; continue; }
    if (onBrowserCloseRequest && !onBrowserCloseRequest(browser)) { done = false; continue; }
    _browsersToClose.push_back(&browser);
    }
  for (std::size_t k=0; k<_dataPanes.size(); )
    if (!_hiddenPanes.count(_dataPanes[k].get()) && windowOf(_dataPanes[k]->windowTitle().data()) == window) _dataPanes.erase(_dataPanes.begin() + long(k)); else k++;
  for (std::size_t k=0; k<_spreadsheetPanes.size(); )
    if (!_hiddenPanes.count(_spreadsheetPanes[k].get()) && windowOf(_spreadsheetPanes[k]->windowTitle().data()) == window) _spreadsheetPanes.erase(_spreadsheetPanes.begin() + long(k)); else k++;
  if (windowOf(FitPane::windowName()) == window) _fitsVisible = false;
  if (windowOf("Canvases") == window) _canvasListVisible = false;
  return done;
  }

  // ------------------------------------------------------------------------------------------------ panes shown or hidden

  std::vector<const void *> DatumGui::panesOfKind(PaneKind kind) const
  {
  std::vector<const void *> panes;
  if (kind == kBrowserPanes) for (const auto & b : _browsers) panes.push_back(b.get());
  else if (kind == kTablePanes) for (const auto & p : _spreadsheetPanes) panes.push_back(p.get());
  else for (const auto & p : _dataPanes) panes.push_back(p.get());
  return panes;
  }

  int DatumGui::nPanesOf(PaneKind kind) const
  {
  return int(panesOfKind(kind).size());
  }

  const void * DatumGui::currentOfKind(PaneKind kind) const
  {
  std::vector<const void *> panes = panesOfKind(kind);
  if (panes.empty()) return nullptr;
  const void * current = kind == kBrowserPanes ? (_activeBrowser >= 0 && _activeBrowser < int(panes.size()) ? panes[std::size_t(_activeBrowser)] : nullptr)
                       : kind == kTablePanes ? _currentTable : _currentData;
  if (std::find(panes.begin(), panes.end(), current) == panes.end()) current = panes.back();   // closed meanwhile: the last one
  return current;
  }

  bool DatumGui::paneShown(PaneKind kind, const void * pane) const
  {
  if (kind == kBrowserPanes && !_browsers.empty() && pane == _browsers.front().get()) return _settings.browserVisible;
  return _hiddenPanes.count(pane) == 0;
  }

  void DatumGui::setPaneShown(PaneKind kind, const void * pane, bool shown)
  {
  if (!pane || paneShown(kind, pane) == shown) return;
  if (kind == kBrowserPanes && pane == _browsers.front().get()) { setBrowserVisible(shown); return; }
  if (!shown) { _hiddenPanes.insert(pane); return; }
  _hiddenPanes.erase(pane);
  // shown again: in the current window if its place was lost (its window closed)
  std::string name;
  if (kind == kBrowserPanes) { for (const auto & b : _browsers) if (b.get() == pane) name = b->windowName(); }
  else if (kind == kTablePanes) { for (const auto & p : _spreadsheetPanes) if (p.get() == pane) name = p->windowTitle().str(); }
  else for (const auto & p : _dataPanes) if (p.get() == pane) name = p->windowTitle().str();
  if (!name.empty() && needsPlace(name.c_str())) dockInCurrentWindow(name, kind == kBrowserPanes);
  }

  bool DatumGui::panesShown(PaneKind kind, bool all) const
  {
  if (!all) { const void * current = currentOfKind(kind); return current && paneShown(kind, current); }
  std::vector<const void *> panes = panesOfKind(kind);
  if (panes.empty()) return false;
  for (const void * p : panes) if (!paneShown(kind, p)) return false;
  return true;
  }

  void DatumGui::setPanesShown(PaneKind kind, bool all, bool shown)
  {
  if (!all) { setPaneShown(kind, currentOfKind(kind), shown); return; }
  for (const void * p : panesOfKind(kind)) setPaneShown(kind, p, shown);
  }

  void DatumGui::closeGuiWindow(std::size_t j)
  {
  int code = -2 - int(j);
  if (!closeWindowContents(code)) { setStatus("Save or close the trees of its browser first: the window stays open"); return; }
  _windows[j].open = false;
  validateCurrentWindow();
  if (nOpenWindows() == 0) requestExit();   // the last DatumGui window: the application ends
  }

  bool DatumGui::hideMainWindow()
  {
  if (!closeWindowContents(kMainWindow)) return false;
  glfwHideWindow(_window);
  _mainHidden = true;
  ImGui::GetIO().ConfigViewportsNoAutoMerge = true;   // nothing goes into the hidden window
  validateCurrentWindow();
  return true;
  }

  void DatumGui::showMainWindow()
  {
  if (!_mainHidden) return;
  _mainHidden = false;
  ImGui::GetIO().ConfigViewportsNoAutoMerge = false;
  glfwShowWindow(_window);
  _settings.browserVisible = true;
  _layoutRequested = true;
  _currentWindow = kMainWindow;
  }

  void DatumGui::processDockRequests()
  {
  std::vector<DockRequest> later;
  for (DockRequest & request : _dockInto)
    {
    ImGuiID dockspace = dockspaceOf(request.window);
    ImGuiDockNode * root = windowOpen(request.window) ? ImGui::DockBuilderGetNode(dockspace) : nullptr;
    if (!root)
      {
      // the window is not drawn yet (made this frame): next frame; a window closed meanwhile: the current one
      if (!windowOpen(request.window)) { validateCurrentWindow(); request.window = _currentWindow; }
      if (++request.tries < 5) later.push_back(request);
      continue;
      }
    ImGuiDockNode * central = ImGui::DockBuilderGetCentralNode(dockspace);
    if (!central) central = root;
    if (request.left)
      {
      ImGuiID left = 0, rest = 0;
      ImGui::DockBuilderSplitNode(central->ID, ImGuiDir_Left, 0.25f, &left, &rest);
      ImGui::DockBuilderDockWindow(request.name.c_str(), left);
      }
    else if (central->Windows.Size > 0)
      {
      ImGuiID atDir = 0, other = 0;
      ImGuiDir direction = central->Size.x >= central->Size.y ? ImGuiDir_Right : ImGuiDir_Down;
      ImGui::DockBuilderSplitNode(central->ID, direction, 0.5f, &atDir, &other);
      ImGui::DockBuilderDockWindow(request.name.c_str(), atDir);
      }
    else ImGui::DockBuilderDockWindow(request.name.c_str(), central->ID);
    ImGui::DockBuilderFinish(dockspace);
    }
  _dockInto = later;
  }

  int DatumGui::addPaneWindow(const String & title, int columns, int rows, int width, int height)
  {
  PaneWindow window;
  window.title   = title;
  window.label   = title.str() + "###DatumPaneWindow" + std::to_string(_paneWindows.size());
  window.columns = std::max(1, columns);
  window.rows    = std::max(1, rows);
  window.width   = std::max(200, width);
  window.height  = std::max(150, height);
  _paneWindows.push_back(window);
  return int(_paneWindows.size()) - 1;
  }

  void DatumGui::setPaneWindow(int k, const String & title, int columns, int rows, int width, int height)
  {
  while (int(_paneWindows.size()) <= k) addPaneWindow(title, columns, rows, width, height);
  PaneWindow & w = _paneWindows[std::size_t(k)];
  w.title = title;
  w.label = title.str() + "###DatumPaneWindow" + std::to_string(k);
  columns = std::max(1, columns);
  rows = std::max(1, rows);
  bool shown = false;
  for (const auto & p : _panes) if (p->host() == k) shown = true;
  if (w.columns != columns || w.rows != rows)
    {
    // a window on screen whose canvases changed (e.g. one moved out): resized to keep the size of its panes
    if (shown && w.placed && w.resizeColumns == 0) { w.resizeColumns = w.columns; w.resizeRows = w.rows; }
    w.columns = columns;
    w.rows = rows;
    w.layoutPending = true;
    }
  if (!shown) { w.width = std::max(200, width); w.height = std::max(150, height); w.placed = false; w.resizeColumns = w.resizeRows = 0; }
  }

  void DatumGui::setPaneWindowPaneCount(int k, int n)
  {
  if (k < 0 || k >= nPaneWindows()) return;
  int count = 0;
  for (const auto & p : _panes) if (p->host() == k) count++;
  if (count == n) return;
  while (count > n)
    for (int j=nPanes()-1; j>=0; j--) if (_panes[std::size_t(j)]->host() == k) { _panes.erase(_panes.begin() + j); count--; break; }
  while (count < n) { addPaneTo(k); count++; }
  _paneWindows[std::size_t(k)].layoutPending = true;
  if (_currentPane >= nPanes()) _currentPane = 0;
  }

  void DatumGui::closePaneWindowsFrom(int k)
  {
  // a DatumGui window closed (the window of a plot file): the other panes it holds are closed too
  for (int w=std::max(0, k); w<nPaneWindows(); w++) if (windowOpen(w)) closeWindowContents(w);
  for (int j=nPanes()-1; j>=0; j--) if (_panes[std::size_t(j)]->host() >= k) _panes.erase(_panes.begin() + j);
  for (std::size_t w=std::size_t(std::max(0, k)); w<_paneWindows.size(); w++) { _paneWindows[w].placed = false; _paneWindows[w].layoutPending = true; }
  if (_currentPane >= nPanes()) _currentPane = 0;
  }

  std::vector<CanvasPane *> DatumGui::panesOf(int window)
  {
  std::vector<CanvasPane *> panes;
  for (const auto & p : _panes) if (p->host() == window) panes.push_back(p.get());
  return panes;
  }

  CanvasPane & DatumGui::addPaneTo(int window)
  {
  CanvasPane & pane = newPane();
  if (window >= 0 && window < nPaneWindows())
    {
    pane.setHost(window);
    _paneWindows[std::size_t(window)].layoutPending = true;
    }
  return pane;
  }

  void DatumGui::paneWindowLayout(int k, int & width, int & height, int & columns, int & rows) const
  {
  const PaneWindow & w = _paneWindows.at(std::size_t(k));
  width = w.width; height = w.height; columns = w.columns; rows = w.rows;
  }

  void DatumGui::setWindowTitle(const String & title)
  {
  _settings.title = title;
  if (_window) glfwSetWindowTitle(_window, title.data());
  }

  void DatumGui::windowSize(int & width, int & height) const
  {
  width = _settings.width; height = _settings.height;
  if (_window) glfwGetWindowSize(_window, &width, &height);
  }

  void DatumGui::setWindowSize(int width, int height)
  {
  _settings.width = width; _settings.height = height;
  if (_window && width > 0 && height > 0) glfwSetWindowSize(_window, width, height);
  _layoutRequested = true;
  }

  void DatumGui::renderPaneWindows(const MenuItem *& hovered)
  {
  ImGuiViewport * main = ImGui::GetMainViewport();
  for (std::size_t w=0; w<_paneWindows.size(); w++)
    {
    PaneWindow & window = _paneWindows[w];
    std::vector<CanvasPane *> panes;
    for (const auto & p : _panes) if (p->host() == int(w)) panes.push_back(p.get());
    if (panes.empty()) continue;
    if (!window.placed)
      {
      // a window of its own on the screen (not merged into the main one): beside the main window if the screen has
      // room for it, otherwise below it, otherwise cascaded over it
      float width = float(window.width), height = float(window.height);
      ImVec2 screenMin(main->Pos.x, main->Pos.y), screenMax(main->Pos.x + main->Size.x, main->Pos.y + main->Size.y);
      ImGuiPlatformIO & platform = ImGui::GetPlatformIO();
      for (const ImGuiPlatformMonitor & m : platform.Monitors)
        if (main->Pos.x >= m.WorkPos.x && main->Pos.x < m.WorkPos.x + m.WorkSize.x &&
            main->Pos.y >= m.WorkPos.y && main->Pos.y < m.WorkPos.y + m.WorkSize.y)
          { screenMin = m.WorkPos; screenMax = ImVec2(m.WorkPos.x + m.WorkSize.x, m.WorkPos.y + m.WorkSize.y); }
      float step = 30.0f*float(w);
      ImVec2 position(main->Pos.x + main->Size.x + 8.0f + step, main->Pos.y + step);
      if (position.x + width > screenMax.x)
        {
        position = ImVec2(main->Pos.x + step, main->Pos.y + main->Size.y + 8.0f + step);
        if (position.y + height > screenMax.y)
          position = ImVec2(std::max(screenMin.x, std::min(main->Pos.x + 60.0f + 40.0f*float(w), screenMax.x - width)),
                            std::max(screenMin.y, std::min(main->Pos.y + 80.0f + 40.0f*float(w), screenMax.y - height)));
        }
      ImGui::SetNextWindowPos(position, ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(float(window.width), float(window.height)), ImGuiCond_Always);
      window.placed = true;
      window.resizeColumns = window.resizeRows = 0;
      }
    else if (window.resizeColumns > 0)
      {
      // its grid changed: the panes keep their size, the window shrinks (or grows, within its monitor)
      float bar = ImGui::GetFrameHeight();
      float width = float(window.width)*float(window.columns)/float(window.resizeColumns);
      float height = bar + std::max(50.0f, float(window.height) - bar)*float(window.rows)/float(window.resizeRows);
      ImVec2 limit(main->Size.x, main->Size.y);
      ImGuiPlatformIO & platform = ImGui::GetPlatformIO();
      for (const ImGuiPlatformMonitor & m : platform.Monitors) limit = ImVec2(std::max(limit.x, m.WorkSize.x), std::max(limit.y, m.WorkSize.y));
      if (width > float(window.width)) width = std::min(width, std::max(float(window.width), limit.x));
      if (height > float(window.height)) height = std::min(height, std::max(float(window.height), limit.y));
      ImGui::SetNextWindowSize(ImVec2(std::max(200.0f, width), std::max(150.0f, height)), ImGuiCond_Always);
      window.resizeColumns = window.resizeRows = 0;
      }
    ImGuiWindowClass windowClass;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
    if (window.gui) flags |= ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    bool visible = ImGui::Begin(window.label.c_str(), window.gui ? &open : nullptr, flags);
    ImGui::PopStyleVar();
    // a DatumGui window (the window of a plot file): the menus of the application, a status bar
    const MenuItem * item = nullptr;
    if (window.gui)
      {
      if (ImGui::IsWindowFocused()) _currentWindow = int(w);
      bool menuOpen = false;
      if (visible && ImGui::BeginMenuBar())
        {
        item = _menuBar.renderMenus(menuOpen);
        if (item) hovered = item;
        ImGui::EndMenuBar();
        }
      if (menuOpen) setCurrentWindow(int(w));
      }
    float status = window.gui ? ImGui::GetFrameHeight() : 0.0f;
    ImVec2 size = ImGui::GetWindowSize();
    window.width = int(size.x);
    window.height = int(size.y);
    ImGuiID dock = ImHashStr((std::string("DatumPaneWindowDock") + std::to_string(w)).c_str());
    if (window.layoutPending)
      {
      ImVec2 area = ImGui::GetContentRegionAvail();
      ImGui::DockBuilderRemoveNode(dock);
      ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(dock, ImVec2(std::max(area.x, 100.0f), std::max(area.y - status, 100.0f)));
      dockGrid(dock, panes, window.columns);
      ImGui::DockBuilderFinish(dock);
      window.layoutPending = false;
      }
    if (visible) ImGui::DockSpace(dock, ImVec2(0, -status));
    else ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_KeepAliveOnly);
    if (visible && window.gui)
      {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
      renderStatusBar(item, _hoverText);
      }
    ImGui::End();
    if (!open)
      {
      // closed by its x: the application decides (a plot file asks to be saved), or its canvases are removed
      if (!onCloseWindow || onCloseWindow(int(w)))
        {
        closeWindowContents(int(w));
        for (int j=nPanes()-1; j>=0; j--) if (_panes[std::size_t(j)]->host() == int(w)) _panes.erase(_panes.begin() + j);
        if (_currentPane >= nPanes()) _currentPane = 0;
        validateCurrentWindow();
        }
      }
    }
  }

  DataPane & DatumGui::addDataPane()
  {
  char name[32];
  std::snprintf(name, sizeof(name), "Data %d", ++_dataCounter);
  _dataPanes.emplace_back(new DataPane(name));
  DataPane & pane = *_dataPanes.back();
  _currentData = &pane;
  std::string beside = paneOfCurrentWindow();
  if (beside.empty()) dockInCurrentWindow(pane.windowTitle().str());
  else _dockBeside.push_back(std::make_pair(std::string("###") + name, beside));
  return pane;
  }

  SpreadsheetPane * DatumGui::activeTable()
  {
  if (!_tableActive) return nullptr;
  for (const auto & p : _spreadsheetPanes)
    if (p.get() == _currentTable) return (!_hiddenPanes.count(p.get()) && p->shown()) ? p.get() : nullptr;
  return nullptr;
  }

  SpreadsheetPane & DatumGui::addSpreadsheetPane()
  {
  char name[32];
  std::snprintf(name, sizeof(name), "Table %d", ++_spreadsheetCounter);
  _spreadsheetPanes.emplace_back(new SpreadsheetPane(name));
  SpreadsheetPane & pane = *_spreadsheetPanes.back();
  _currentTable = &pane;
  _tableActive = true;   // a table just opened is the one in use
  std::string beside = paneOfCurrentWindow();
  if (beside.empty()) dockInCurrentWindow(pane.windowTitle().str());
  else _dockBeside.push_back(std::make_pair(pane.windowId(), beside));
  return pane;
  }

  CanvasPane & DatumGui::addPaneBeside(const std::string & windowId)
  {
  CanvasPane & pane = newPane();
  _dockBeside.push_back(std::make_pair(std::string(pane.windowId()), windowId));
  setCurrentPane(nPanes() - 1);
  return pane;
  }

  CanvasPane & DatumGui::addPaneBelow(const std::string & windowId, float fraction)
  {
  CanvasPane & pane = newPane();
  _dockBelow.push_back(std::make_pair(std::make_pair(std::string(pane.windowId()), windowId), fraction));
  return pane;
  }

  int DatumGui::paneIndexOfWindow(const std::string & windowId) const
  {
  if (windowId.empty()) return -1;
  for (int k=0; k<int(_panes.size()); k++) if (windowId == _panes[std::size_t(k)]->windowId()) return k;
  return -1;
  }

  void DatumGui::closePane(int k)
  {
  if (k < 0 || k >= nPanes()) return;
  // a dock space keeps its central node even when empty: the main window is laid out again rather than leave a hole
  // (the canvas area stays empty when its last canvas is closed)
  ImGuiWindow * window = ImGui::GetCurrentContext() ? ImGui::FindWindowByName(_panes[k]->windowId()) : nullptr;
  if (window && window->DockNode && window->DockNode->IsCentralNode() && _panes[k]->host() == kMainWindow
      && windowOfPane(k) == kMainWindow)
    {
    int rest = 0;
    for (int j=0; j<nPanes(); j++) if (j != k && _panes[std::size_t(j)]->host() == kMainWindow && windowOfPane(j) == kMainWindow) rest++;
    if (rest > 0)
      {
      _gridColumns = int(std::ceil(std::sqrt(double(rest))));
      _layoutRequested = true;
      }
    }
  _panes.erase(_panes.begin() + k);
  if (_currentPane > k || _currentPane >= nPanes()) _currentPane = std::max(0, _currentPane - 1);
  _focusedPane = -1;
  }

  void DatumGui::setFitPaneVisible(bool visible)
  {
  if (visible && !_fitsVisible)
    {
    // docked below the canvases the first time; afterwards where the user left it
    if (needsPlace(FitPane::windowName())) _fitsDockRequested = true;
    _fitsShownOnce = true;
    }
  _fitsVisible = visible;
  }

  void DatumGui::dockFitPane()
  {
  // the node of the canvases of the current window: the largest one holding its canvas but not its file browser
  validateCurrentWindow();
  ImGuiID dockspace = dockspaceOf(_currentWindow);
  std::string canvas = paneOfCurrentWindow();
  ImGuiWindow * first = canvas.empty() ? nullptr : ImGui::FindWindowByName(canvas.c_str());
  ImGuiWindow * browser = ImGui::FindWindowByName(FileBrowser::mainWindowName());
  ImGuiDockNode * browserNode = browser ? browser->DockNode : nullptr;
  auto holdsBrowser = [browserNode](ImGuiDockNode * node)
    {
    for (ImGuiDockNode * n = browserNode; n; n = n->ParentNode) if (n == node) return true;
    return false;
    };
  ImGuiDockNode * node = first ? first->DockNode : nullptr;
  if (!node) node = ImGui::DockBuilderGetCentralNode(dockspace);
  if (!node) return;
  if (ImGui::DockNodeGetRootNode(node)->ID != dockspace) return;
  while (node->ParentNode && !holdsBrowser(node->ParentNode)) node = node->ParentNode;
  ImGuiID below = 0, above = 0;
  ImGui::DockBuilderSplitNode(node->ID, ImGuiDir_Down, 0.3f, &below, &above);
  ImGui::DockBuilderDockWindow(FitPane::windowName(), below);
  ImGui::DockBuilderFinish(dockspace);
  }

  void DatumGui::renderFitPane()
  {
  if (!_fitsShownOnce && !_panes.empty())
    for (const auto & item : currentPane().currentGraph().items())
      if (!item.isMap() && item.nFits() > 0) { setFitPaneVisible(true); break; }
  if (!_fitsVisible) return;
  if (_fitsDockRequested) { dockFitPane(); _fitsDockRequested = false; }
  bool open = true;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
  bool visible = ImGui::Begin(FitPane::windowName(), &open, ImGuiWindowFlags_NoCollapse);
  ImGui::PopStyleVar();
  if (visible && !_panes.empty())
    {
    CanvasPane & pane = currentPane();
    _fitPane.render(&pane.currentGraph(), pane.beforeChange);
    }
  ImGui::End();
  _fitsVisible = open;
  }

  void DatumGui::renderCanvasList()
  {
  if (!_canvasListVisible) return;
  bool open = true;
  // the first time: a tab beside the file browser docked in the main window, or a window of its own (never over the
  // canvases)
  if (needsPlace("Canvases"))
    {
    ImGuiWindow * browser = nullptr;
    for (const auto & b : _browsers)
      if (windowOf(b->windowName()) == _currentWindow) { browser = ImGui::FindWindowByName(b->windowName()); break; }
    if (browser && browser->DockId) ImGui::SetNextWindowDockID(browser->DockId, ImGuiCond_Always);
    else
      {
      ImGuiViewport * here = activeViewport();
      ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
      ImGui::SetNextWindowPos(ImVec2(here->WorkPos.x + 40, here->WorkPos.y + 60), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_Always);
      }
    }
  if (ImGui::Begin("Canvases", &open))
    {
    GRAPHICS::CanvasRegistry & registry = GRAPHICS::CanvasRegistry::session();
    auto button = [this](const char * label, const char * command, const char * tip)
      {
      if (ImGui::SmallButton(label) && onCommand) onCommand(command);
      ImGui::SetItemTooltip("%s", tip);
      };
    button("Save All...", "file.saveAll", "Save every canvas to its own file (SVG or PDF)");
    ImGui::SameLine();
    button("Book...", "file.saveBook", "Every canvas as a page of one PDF file, to print");
    ImGui::SameLine();
    button("Presentation...", "file.presentation", "A Beamer presentation (LaTeX) of the canvases");
    ImGui::Separator();
    ImGui::TextDisabled("%d canvas%s", registry.size(), registry.size() == 1 ? "" : "es");
    for (const String & group : registry.groups())
      {
      ImGui::PushID(group.data());
      ImGui::SetNextItemOpen(true, ImGuiCond_Once);
      if (ImGui::TreeNode("##group", "%s", group.str().empty() ? "(no group)" : group.data()))
        {
        for (int k=0; k<registry.size(); k++)
          {
          if (!(registry.group(k) == group)) continue;
          GRAPHICS::Canvas & canvas = registry.canvas(k);
          int pane = -1;                                       // the pane showing it (none: a canvas of a script)
          for (int p=0; p<nPanes(); p++) if (&_panes[size_t(p)]->canvas() == &canvas) pane = p;
          ImGui::PushID(k);
          ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
                                     | (pane == _currentPane ? ImGuiTreeNodeFlags_Selected : 0);
          bool expanded = ImGui::TreeNodeEx("##canvas", flags, "%s  (%d graph%s)", registry.name(k).data(),
                                            canvas.nGraphs(), canvas.nGraphs() == 1 ? "" : "s");
          if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && pane >= 0) setCurrentPane(pane);
          if (expanded)
            {
            for (int g=0; g<canvas.nGraphs(); g++)
              {
              const GRAPHICS::Graph & graph = canvas.graph(g);
              String label = graph.title().str().empty() ? String("graph ") + String(std::to_string(g + 1)) : graph.title();
              char text[256];
              std::snprintf(text, sizeof(text), "%s  (%d object%s%s)##g%d", label.data(), int(graph.items().size()),
                            graph.items().size() == 1 ? "" : "s", graph.shapes().empty() ? "" : ", shapes", g);
              bool selected = pane == _currentPane && pane >= 0 && _panes[size_t(pane)]->selectedGraph() == g;
              if (ImGui::Selectable(text, selected) && pane >= 0) { setCurrentPane(pane); _panes[size_t(pane)]->setSelectedGraph(g); }
              }
            ImGui::TreePop();
            }
          ImGui::PopID();
          }
        ImGui::TreePop();
        }
      ImGui::PopID();
      }
    }
  ImGui::End();
  _canvasListVisible = open;
  }

  void DatumGui::setShowToolbars(bool v)
  {
  _settings.toolbars = v;
  for (auto & pane : _panes) pane->setShowToolbar(v);
  }

  void DatumGui::setStatus(const String & message)
  {
  _status = message;
  _statusTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
  }

  void DatumGui::showMessage(const String & title, const String & text)
  {
  _messageTitle = title;
  _messageText  = text;
  _messageRequested = true;
  }

  void DatumGui::requestExit()
  {
  _exitRequested = true;
  }


  void DatumGui::run()
  {
  if (!_window) return;
  ImGuiIO & io = ImGui::GetIO();
  while (true)
    {
    if (glfwWindowShouldClose(_window) && !_exitRequested)
      {
      // the x of the main window: hidden if other DatumGui windows are open, otherwise the application ends
      glfwSetWindowShouldClose(_window, GLFW_FALSE);
      bool others = nOpenWindows() > (_mainHidden ? 0 : 1);
      if (!others) _exitRequested = true;
      else if (!hideMainWindow()) setStatus("Save or close the trees of the browsers of the main window first");
      }
    if (_exitRequested)
      {
      bool close = !onCloseRequest || onCloseRequest();
      if (close) break;
      glfwSetWindowShouldClose(_window, GLFW_FALSE);
      _exitRequested = false;
      if (nOpenWindows() == 0) showMainWindow();          // the exit was cancelled: a window to work in
      }
    glfwWaitEventsTimeout(0.05);                       // low CPU use when idle
    // the main window minimized (and no other DatumGui window): nothing to draw
    if (glfwGetWindowAttrib(_window, GLFW_ICONIFIED) && nOpenWindows() <= 1) { ImGui_ImplGlfw_Sleep(10); continue; }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    renderFrame();
    ImGui::Render();

    int width, height;
    glfwGetFramebufferSize(_window, &width, &height);
    glViewport(0, 0, width, height);
    ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    glClearColor(bg.x, bg.y, bg.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
      {
      GLFWwindow * context = glfwGetCurrentContext();
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
      glfwMakeContextCurrent(context);
      }
    glfwSwapBuffers(_window);
    LatexTextures::instance().collect(ImGui::GetFrameCount());
    }
  }

  void DatumGui::renderFrame()
  {
  // the DatumGui windows on screen (viewports), from the last frame: where each pane is
  _viewportWindows.clear();
  if (!_mainHidden) _viewportWindows[ImGui::GetMainViewport()->ID] = kMainWindow;
  for (int k=0; k<nPaneWindows(); k++)
    if (windowOpen(k)) if (ImGuiViewport * v = viewportOf(k)) _viewportWindows[v->ID] = k;
  for (std::size_t j=0; j<_windows.size(); j++)
    if (_windows[j].open) if (ImGuiViewport * v = viewportOf(-2 - int(j))) _viewportWindows[v->ID] = -2 - int(j);
  // windows closed from a menu (Datum > Close Window): as their x
  std::vector<int> closing;
  closing.swap(_closeWindowRequests);
  for (int window : closing)
    {
    if (!windowOpen(window)) continue;
    if (window == kMainWindow) glfwSetWindowShouldClose(_window, GLFW_TRUE);   // handled by run(): hidden or the end
    else if (window <= -2) closeGuiWindow(std::size_t(-2 - window));
    else if (!onCloseWindow || onCloseWindow(window))
      {
      closeWindowContents(window);
      for (int j=nPanes()-1; j>=0; j--) if (_panes[std::size_t(j)]->host() == window) _panes.erase(_panes.begin() + j);
      if (_currentPane >= nPanes()) _currentPane = 0;
      }
    }
  validateCurrentWindow();
  gActiveViewport = viewportOf(_currentWindow);
  FileDialog::viewport = gActiveViewport;
  bool menuOpen = false;
  const MenuItem * hovered = _menuBar.render(&menuOpen);
  if (menuOpen && !_mainHidden) setCurrentWindow(kMainWindow);
  String hoverText;

  // main area: a dock space (the file browser and other tools dock around the canvases)
  ImGuiViewport * viewport = ImGui::GetMainViewport();
  float statusHeight = ImGui::GetFrameHeight();
  ImGuiWindowFlags sideFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
  if (ImGui::BeginViewportSideBar("##statusbar", viewport, ImGuiDir_Down, statusHeight, sideFlags))
    {
    if (ImGui::BeginMenuBar()) { renderStatusBar(hovered, hoverText); ImGui::EndMenuBar(); }
    }
  ImGui::End();
  ImGuiID dockspace = ImHashStr("DatumDockSpace");
  if (_firstFrame)
    {
    // the canvas panes are laid out at startup; the browser follows the settings
    _layoutRequested = true;
    _firstFrame = false;
    }
  if (_layoutRequested) { buildDockLayout(dockspace); _layoutRequested = false; }
  ImGui::DockSpaceOverViewport(dockspace, viewport);
  if (_sideDockRequested) { dockSideWindow(dockspaceOf(toolWindow())); _sideDockRequested = false; }
  renderPaneWindows(hovered);
  renderGuiWindows(hovered);
  processDockRequests();
  renderCanvases(hoverText);

  renderBrowser();
  renderCanvasList();
  _fileDialog.render();
  renderMessage();
  if (onFrame) onFrame();
  // the last DatumGui window closed otherwise (e.g. the window of a plot file): the application ends
  if (_mainHidden && nOpenWindows() == 0) requestExit();
  }

  FileBrowser & DatumGui::activeBrowser()
  {
  if (_activeBrowser < 0 || _activeBrowser >= int(_browsers.size())) _activeBrowser = 0;
  return *_browsers[_activeBrowser];
  }

  void DatumGui::setBrowserVisible(bool visible)
  {
  if (visible && !_settings.browserVisible && needsPlace(FileBrowser::mainWindowName()))
    dockInCurrentWindow(FileBrowser::mainWindowName(), true);
  _settings.browserVisible = visible;
  }

  void DatumGui::closeBrowser(FileBrowser & browser)
  {
  if (!_browsers.empty() && _browsers.front().get() == &browser) { _settings.browserVisible = false; return; }
  _browsersToClose.push_back(&browser);
  }

  FileBrowser & DatumGui::addBrowser()
  {
  _browsers.emplace_back(new FileBrowser(++_browserCounter));
  FileBrowser & browser = *_browsers.back();
  _browsersToPlace.push_back(browser.windowName());
  if (onNewBrowser) onNewBrowser(browser);
  _activeBrowser = int(_browsers.size()) - 1;
  _settings.browserVisible = true;
  return browser;
  }

  void DatumGui::setBrowserDocked(bool docked)
  {
  if (docked == _settings.browserDocked) return;
  _settings.browserDocked = docked;
  _settings.browserVisible = true;
  if (docked) _layoutRequested = true; else _undockRequested = true;
  }

  void DatumGui::buildDockLayout(unsigned int dockId)
  {
  ImGuiViewport * viewport = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockId);
  ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockId, viewport->WorkSize);
  ImGuiID canvases = dockId;
  if (_settings.browserDocked)
    {
    ImGuiID left = 0;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.22f, &left, &canvases);
    ImGui::DockBuilderDockWindow(FileBrowser::mainWindowName(), left);
    }
  if (!_sideWindow.empty() && _sideVisible)
    {
    ImGuiID side = 0;
    ImGui::DockBuilderSplitNode(canvases, ImGuiDir_Left, _sideFraction, &side, &canvases);
    ImGui::DockBuilderDockWindow(_sideWindow.c_str(), side);
    }
  buildPaneGrid(canvases);
  ImGui::DockBuilderFinish(dockId);
  }

  void DatumGui::dockSideWindow(unsigned int dockspace)
  {
  if (_sideWindow.empty()) return;
  ImGuiWindow * window = ImGui::FindWindowByName(_sideWindow.c_str());
  if (window && window->DockId != 0 && ImGui::DockBuilderGetNode(window->DockId) && !needsPlace(_sideWindow.c_str()))
    return;   // its place is kept
  if (window && !window->DockId && !needsPlace(_sideWindow.c_str())) return;   // floating where the user left it
  ImGuiDockNode * area = ImGui::DockBuilderGetNode(dockspace);
  if (!area) { if (dockspace == dockspaceOf(kMainWindow)) _layoutRequested = true; return; }
  // the canvases: beside a browser docked on the left of that window, or the whole dock space
  ImGuiWindow * browser = nullptr;
  for (const auto & b : _browsers)
    {
    ImGuiWindow * w = ImGui::FindWindowByName(b->windowName());
    if (w && w->DockNode && ImGui::DockNodeGetRootNode(w->DockNode)->ID == dockspace) { browser = w; break; }
    }
  if (browser && browser->DockNode && browser->DockNode->ParentNode)
    {
    ImGuiDockNode * parent = browser->DockNode->ParentNode;
    ImGuiDockNode * other = parent->ChildNodes[0] == browser->DockNode ? parent->ChildNodes[1] : parent->ChildNodes[0];
    if (other) area = other;
    }
  ImGuiID side = 0, rest = 0;
  ImGui::DockBuilderSplitNode(area->ID, ImGuiDir_Left, _sideFraction, &side, &rest);
  ImGui::DockBuilderDockWindow(_sideWindow.c_str(), side);
  ImGui::DockBuilderFinish(dockspace);
  }

  void DatumGui::buildPaneGrid(unsigned int nodeId)
  {
  std::vector<CanvasPane *> panes;
  for (int k=0; k<nPanes(); k++)
    if (_panes[std::size_t(k)]->host() == kMainWindow && windowOfPane(k) == kMainWindow) panes.push_back(_panes[std::size_t(k)].get());
  dockGrid(nodeId, panes, _gridColumns);
  }

  void DatumGui::dockGrid(unsigned int nodeId, const std::vector<CanvasPane *> & panes, int gridColumns)
  {
  // rows from the top, then the panes of each row from the left (the last row may have fewer panes)
  int n = int(panes.size());
  if (n == 0) return;
  int columns = std::min(std::max(gridColumns, 1), n);
  int rows = (n + columns - 1)/columns;
  std::vector<ImGuiID> rowNodes;
  ImGuiID remaining = nodeId;
  for (int r=0; r<rows-1; r++)
    {
    ImGuiID top = 0;
    ImGui::DockBuilderSplitNode(remaining, ImGuiDir_Up, 1.0f/float(rows - r), &top, &remaining);
    rowNodes.push_back(top);
    }
  rowNodes.push_back(remaining);
  for (int r=0; r<rows; r++)
    {
    int count = std::min(columns, n - r*columns);
    ImGuiID rest = rowNodes[r];
    for (int c=0; c<count-1; c++)
      {
      ImGuiID left = 0;
      ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 1.0f/float(count - c), &left, &rest);
      ImGui::DockBuilderDockWindow(panes[r*columns + c]->windowId(), left);
      }
    ImGui::DockBuilderDockWindow(panes[r*columns + count - 1]->windowId(), rest);
    }
  }

  void DatumGui::renderBrowser()
  {
  for (FileBrowser * closing : _browsersToClose)
    for (std::size_t k=1; k<_browsers.size(); k++)
      if (_browsers[k].get() == closing)
        {
        if (onCloseBrowser) onCloseBrowser(*closing);
        _browsers.erase(_browsers.begin() + long(k));
        if (_activeBrowser >= int(k)) _activeBrowser = 0;
        break;
        }
  _browsersToClose.clear();
  ImGuiViewport * viewport = activeViewport();
  // the first browser, if shown (hiding it leaves the other browsers shown)
  if (_settings.browserVisible)
    {
    if (_undockRequested)
      {
      // a window of its own: next to the left edge of the main window, outside it if the screen allows
      ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
      ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 40, viewport->WorkPos.y + 60), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(380, viewport->WorkSize.y*0.75f), ImGuiCond_Always);
      _undockRequested = false;
      }
    else
      ImGui::SetNextWindowSize(ImVec2(380, viewport->WorkSize.y*0.75f), ImGuiCond_FirstUseEver);
    ImGuiWindowClass browserClass;
    if (!_settings.browserDocked) browserClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&browserClass);
    bool visible = true;
    if (ImGui::Begin(FileBrowser::mainWindowName(), &visible))
      {
      // follow what the user did with the mouse (docking or undocking the window)
      if (!_layoutRequested) _settings.browserDocked = ImGui::IsWindowDocked();
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        { _activeBrowser = 0; _lastFocus = kFocusBrowser; noteFocus(FileBrowser::mainWindowName()); _tableActive = false; }
      _browsers.front()->render();
      }
    else if (!_layoutRequested) _settings.browserDocked = ImGui::IsWindowDocked();
    ImGui::End();
    _settings.browserVisible = visible;
    }

  // the other browsers: each in a pane of its own when it is made, below the last browser (the area of that browser
  // split in two; a floating window if the browsers are not docked), then wherever they are moved
  for (const std::string & name : _browsersToPlace)
    {
    // below the last browser of the current DatumGui window, or on the left of that window
    ImGuiWindow * last = nullptr;
    for (std::size_t k=_browsers.size(); k-- > 0 && !last; )
      if (name != _browsers[k]->windowName() && windowOf(_browsers[k]->windowName()) == _currentWindow)
        {
        ImGuiWindow * w = ImGui::FindWindowByName(_browsers[k]->windowName());
        if (w && w->DockNode) last = w;
        }
    if (!last) { dockInCurrentWindow(name, true); continue; }
    ImGuiID below = 0, above = 0;
    ImGui::DockBuilderSplitNode(last->DockNode->ID, ImGuiDir_Down, 0.5f, &below, &above);
    ImGui::DockBuilderDockWindow(name.c_str(), below);
    ImGui::DockBuilderFinish(ImGui::DockNodeGetRootNode(last->DockNode)->ID);
    }
  _browsersToPlace.clear();
  for (std::size_t k=1; k<_browsers.size(); )
    {
    FileBrowser & browser = *_browsers[k];
    if (_hiddenPanes.count(&browser)) { k++; continue; }   // hidden (View > File Browsers)
    ImGui::SetNextWindowSize(ImVec2(380, viewport->WorkSize.y*0.75f), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin(browser.windowName(), &open))
      {
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        { _activeBrowser = int(k); _lastFocus = kFocusBrowser; noteFocus(browser.windowName()); _tableActive = false; }
      browser.render();
      }
    ImGui::End();
    if (open) { k++; continue; }
    if (onBrowserCloseRequest && !onBrowserCloseRequest(browser)) { k++; continue; }
    _hiddenPanes.erase(&browser);
    if (onCloseBrowser) onCloseBrowser(browser);
    _browsers.erase(_browsers.begin() + long(k));
    if (_activeBrowser >= int(k)) _activeBrowser = 0;
    }
  }

  void DatumGui::renderCanvases(String & hoverText)
  {
  // panes added since the last frame: docked beside the pane that was current, splitting its area
  ImGuiID dockspace = dockspaceOf(_currentWindow);
  for (const auto & request : _dockBeside)
    {
    ImGuiWindow * target = request.second.empty() ? nullptr : ImGui::FindWindowByName(request.second.c_str());
    ImGuiDockNode * node = target ? target->DockNode : nullptr;
    if (!node) node = ImGui::DockBuilderGetCentralNode(dockspace);
    if (!node) continue;
    ImGuiID atDir = 0, other = 0;
    ImGuiDir direction = node->Size.x >= node->Size.y ? ImGuiDir_Right : ImGuiDir_Down;
    ImGui::DockBuilderSplitNode(node->ID, direction, 0.5f, &atDir, &other);
    ImGui::DockBuilderDockWindow(request.first.c_str(), atDir);
    ImGui::DockBuilderFinish(dockspace);
    }
  _dockBeside.clear();
  // panes added below a window: its area split, the new pane at the bottom (beside the current pane if it is not docked)
  for (const auto & request : _dockBelow)
    {
    ImGuiWindow * target = request.first.second.empty() ? nullptr : ImGui::FindWindowByName(request.first.second.c_str());
    ImGuiDockNode * node = target ? target->DockNode : nullptr;
    if (!node) node = ImGui::DockBuilderGetCentralNode(dockspace);
    if (!node) continue;
    ImGuiID below = 0, above = 0;
    ImGui::DockBuilderSplitNode(node->ID, ImGuiDir_Down, request.second, &below, &above);
    ImGui::DockBuilderDockWindow(request.first.first.c_str(), below);
    ImGui::DockBuilderFinish(ImGui::DockNodeGetRootNode(node)->ID);
    }
  _dockBelow.clear();

  float areaX0 = 1e9f, areaY0 = 1e9f, areaX1 = -1e9f, areaY1 = -1e9f;
  for (int k=0; k<nPanes(); k++)
    {
    CanvasPane & pane = *_panes[k];
    bool open = true;
    if (_focusRequested && k == _currentPane) ImGui::SetNextWindowFocus();
    // a close button on every canvas (closing the last canvas of the main window clears it, see closePane())
    ImGuiWindow * window = ImGui::FindWindowByName(pane.windowId());
    bool outside = window && window->Viewport && window->Viewport != ImGui::GetMainViewport();
    // its group in the registry of the canvases: the main window, or the separate window holding it
    if (pane.canvas().registry())
      {
      String group = "Main window";
      int where = windowOfPane(k);
      if (pane.host() >= 0 && pane.host() < nPaneWindows())
        group = _paneWindows[std::size_t(pane.host())].title;
      else if (where <= -2 && std::size_t(-2 - where) < _windows.size())
        group = _windows[std::size_t(-2 - where)].title;
      else if (outside)
        {
        auto found = _windowNumbers.find(window->Viewport->ID);
        if (found == _windowNumbers.end()) found = _windowNumbers.emplace(window->Viewport->ID, int(_windowNumbers.size()) + 2).first;
        group = String("Window ") + String(std::to_string(found->second));
        }
      GRAPHICS::CanvasRegistry & registry = *pane.canvas().registry();
      int k = registry.indexOf(pane.canvas());
      if (k >= 0 && !(registry.group(k) == group)) registry.setGroup(pane.canvas(), group);
      }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    bool visible = ImGui::Begin(pane.windowLabel(), &open,
                                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    if (visible)
      {
      // the pane whose window gets the focus (click, tab) becomes current
      bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
      if (focused) { _lastFocus = kFocusCanvas; noteFocus(pane.windowId()); _tableActive = false; }
      if (focused && _focusedPane != k) { _currentPane = k; _focusedPane = k; }
      ImVec2 size = ImGui::GetContentRegionAvail();
      bool clicked = pane.render(size.x, size.y, k == _currentPane, _font);
      if (clicked) { _currentPane = k; noteFocus(pane.windowId()); }
      if (!pane.hoverText().isNull()) hoverText = pane.hoverText();
      float x, y, w, h;
      pane.canvasRect(x, y, w, h);
      areaX0 = std::min(areaX0, x); areaY0 = std::min(areaY0, y);
      areaX1 = std::max(areaX1, x + w); areaY1 = std::max(areaY1, y + h);
      }
    ImGui::End();
    if (!open) _closeRequests.push_back(k);
    }
  _focusRequested = false;
  _hoverText = hoverText;

  // data panes: tables of data points, closed with the x of their tab
  for (std::size_t k=0; k<_dataPanes.size(); )
    {
    DataPane & pane = *_dataPanes[k];
    if (_hiddenPanes.count(&pane)) { k++; continue; }   // hidden (View > Data Panes)
    bool open = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    bool visible = ImGui::Begin(pane.windowTitle().data(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) { noteFocus(pane.windowTitle().data()); _currentData = &pane; _tableActive = false; }
    if (visible) pane.render();
    ImGui::End();
    if (open) k++;
    else _dataPanes.erase(_dataPanes.begin() + k);
    }
  // table panes (Spreadsheet), closed with the x of their tab
  for (std::size_t k=0; k<_spreadsheetPanes.size(); )
    {
    SpreadsheetPane & pane = *_spreadsheetPanes[k];
    if (_hiddenPanes.count(&pane)) { k++; continue; }   // hidden (View > Tables)
    bool open = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    bool visible = ImGui::Begin(pane.windowTitle().data(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) { noteFocus(pane.windowTitle().data()); _currentTable = &pane; _tableActive = true; }
    pane.setShown(visible);
    if (visible) pane.render();
    ImGui::End();
    if (open) k++;
    else _spreadsheetPanes.erase(_spreadsheetPanes.begin() + k);
    }
  renderFitPane();
  if (_fitDialog.render(_panes.empty() ? nullptr : &currentPane().currentGraph())) setFitPaneVisible(true);
  _mapsDialog.render();
  if (areaX1 > areaX0) { _areaX = areaX0; _areaY = areaY0; _areaWidth = areaX1 - areaX0; _areaHeight = areaY1 - areaY0; }

  // panes added (+ button) or closed (x of their tab) by the user
  if (_addRequested)
    {
    if (beforePanesChange) beforePanesChange();
    addPane();
    _addRequested = false;
    }
  if (!_closeRequests.empty())
    {
    if (beforePanesChange) beforePanesChange();
    std::sort(_closeRequests.rbegin(), _closeRequests.rend());
    _closeRequests.erase(std::unique(_closeRequests.begin(), _closeRequests.end()), _closeRequests.end());
    std::vector<int> rest = onClosePanes ? onClosePanes(_closeRequests) : _closeRequests;
    for (int k : rest) closePane(k);
    _closeRequests.clear();
    }
  }

  void DatumGui::renderStatusBar(const MenuItem * hovered, const String & /*hoverText*/)
  {
  // left: menu item description, or the last status message (10 s), or a hint
  if (hovered && !hovered->description().isNull())
    ImGui::TextUnformatted(hovered->description().data());
  else if (!_status.isNull() && ImGui::GetTime() - _statusTime < 10.0)
    ImGui::TextUnformatted(_status.data());
  else
    ImGui::TextDisabled("Drag to zoom (double-click: automatic ranges); drag titles and legends to move them; right-click to edit");
  // right: coordinates under the mouse, current canvas
  char right[160];
  std::snprintf(right, sizeof(right), "%s    %s  (%d canvas%s)", _hoverText.data(),
                _panes.empty() ? "" : currentPane().title().data(), nPanes(), nPanes() > 1 ? "es" : "");
  float textWidth = ImGui::CalcTextSize(right).x;
  ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 12.0f);
  ImGui::TextUnformatted(right);
  }

  void DatumGui::renderMessage()
  {
  if (_messageRequested) { ImGui::OpenPopup("##message"); _messageRequested = false; }
  ImGui::SetNextWindowPos(activeViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("##message", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
    ImGui::TextUnformatted(_messageTitle.data());
    ImGui::Separator();
    ImGui::PushTextWrapPos(ImGui::GetFontSize()*35.0f);
    ImGui::TextUnformatted(_messageText.data());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    }
  }

  namespace
  {
  //! Draw into a page painter (SvgPainter or PdfPainter) and write it.
  template <class PAINTER>
  bool paint(const String & fileName, float width, float height, const std::function<void(PAINTER &)> & draw)
  {
  PAINTER painter(width, height);
  draw(painter);
  painter.endRegion();
  return painter.write(fileName);
  }
  } // namespace

  bool DatumGui::saveFigure(const String & fileName, int paneIndex, int graphIndex)
  {
  if (paneIndex >= nPanes()) return false;
  // the page: the area of the canvases, of one canvas, or of the cell of one graph (points = pixels on screen)
  float pageWidth = _areaWidth, pageHeight = _areaHeight;
  float x = 0, y = 0, w = 0, h = 0;
  if (paneIndex >= 0)
    {
    _panes[paneIndex]->canvasRect(x, y, w, h);
    pageWidth = w; pageHeight = h;
    if (graphIndex >= 0)
      {
      GRAPHICS::Canvas & canvas = _panes[paneIndex]->canvas();
      if (graphIndex >= canvas.nGraphs()) return false;
      const GRAPHICS::Graph & graph = canvas.graph(graphIndex);
      pageWidth  = (graph.cellXHigh() - graph.cellXLow())*w;
      pageHeight = (graph.cellYHigh() - graph.cellYLow())*h;
      }
    }
  if (pageWidth <= 0 || pageHeight <= 0) return false;
  // typeset the LaTeX texts of the canvases drawn in one run
  for (int k=0; k<nPanes(); k++)
    {
    if (paneIndex >= 0 && k != paneIndex) continue;
    float px, py, pw, ph;
    _panes[k]->canvasRect(px, py, pw, ph);
    if (pw > 0 && ph > 0) GRAPHICS::LatexRenderer::instance().prepare(_panes[k]->canvas(), pw, ph);
    }
  auto draw = [&](auto & painter)
    {
    if (paneIndex < 0)
      {
      for (auto & pane : _panes)
        {
        float px, py, pw, ph;
        pane->canvasRect(px, py, pw, ph);
        if (pw <= 0 || ph <= 0) continue;
        painter.beginRegion(px - _areaX, py - _areaY, pw, ph);
        pane->canvas().draw(painter);
        }
      return;
      }
    GRAPHICS::Canvas & canvas = _panes[paneIndex]->canvas();
    if (graphIndex < 0) { painter.beginRegion(0, 0, w, h); canvas.draw(painter); return; }
    // one graph: the canvas placed so that the cell of the graph fills the page (texts keep their size on screen)
    const GRAPHICS::Graph & graph = canvas.graph(graphIndex);
    painter.beginRegion(-graph.cellXLow()*w, -(1.0f - graph.cellYHigh())*h, w, h);
    std::vector<GRAPHICS::Point2D> cell{GRAPHICS::Point2D(graph.cellXLow(), graph.cellYLow()),
                                        GRAPHICS::Point2D(graph.cellXHigh(), graph.cellYLow()),
                                        GRAPHICS::Point2D(graph.cellXHigh(), graph.cellYHigh()),
                                        GRAPHICS::Point2D(graph.cellXLow(), graph.cellYHigh())};
    GRAPHICS::LineAttributes noLine;
    noLine.setStyle(0);
    painter.drawPolygon(cell, canvas.background(), noLine);
    graph.draw(painter);
    };
  if (FileSystem::hasExtension(fileName, ".pdf"))
    return paint<GRAPHICS::PdfPainter>(fileName, pageWidth, pageHeight, draw);
  return paint<GRAPHICS::SvgPainter>(fileName, pageWidth, pageHeight, draw);
  }

  } // namespace GUI
} // namespace Datum
