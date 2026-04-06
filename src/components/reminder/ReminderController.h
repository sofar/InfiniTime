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
#pragma once

#include <FreeRTOS.h>
#include <timers.h>
#include <semphr.h>
#include <array>
#include <cstdint>
#include "components/datetime/DateTimeController.h"
#include "components/fs/FS.h"

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {

    struct Reminder {
      static constexpr uint8_t FormatVersion = 2;
      static constexpr uint8_t MaxReminders = 10;
      static constexpr uint8_t MaxMessageLen = 32;

      // Flags bitmask
      static constexpr uint8_t FlagEnabled  = 0x01;
      // Priority in bits 1-2: (flags >> 1) & 0x03
      static constexpr uint8_t FlagPriorityShift = 1;
      static constexpr uint8_t FlagPriorityMask  = 0x06;

      // Recurrence values
      static constexpr uint8_t RecurOnce  = 0x00; // fire once then disable (use month/day for specific date)
      static constexpr uint8_t RecurDaily = 0x80; // every day
      // bits 0-6: day-of-week bitmask (bit0=Sun, bit1=Mon, ..., bit6=Sat)

      uint8_t version = FormatVersion;
      uint8_t id = 0;           // 0-19, assigned by bridge
      uint8_t hours = 0;        // 0-23
      uint8_t minutes = 0;      // 0-59
      uint8_t recurrence = 0;   // 0x00=once, 0x80=daily, bits 0-6=weekday bitmask
      uint8_t flags = 0;        // bit0=enabled, bits 1-2=priority (0-2)
      uint8_t month = 0;        // 1-12 for specific date, 0=ignore
      uint8_t day = 0;          // 1-31 for specific date, 0=ignore
      char message[MaxMessageLen] = {0};

      bool IsEnabled() const {
        return flags & FlagEnabled;
      }

      void SetEnabled(bool en) {
        if (en)
          flags |= FlagEnabled;
        else
          flags &= ~FlagEnabled;
      }

      uint8_t Priority() const {
        return (flags & FlagPriorityMask) >> FlagPriorityShift;
      }

      void SetPriority(uint8_t p) {
        flags = (flags & ~FlagPriorityMask) | ((p << FlagPriorityShift) & FlagPriorityMask);
      }

      bool HasSpecificDate() const {
        return month > 0 && day > 0;
      }
    };

    class ReminderController {
    public:
      ReminderController(Controllers::DateTime& dateTimeController, Controllers::FS& fs);

      void Init(System::SystemTask* systemTask);

      bool AddOrUpdate(const Reminder& reminder);
      bool Delete(uint8_t id);
      void ClearAll();

      uint8_t Count() const;
      const Reminder* Get(uint8_t id) const;
      const std::array<Reminder, Reminder::MaxReminders>& GetAll() const {
        return reminders;
      }

      uint8_t ActiveCount() const {
        return count;
      }

      uint8_t EnabledCount() const;

      std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> LastSyncTime() const {
        return lastSyncTime;
      }

      void SetOffReminderNow();
      void StopAlerting();

      bool IsAlerting() const {
        return isAlerting;
      }

      uint8_t AlertingReminderId() const {
        return alertingId;
      }

      bool HasPendingAck() const {
        return pendingAck;
      }

      uint8_t PendingAckId() const {
        return pendingAckId;
      }

      uint32_t PendingAckTimestamp() const {
        return pendingAckTimestamp;
      }

      void ClearPendingAck();

      void ScheduleNextReminder();
      void SaveAndScheduleNow();
      void OnSaveTimerFired();

    private:
      static constexpr uint8_t reminderFormatVersion = 1;
      static constexpr const char* filePath = "/.system/reminders.dat";

      Controllers::DateTime& dateTimeController;
      Controllers::FS& fs;
      System::SystemTask* systemTask = nullptr;
      TimerHandle_t reminderTimer;
      TimerHandle_t saveTimer;
      SemaphoreHandle_t mutex;

      std::array<Reminder, Reminder::MaxReminders> reminders;
      uint8_t count = 0;

      bool isAlerting = false;
      uint8_t alertingId = 0;
      bool settingsChanged = false;

      bool pendingAck = false;
      uint8_t pendingAckId = 0;
      uint32_t pendingAckTimestamp = 0;

      std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> lastSyncTime;

      int FindById(uint8_t id) const;
      void LoadSettingsFromFile();
      void DeferSaveAndSchedule();

    public:
      // Public so DisplayApp can call it from its larger stack context
      void SaveSettingsToFile();

    private:

      // Reusable file handles — kept as members to avoid putting them on
      // SystemTask's tight 1400-byte stack during Load/Save operations.
      lfs_file_t lfsFile;
      lfs_dir lfsDir;
    };
  }
}
