/**
 * @file widget_qt_modules.h
 * @brief Canonical list of the Qt modules a Widget plugin may declare in its
 *        manifest's "qt_modules" array.
 *
 * The list is what the official PlotJuggler distribution ships. A module
 * outside it loads on a developer machine and fails on a user's, because the
 * deployment tools of the Linux and Windows packages do not walk plugin
 * dependencies the same way. Adding a module is one line here plus a MINOR
 * SDK release; the version column records the release that admitted it.
 *
 * Single source: CMake derives the installed
 * share/plotjuggler_sdk/widget_qt_modules.json from this file, so the two
 * cannot drift. Keep one X(...) entry per line and nothing else on it.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#ifndef PJ_WIDGET_QT_MODULES_H
#define PJ_WIDGET_QT_MODULES_H

/* X(module_name_without_Qt_prefix, "SDK version that admitted it") */
#define PJ_WIDGET_QT_MODULES(X) \
  X(Core, "0.39.0")             \
  X(Gui, "0.39.0")              \
  X(Widgets, "0.39.0")          \
  X(Svg, "0.39.0")              \
  X(Xml, "0.39.0")              \
  X(Network, "0.39.0")          \
  X(OpenGL, "0.39.0")           \
  X(OpenGLWidgets, "0.39.0")    \
  X(UiTools, "0.39.0")

#endif /* PJ_WIDGET_QT_MODULES_H */
