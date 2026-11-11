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

#include "Console/GplLogExport.h"

#include <QDebug>
#include <QMetaObject>
#include <vector>

#include "Console/GplLogFiles.h"

namespace Console {

/**
 * @brief Worker-thread owner of the per-device log files.
 */
class GplLogWorker final : public DataModel::FrameConsumerWorker<GplLogRecordPtr> {
public:
  GplLogWorker(moodycamel::ReaderWriterQueue<GplLogRecordPtr>* queue,
               std::atomic<bool>* enabled,
               std::atomic<size_t>* queueSize,
               std::atomic<bool>* open)
    : FrameConsumerWorker(queue, enabled, queueSize), m_open(open)
  {}

  void setOutputDirectory(const QString& directory)
  {
    const bool wasOpen = m_files.isOpen();
    m_files.setDirectory(directory);
    syncOpen();
    if (wasOpen)
      Q_EMIT resourceOpenChanged();
  }

  [[nodiscard]] bool isResourceOpen() const override { return m_files.isOpen(); }

protected:
  void processItems(const std::vector<GplLogRecordPtr>& items) override
  {
    const QString previousError = m_files.lastError();
    for (const auto& item : items)
      if (item && !item->text.isEmpty())
        (void)m_files.append(item->deviceId, item->text);

    (void)m_files.flush();
    syncOpen();

    const QString error = m_files.lastError();
    if (!error.isEmpty() && error != previousError)
      qWarning().noquote() << "[ConsoleLog]" << error;
  }

  void closeResources() override
  {
    m_files.close();
    syncOpen();
  }

private:
  void syncOpen() { m_open->store(m_files.isOpen(), std::memory_order_release); }

  GplLog::FileSet m_files;
  std::atomic<bool>* m_open;
};

//--------------------------------------------------------------------------------------------------
// GplLogExport
//--------------------------------------------------------------------------------------------------

/**
 * @brief Starts the bounded queue and writer thread with export disabled.
 */
GplLogExport::GplLogExport()
  : FrameConsumer({.queueCapacity = 16384, .flushThreshold = 512, .timerIntervalMs = 250})
  , m_open(false)
  , m_reportedOpen(false)
{
  initializeWorker();
  setConsumerEnabled(false);
  connect(m_worker,
          &DataModel::FrameConsumerWorkerBase::resourceOpenChanged,
          this,
          &GplLogExport::syncOpenState,
          Qt::QueuedConnection);
}

/**
 * @brief Flushes and stops the writer while qApp still exists.
 */
GplLogExport::~GplLogExport()
{
  stop();
}

/**
 * @brief Returns whether at least one device transcript is open.
 */
bool GplLogExport::isOpen() const noexcept
{
  return m_open.load(std::memory_order_acquire);
}

/**
 * @brief Returns whether new console chunks are accepted.
 */
bool GplLogExport::enabled() const noexcept
{
  return consumerEnabled();
}

/**
 * @brief Enables future chunks or drains and closes the current session when disabled.
 */
void GplLogExport::setEnabled(const bool enabled)
{
  if (consumerEnabled() == enabled)
    return;

  setConsumerEnabled(enabled);
  if (!enabled)
    close();
}

/**
 * @brief Changes the directory used by the next chunk, closing files from the old context first.
 */
void GplLogExport::setOutputDirectory(const QString& directory)
{
  if (!m_worker)
    return;

  auto* worker = static_cast<GplLogWorker*>(m_worker);
  QMetaObject::invokeMethod(
    worker, [worker, directory] { worker->setOutputDirectory(directory); }, Qt::QueuedConnection);
}

/**
 * @brief Queues one visible Console chunk for its device.
 */
void GplLogExport::append(const int deviceId, const QStringView text)
{
  if (!enabled() || text.isEmpty())
    return;

  enqueueData(std::make_shared<GplLogRecord>(deviceId, text.toString()));
}

/**
 * @brief Drains queued chunks and closes every device file.
 */
void GplLogExport::close()
{
  closeWorkerResources();
  syncOpenState();
}

/**
 * @brief Permanently stops the worker thread for application teardown.
 */
void GplLogExport::stop()
{
  if (!m_worker)
    return;

  stopWorker();
  m_open.store(false, std::memory_order_release);
  syncOpenState();
}

/**
 * @brief Constructs the worker attached to this facade's bounded queue.
 */
DataModel::FrameConsumerWorkerBase* GplLogExport::createWorker()
{
  return new GplLogWorker(&m_pendingQueue, &m_consumerEnabled, &m_queueSize, &m_open);
}

/**
 * @brief Publishes an open-state edge on the facade's thread.
 */
void GplLogExport::syncOpenState()
{
  const bool open = isOpen();
  if (m_reportedOpen == open)
    return;

  m_reportedOpen = open;
  Q_EMIT openChanged();
}

}  // namespace Console
