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

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#undef max
#undef min

#include "components/reminder/ReminderController.h"

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {

    class ReminderService {
    public:
      ReminderService(Pinetime::System::SystemTask& systemTask, Pinetime::Controllers::ReminderController& reminderController);
      void Init();

      int OnCommand(struct ble_gatt_access_ctxt* ctxt);

      void SendAckNotification(uint8_t reminderId, uint32_t timestamp);

    private:
      // 0006yyxx-78fc-48fe-8e23-433b3a1942d0
      static constexpr ble_uuid128_t CharUuid(uint8_t x, uint8_t y) {
        return ble_uuid128_t {.u = {.type = BLE_UUID_TYPE_128},
                              .value = {0xd0, 0x42, 0x19, 0x3a, 0x3b, 0x43, 0x23, 0x8e, 0xfe, 0x48, 0xfc, 0x78, y, x, 0x06, 0x00}};
      }

      static constexpr ble_uuid128_t BaseUuid() {
        return CharUuid(0x00, 0x00);
      }

      ble_uuid128_t serviceUuid {BaseUuid()};
      ble_uuid128_t uploadCharUuid {CharUuid(0x01, 0x00)};   // Write: upload/modify reminder
      ble_uuid128_t deleteCharUuid {CharUuid(0x02, 0x00)};   // Write: delete by ID
      ble_uuid128_t listCharUuid {CharUuid(0x03, 0x00)};     // Read: list all reminders
      ble_uuid128_t ackCharUuid {CharUuid(0x04, 0x00)};      // Notify: ack events
      ble_uuid128_t syncCharUuid {CharUuid(0x05, 0x00)};     // Write: clear all + bulk upload
      ble_uuid128_t statusCharUuid {CharUuid(0x06, 0x00)};  // Read: uptime + status

      struct ble_gatt_chr_def characteristicDefinition[7];
      struct ble_gatt_svc_def serviceDefinition[2];

      Pinetime::System::SystemTask& systemTask;
      Pinetime::Controllers::ReminderController& reminderController;

      uint16_t ackHandle {};
    };
  }
}
