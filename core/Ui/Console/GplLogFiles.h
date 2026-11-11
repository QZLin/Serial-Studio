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

#pragma once

#include <memory>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace Console::GplLog {

/**
 * @brief Removes CSI, OSC, DCS and two-byte ANSI escape sequences from console text.
 */
[[nodiscard]] QString stripAnsi(QStringView text);

/**
 * @brief Owns one UTF-8 log file per device for a single console recording session.
 *
 * This sink has no QObject or thread affinity. Its owner must call it from one worker thread.
 */
class FileSet final {
public:
  struct State;

  FileSet();
  ~FileSet();

  FileSet(FileSet&&)                 = delete;
  FileSet& operator=(FileSet&&)      = delete;
  FileSet(const FileSet&)            = delete;
  FileSet& operator=(const FileSet&) = delete;

  void setDirectory(const QString& directory, const QString& sessionStem = {});
  [[nodiscard]] bool append(int deviceId, QStringView text);
  [[nodiscard]] bool flush();
  void close();

  [[nodiscard]] bool isOpen() const;
  [[nodiscard]] QString lastError() const;
  [[nodiscard]] QStringList filePaths() const;

private:
  std::unique_ptr<State> m_state;
};

}  // namespace Console::GplLog
