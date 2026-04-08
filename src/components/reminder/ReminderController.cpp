/*  Copyright (C) 2024

    This file is part of InfiniTime.

    InfiniTime is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    InfiniTime is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "components/reminder/ReminderController.h"
#include "systemtask/SystemTask.h"
#include "task.h"
#include <chrono>
#include <cstring>
#include <algorithm>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;
using namespace std::chrono_literals;

ReminderController::ReminderController(Controllers::DateTime& dateTimeController, Controllers::FS& fs)
  : dateTimeController {dateTimeController}, fs {fs} {
}

namespace {
  void ReminderTimerCallback(TimerHandle_t xTimer) {
    auto* controller = static_cast<Pinetime::Controllers::ReminderController*>(pvTimerGetTimerID(xTimer));
    controller->SetOffReminderNow();
  }

  void SaveTimerCallback(TimerHandle_t xTimer) {
    auto* controller = static_cast<Pinetime::Controllers::ReminderController*>(pvTimerGetTimerID(xTimer));
    // Don't do heavy work here — timer task stack is only 1200 bytes.
    // Push a message to SystemTask which has a much larger stack.
    controller->OnSaveTimerFired();
  }
}

void ReminderController::Init(System::SystemTask* systemTask) {
  this->systemTask = systemTask;
  mutex = xSemaphoreCreateMutex();
  reminderTimer = xTimerCreate("Reminder", 1, pdFALSE, this, ReminderTimerCallback);
  // 2-second debounce timer for batching saves during bulk BLE writes
  saveTimer = xTimerCreate("ReminderSave", pdMS_TO_TICKS(2000), pdFALSE, this, SaveTimerCallback);
  LoadSettingsFromFile();
  ScheduleNextReminder();
}

int ReminderController::FindById(uint8_t id) const {
  for (size_t i = 0; i < reminders.size(); i++) {
    if (reminders[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool ReminderController::AddOrUpdate(const Reminder& reminder) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  int idx = FindById(reminder.id);
  if (idx >= 0) {
    // Compare — skip write if identical
    if (std::memcmp(&reminders[idx], &reminder, sizeof(Reminder)) == 0) {
      xSemaphoreGive(mutex);
      return true;
    }
    reminders[idx] = reminder;
    reminders[idx].version = Reminder::FormatVersion;
  } else {
    if (reminders.size() >= Reminder::MaxReminders) {
      NRF_LOG_WARNING("[ReminderController] Cannot add reminder, storage full");
      xSemaphoreGive(mutex);
      return false;
    }
    Reminder r = reminder;
    r.version = Reminder::FormatVersion;
    reminders.push_back(r);
  }
  settingsChanged = true;
  lastSyncTime = dateTimeController.CurrentDateTime();
  xSemaphoreGive(mutex);
  DeferSaveAndSchedule();
  return true;
}

uint8_t ReminderController::EnabledCount() const {
  uint8_t enabled = 0;
  for (const auto& r : reminders) {
    if (r.IsEnabled()) {
      enabled++;
    }
  }
  return enabled;
}

bool ReminderController::Delete(uint8_t id) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  int idx = FindById(id);
  if (idx < 0) {
    xSemaphoreGive(mutex);
    return false;
  }
  reminders.erase(reminders.begin() + idx);
  settingsChanged = true;
  xSemaphoreGive(mutex);
  DeferSaveAndSchedule();
  return true;
}

void ReminderController::ClearAll() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  reminders.clear();
  settingsChanged = true;
  xTimerStop(reminderTimer, 0);
  xSemaphoreGive(mutex);
  DeferSaveAndSchedule();
}

uint8_t ReminderController::Count() const {
  return static_cast<uint8_t>(reminders.size());
}

const Reminder* ReminderController::Get(uint8_t id) const {
  int idx = FindById(id);
  if (idx < 0) {
    return nullptr;
  }
  return &reminders[idx];
}

void ReminderController::ScheduleNextReminder() {
  xTimerStop(reminderTimer, 0);

  auto now = dateTimeController.CurrentDateTime();
  time_t ttNow = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));

  int64_t bestSeconds = -1;

  for (size_t i = 0; i < reminders.size(); i++) {
    if (!reminders[i].IsEnabled()) {
      continue;
    }

    time_t candidateTime;

    if (reminders[i].HasSpecificDate()) {
      // Specific date: use month + day fields
      tm* tmCandidate = std::localtime(&ttNow);
      tmCandidate->tm_mon = reminders[i].month - 1; // tm_mon is 0-based
      tmCandidate->tm_mday = reminders[i].day;
      tmCandidate->tm_hour = reminders[i].hours;
      tmCandidate->tm_min = reminders[i].minutes;
      tmCandidate->tm_sec = 0;
      tmCandidate->tm_isdst = -1;
      candidateTime = std::mktime(tmCandidate);

      // If this date has already passed this year, skip (one-shot specific date)
      if (candidateTime <= ttNow) {
        continue;
      }
    } else if (reminders[i].recurrence == Reminder::RecurDaily) {
      // Daily: next occurrence is today or tomorrow at the specified time
      tm* tmCandidate = std::localtime(&ttNow);
      tmCandidate->tm_hour = reminders[i].hours;
      tmCandidate->tm_min = reminders[i].minutes;
      tmCandidate->tm_sec = 0;
      tmCandidate->tm_isdst = -1;
      candidateTime = std::mktime(tmCandidate);
      if (candidateTime <= ttNow) {
        tmCandidate->tm_mday += 1;
        candidateTime = std::mktime(tmCandidate);
      }
    } else if (reminders[i].recurrence == Reminder::RecurOnce) {
      // Once: next occurrence at the specified time
      tm* tmCandidate = std::localtime(&ttNow);
      tmCandidate->tm_hour = reminders[i].hours;
      tmCandidate->tm_min = reminders[i].minutes;
      tmCandidate->tm_sec = 0;
      tmCandidate->tm_isdst = -1;
      candidateTime = std::mktime(tmCandidate);
      if (candidateTime <= ttNow) {
        tmCandidate->tm_mday += 1;
        candidateTime = std::mktime(tmCandidate);
      }
    } else {
      // Day-of-week bitmask: find next matching day
      tm* tmCandidate = std::localtime(&ttNow);
      tmCandidate->tm_hour = reminders[i].hours;
      tmCandidate->tm_min = reminders[i].minutes;
      tmCandidate->tm_sec = 0;
      tmCandidate->tm_isdst = -1;
      candidateTime = std::mktime(tmCandidate);
      if (candidateTime <= ttNow) {
        tmCandidate->tm_mday += 1;
        candidateTime = std::mktime(tmCandidate);
      }

      uint8_t recurrence = reminders[i].recurrence;
      for (int attempt = 0; attempt < 7; attempt++) {
        tm* check = std::localtime(&candidateTime);
        uint8_t dayBit = 1 << check->tm_wday;
        if (recurrence & dayBit) {
          break;
        }
        check->tm_mday += 1;
        check->tm_isdst = -1;
        candidateTime = std::mktime(check);
      }
    }

    int64_t secondsToThis = static_cast<int64_t>(difftime(candidateTime, ttNow));
    if (secondsToThis > 0 && (bestSeconds < 0 || secondsToThis < bestSeconds)) {
      bestSeconds = secondsToThis;
      alertingId = reminders[i].id;
    }
  }

  if (bestSeconds > 0) {
    TickType_t ticks = static_cast<TickType_t>(bestSeconds) * configTICK_RATE_HZ;
    if (ticks == 0) {
      ticks = 1;
    }
    xTimerChangePeriod(reminderTimer, ticks, 0);
    xTimerStart(reminderTimer, 0);
    NRF_LOG_INFO("[ReminderController] Next reminder in %d seconds", static_cast<int>(bestSeconds));
  }
}

void ReminderController::SetOffReminderNow() {
  isAlerting = true;
  systemTask->PushMessage(System::Messages::SetOffReminder);
}

void ReminderController::StopAlerting() {
  if (!isAlerting) {
    return;
  }
  isAlerting = false;

  // Record pending acknowledgment
  auto now = dateTimeController.CurrentDateTime();
  time_t ttNow = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
  pendingAck = true;
  pendingAckId = alertingId;
  pendingAckTimestamp = static_cast<uint32_t>(ttNow);

  // Handle recurrence
  int idx = FindById(alertingId);
  if (idx >= 0) {
    if (reminders[idx].recurrence == Reminder::RecurOnce) {
      reminders[idx].SetEnabled(false);
      settingsChanged = true;
      SaveSettingsToFile();
    }
  }

  ScheduleNextReminder();
}

void ReminderController::ClearPendingAck() {
  pendingAck = false;
}

void ReminderController::LoadSettingsFromFile() {
  if (fs.FileOpen(&lfsFile, filePath, LFS_O_RDONLY) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[ReminderController] Failed to open reminders file");
    return;
  }

  uint8_t storedCount = 0;
  fs.FileRead(&lfsFile, &storedCount, sizeof(storedCount));

  if (storedCount > Reminder::MaxReminders) {
    NRF_LOG_WARNING("[ReminderController] Stored count %u exceeds max, discarding", storedCount);
    fs.FileClose(&lfsFile);
    return;
  }

  reminders.clear();
  for (uint8_t i = 0; i < storedCount; i++) {
    Reminder r;
    fs.FileRead(&lfsFile, reinterpret_cast<uint8_t*>(&r), sizeof(Reminder));
    if (r.version == Reminder::FormatVersion) {
      reminders.push_back(r);
    }
  }
  fs.FileClose(&lfsFile);

  NRF_LOG_INFO("[ReminderController] Loaded %u reminders from file", reminders.size());
}

void ReminderController::SaveSettingsToFile() {
  if (fs.DirOpen("/.system", &lfsDir) == LFS_ERR_OK) {
    fs.DirClose(&lfsDir);
  } else {
    fs.DirCreate("/.system");
  }

  if (fs.FileOpen(&lfsFile, filePath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[ReminderController] Failed to open reminders file for saving");
    return;
  }

  uint8_t cnt = static_cast<uint8_t>(reminders.size());
  fs.FileWrite(&lfsFile, &cnt, sizeof(cnt));
  fs.FileWrite(&lfsFile, reinterpret_cast<const uint8_t*>(reminders.data()), cnt * sizeof(Reminder));
  fs.FileClose(&lfsFile);

  NRF_LOG_INFO("[ReminderController] Saved %u reminders to file", cnt);
}

void ReminderController::DeferSaveAndSchedule() {
  // Reset the 2-second debounce timer. If another write arrives before it
  // expires, the timer restarts — so save + schedule only runs once after
  // the last write in a batch.
  xTimerReset(saveTimer, 0);
}

void ReminderController::OnSaveTimerFired() {
  systemTask->PushMessage(System::Messages::SaveReminders);
}

void ReminderController::SaveAndScheduleNow() {
  // Called from SystemTask context (1400B stack) — only schedule here.
  // File save is forwarded to DisplayApp (3200B stack) separately.
  xSemaphoreTake(mutex, portMAX_DELAY);
  settingsChanged = false;
  ScheduleNextReminder();
  NRF_LOG_INFO("[ReminderController] Schedule complete (%u reminders), save deferred to DisplayApp", reminders.size());
  xSemaphoreGive(mutex);
}
