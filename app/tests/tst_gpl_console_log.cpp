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

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "Console/GplLogFiles.h"

class GplConsoleLogTest : public QObject {
  Q_OBJECT

private slots:
  void stripsTerminalSequences();
  void writesUtf8FilesPerDevice();
  void neverOverwritesAnExistingSession();
};

void GplConsoleLogTest::stripsTerminalSequences()
{
  const QString input = QStringLiteral("plain \033[31mred\033[0m \033]0;title\aend "
                                       "\033Pignored\033\\done");
  QCOMPARE(Console::GplLog::stripAnsi(input), QStringLiteral("plain red end done"));
}

void GplConsoleLogTest::writesUtf8FilesPerDevice()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());

  Console::GplLog::FileSet files;
  files.setDirectory(temporary.path(), QStringLiteral("fixed-session"));
  QVERIFY(files.append(2, QStringLiteral("\033[32mtemperature=23°C\033[0m\n")));
  QVERIFY(files.append(-1, QStringLiteral("default stream\n")));
  QVERIFY(files.flush());
  QVERIFY(files.isOpen());
  QCOMPARE(files.filePaths().size(), 2);
  files.close();
  QVERIFY(!files.isOpen());

  const QStringList paths = files.filePaths();
  for (const auto& path : paths) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    QVERIFY(bytes.startsWith("\xef\xbb\xbf"));
    QVERIFY(!bytes.contains('\x1b'));
  }

  const auto devicePath = paths.filter(QStringLiteral("_device2"));
  QCOMPARE(devicePath.size(), 1);
  QFile deviceFile(devicePath.constFirst());
  QVERIFY(deviceFile.open(QIODevice::ReadOnly));
  QVERIFY(deviceFile.readAll().contains(QStringLiteral("temperature=23°C").toUtf8()));
}

void GplConsoleLogTest::neverOverwritesAnExistingSession()
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());

  Console::GplLog::FileSet first;
  first.setDirectory(temporary.path(), QStringLiteral("same-session"));
  QVERIFY(first.append(4, QStringLiteral("first\n")));
  first.close();

  Console::GplLog::FileSet second;
  second.setDirectory(temporary.path(), QStringLiteral("same-session"));
  QVERIFY(second.append(4, QStringLiteral("second\n")));
  second.close();

  QDir directory(temporary.path());
  const QStringList logs = directory.entryList({QStringLiteral("*.txt")}, QDir::Files);
  QCOMPARE(logs.size(), 2);
  QVERIFY(logs.contains(QStringLiteral("same-session_device4.txt")));
  QVERIFY(logs.contains(QStringLiteral("same-session_device4_2.txt")));
}

QTEST_MAIN(GplConsoleLogTest)
#include "tst_gpl_console_log.moc"
