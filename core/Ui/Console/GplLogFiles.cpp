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

#include "Console/GplLogFiles.h"

#include <map>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSet>

namespace Console::GplLog {

//--------------------------------------------------------------------------------------------------
// ANSI filtering
//--------------------------------------------------------------------------------------------------

/**
 * @brief Parser states for ANSI control strings accepted by terminals.
 */
enum class EscapeState {
  Text,
  Escape,
  ControlSequence,
  ControlString,
  StringEscape
};

/**
 * @brief Returns true when @p code terminates a CSI control sequence.
 */
static bool isCsiFinal(const ushort code)
{
  return code >= 0x40 && code <= 0x7e;
}

/**
 * @brief Removes terminal control sequences without interpreting their payload.
 */
QString stripAnsi(const QStringView text)
{
  QString output;
  output.reserve(text.size());

  EscapeState state = EscapeState::Text;
  for (const QChar character : text) {
    const ushort code = character.unicode();
    switch (state) {
      case EscapeState::Text:
        if (code == 0x1b)
          state = EscapeState::Escape;
        else
          output.append(character);

        break;

      case EscapeState::Escape:
        if (character == QLatin1Char('['))
          state = EscapeState::ControlSequence;
        else if (character == QLatin1Char(']') || character == QLatin1Char('P')
                 || character == QLatin1Char('X') || character == QLatin1Char('^')
                 || character == QLatin1Char('_'))
          state = EscapeState::ControlString;
        else
          state = EscapeState::Text;

        break;

      case EscapeState::ControlSequence:
        if (isCsiFinal(code))
          state = EscapeState::Text;

        break;

      case EscapeState::ControlString:
        if (code == 0x07)
          state = EscapeState::Text;
        else if (code == 0x1b)
          state = EscapeState::StringEscape;

        break;

      case EscapeState::StringEscape:
        if (character == QLatin1Char('\\'))
          state = EscapeState::Text;
        else if (code != 0x1b)
          state = EscapeState::ControlString;

        break;
    }
  }

  return output;
}

//--------------------------------------------------------------------------------------------------
// FileSet state
//--------------------------------------------------------------------------------------------------

/**
 * @brief Private file state kept out of the public header.
 */
struct FileSet::State {
  QString directory;
  QString sessionStem;
  QString lastError;
  QStringList paths;
  QSet<int> failedDevices;
  std::map<int, std::unique_ptr<QFile>> files;
};

/**
 * @brief Constructs an empty console-log file set.
 */
FileSet::FileSet() : m_state(std::make_unique<State>()) {}

/**
 * @brief Closes every open log file.
 */
FileSet::~FileSet()
{
  close();
}

/**
 * @brief Returns a collision-free path for @p deviceId without overwriting an earlier session.
 */
static QString nextFilePath(const FileSet::State& state, const int deviceId)
{
  const QString device = deviceId >= 0 ? QStringLiteral("_device%1").arg(deviceId) : QString();
  const QString base   = state.sessionStem + device;
  QDir directory(state.directory);

  for (int suffix = 1; suffix <= 9999; ++suffix) {
    const QString name = suffix == 1 ? base + QStringLiteral(".txt")
                                     : QStringLiteral("%1_%2.txt").arg(base).arg(suffix);
    const QString path = directory.filePath(name);
    if (!QFile::exists(path))
      return path;
  }

  return {};
}

/**
 * @brief Sets the output directory and starts a new lazily-opened recording session.
 */
void FileSet::setDirectory(const QString& directory, const QString& sessionStem)
{
  close();
  m_state->directory = directory.trimmed().isEmpty() ? QString() : QDir::cleanPath(directory);
  m_state->sessionStem =
    sessionStem.isEmpty()
      ? QDateTime::currentDateTime().toString(QStringLiteral("yyyy_MMM_dd HH_mm_ss"))
      : sessionStem;
  m_state->lastError.clear();
  m_state->paths.clear();
  m_state->failedDevices.clear();
}

/**
 * @brief Opens the selected device's file on first use and writes a UTF-8 BOM.
 */
static QFile* ensureFile(FileSet::State& state, const int deviceId)
{
  const auto existing = state.files.find(deviceId);
  if (existing != state.files.end())
    return existing->second.get();

  if (state.directory.isEmpty() || state.failedDevices.contains(deviceId))
    return nullptr;

  QDir directory;
  if (!directory.mkpath(state.directory)) {
    state.lastError =
      QStringLiteral("Cannot create console log directory: %1").arg(state.directory);
    state.failedDevices.insert(deviceId);
    return nullptr;
  }

  const QString path = nextFilePath(state, deviceId);
  if (path.isEmpty()) {
    state.lastError =
      QStringLiteral("Cannot allocate a unique console log name in: %1").arg(state.directory);
    state.failedDevices.insert(deviceId);
    return nullptr;
  }

  auto file = std::make_unique<QFile>(path);
  if (!file->open(QIODeviceBase::WriteOnly | QIODeviceBase::NewOnly)) {
    state.lastError = QStringLiteral("Cannot open console log for writing: %1").arg(path);
    state.failedDevices.insert(deviceId);
    return nullptr;
  }

  static const QByteArray kUtf8Bom("\xef\xbb\xbf", 3);
  if (file->write(kUtf8Bom) != kUtf8Bom.size()) {
    state.lastError = QStringLiteral("Cannot write console log header: %1").arg(path);
    file->close();
    QFile::remove(path);
    state.failedDevices.insert(deviceId);
    return nullptr;
  }

  QFile* opened = file.get();
  state.files.emplace(deviceId, std::move(file));
  state.paths.append(path);
  return opened;
}

/**
 * @brief Appends visible console text for one device after removing terminal control sequences.
 */
bool FileSet::append(const int deviceId, const QStringView text)
{
  const QString plain = stripAnsi(text);
  if (plain.isEmpty())
    return true;

  QFile* file = ensureFile(*m_state, deviceId);
  if (!file)
    return false;

  const QByteArray bytes = plain.toUtf8();
  if (file->write(bytes) == bytes.size())
    return true;

  m_state->lastError =
    QStringLiteral("Short write while recording console log: %1").arg(file->fileName());
  file->close();
  m_state->files.erase(deviceId);
  m_state->failedDevices.insert(deviceId);
  return false;
}

/**
 * @brief Flushes all open device logs to the operating system.
 */
bool FileSet::flush()
{
  bool ok = true;
  for (auto& [deviceId, file] : m_state->files) {
    (void)deviceId;
    if (file && file->isOpen() && !file->flush()) {
      m_state->lastError = QStringLiteral("Cannot flush console log: %1").arg(file->fileName());
      ok                 = false;
    }
  }

  return ok;
}

/**
 * @brief Flushes and closes all device logs while preserving their paths for inspection.
 */
void FileSet::close()
{
  (void)flush();
  for (auto& [deviceId, file] : m_state->files) {
    (void)deviceId;
    if (file)
      file->close();
  }

  m_state->files.clear();
}

/**
 * @brief Returns true while at least one device log is open.
 */
bool FileSet::isOpen() const
{
  for (const auto& [deviceId, file] : m_state->files) {
    (void)deviceId;
    if (file && file->isOpen())
      return true;
  }

  return false;
}

/**
 * @brief Returns the latest directory, open or write failure.
 */
QString FileSet::lastError() const
{
  return m_state->lastError;
}

/**
 * @brief Returns every file opened by the current recording session.
 */
QStringList FileSet::filePaths() const
{
  return m_state->paths;
}

}  // namespace Console::GplLog
