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

#include <atomic>
#include <memory>
#include <QString>
#include <QStringView>
#include <utility>

#include "Core/DataModel/FrameConsumer.h"

namespace Console {

/**
 * @brief One visible console chunk routed to the independent GPL log writer.
 */
struct GplLogRecord {
  int deviceId = -1;
  QString text;

  GplLogRecord() = default;

  GplLogRecord(int id, QString&& value) : deviceId(id), text(std::move(value)) {}
};

using GplLogRecordPtr = std::shared_ptr<GplLogRecord>;

/**
 * @brief Asynchronous GPL console transcript exporter, independent of the commercial writer.
 */
class GplLogExport final : public DataModel::FrameConsumer<GplLogRecordPtr> {
  Q_OBJECT

signals:
  void openChanged();

public:
  GplLogExport();
  ~GplLogExport() override;

  [[nodiscard]] bool isOpen() const noexcept;
  [[nodiscard]] bool enabled() const noexcept;

  void setEnabled(bool enabled);
  void setOutputDirectory(const QString& directory);
  void append(int deviceId, QStringView text);
  void close();
  void stop();

protected:
  DataModel::FrameConsumerWorkerBase* createWorker() override;

private slots:
  void syncOpenState();

private:
  std::atomic<bool> m_open;
  bool m_reportedOpen;
};

}  // namespace Console
