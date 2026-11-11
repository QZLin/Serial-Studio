/*
 * Serial Studio
 * https://serial-studio.com/
 *
 * Copyright (C) 2026 QZLin
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QDebug>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>

#include "Console/Export.h"
#include "Core/Services.h"
#include "Core/WorkspaceManager.h"
#include "SelfTest/SelfTest.h"

namespace SelfTest {

/**
 * @brief Records one Console-log suite expectation.
 */
static void consoleLogCheck(SuiteResult& result, const bool ok, const char* description)
{
  ++result.checks;
  if (ok)
    return;

  ++result.failures;
  qCritical().noquote() << "[selftest]" << result.name << "FAILED:" << description;
}

/**
 * @brief Returns every .txt transcript below @p root.
 */
static QStringList consoleLogFiles(const QString& root)
{
  QStringList files;
  QDirIterator it(root, {QStringLiteral("*.txt")}, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext())
    files.append(it.next());

  files.sort();
  return files;
}

/**
 * @brief Exercises the packaged GPL writer through the QML/API-facing Export facade.
 */
void runGplConsoleLogSuite(SuiteResult& result, const SuiteEnvironment& env)
{
  consoleLogCheck(result, env.engine != nullptr, "post-root QML engine exists");

  QTemporaryDir temporary;
  consoleLogCheck(result, temporary.isValid(), "temporary workspace is available");
  if (!temporary.isValid())
    return;

  auto& workspace       = Core::services().workspaceManager;
  auto& exporter        = Console::Export::instance();
  const bool wasEnabled = exporter.exportEnabled();

  exporter.setSettingsPersistent(false);
  exporter.setExportEnabled(false);
  workspace.setTemporaryPath(temporary.path());
  exporter.setExportEnabled(true);

  const QString marker = QStringLiteral("\033[31mGPL-CONSOLE-LOG-SELFTEST\033[0m\n");
  exporter.registerData(7, marker);
  exporter.closeFile();
  exporter.setExportEnabled(false);

  const QStringList files = consoleLogFiles(temporary.path());
  consoleLogCheck(result, files.size() == 1, "one per-device transcript was created");

  QByteArray bytes;
  if (!files.isEmpty()) {
    QFile file(files.constFirst());
    if (file.open(QIODevice::ReadOnly))
      bytes = file.readAll();
  }

  consoleLogCheck(result, bytes.startsWith("\xef\xbb\xbf"), "transcript is UTF-8 with BOM");
  consoleLogCheck(result,
                  bytes.contains("GPL-CONSOLE-LOG-SELFTEST"),
                  "visible Console text reached the transcript");
  consoleLogCheck(result, !bytes.contains('\x1b'), "ANSI control sequences were removed");
  consoleLogCheck(result,
                  !files.isEmpty() && files.constFirst().contains(QStringLiteral("_device7")),
                  "device id is present in the transcript name");

  workspace.clearTemporaryPath();
  exporter.setExportEnabled(wasEnabled);
  exporter.setSettingsPersistent(true);
}

}  // namespace SelfTest
