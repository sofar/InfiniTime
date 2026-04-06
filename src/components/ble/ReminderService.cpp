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
#include "components/ble/ReminderService.h"
#include "systemtask/SystemTask.h"
#include <cstring>
#include <nrf_log.h>

using namespace Pinetime::Controllers;

int ReminderCallback(uint16_t /*connHandle*/, uint16_t /*attrHandle*/, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  return static_cast<ReminderService*>(arg)->OnCommand(ctxt);
}

ReminderService::ReminderService(System::SystemTask& systemTask, ReminderController& reminderController)
  : characteristicDefinition {
      {.uuid = &uploadCharUuid.u, .access_cb = ReminderCallback, .arg = this, .flags = BLE_GATT_CHR_F_WRITE},
      {.uuid = &deleteCharUuid.u, .access_cb = ReminderCallback, .arg = this, .flags = BLE_GATT_CHR_F_WRITE},
      {.uuid = &listCharUuid.u, .access_cb = ReminderCallback, .arg = this, .flags = BLE_GATT_CHR_F_READ},
      {.uuid = &ackCharUuid.u,
       .access_cb = ReminderCallback,
       .arg = this,
       .flags = BLE_GATT_CHR_F_NOTIFY,
       .val_handle = &ackHandle},
      {.uuid = &syncCharUuid.u, .access_cb = ReminderCallback, .arg = this, .flags = BLE_GATT_CHR_F_WRITE},
      {0}},
    serviceDefinition {
      {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &serviceUuid.u, .characteristics = characteristicDefinition},
      {0}},
    systemTask {systemTask},
    reminderController {reminderController} {
}

void ReminderService::Init() {
  int res;
  res = ble_gatts_count_cfg(serviceDefinition);
  ASSERT(res == 0);

  res = ble_gatts_add_svcs(serviceDefinition);
  ASSERT(res == 0);
}

int ReminderService::OnCommand(struct ble_gatt_access_ctxt* ctxt) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    size_t packetLen = OS_MBUF_PKTLEN(ctxt->om);

    if (ble_uuid_cmp(ctxt->chr->uuid, &uploadCharUuid.u) == 0) {
      // Upload/modify a single reminder (expects sizeof(Reminder) bytes)
      if (packetLen < sizeof(Reminder)) {
        NRF_LOG_WARNING("[ReminderService] Upload packet too small: %u", packetLen);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }

      Reminder reminder {};
      os_mbuf_copydata(ctxt->om, 0, sizeof(Reminder), &reminder);
      if (reminder.id >= Reminder::MaxReminders) {
        NRF_LOG_WARNING("[ReminderService] Invalid reminder ID: %u", reminder.id);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }

      if (!reminderController.AddOrUpdate(reminder)) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
      }
      NRF_LOG_INFO("[ReminderService] Uploaded reminder %u: %02d:%02d", reminder.id, reminder.hours, reminder.minutes);

    } else if (ble_uuid_cmp(ctxt->chr->uuid, &deleteCharUuid.u) == 0) {
      // Delete by ID (1 byte)
      if (packetLen < 1) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }

      uint8_t reminderId = 0;
      os_mbuf_copydata(ctxt->om, 0, 1, &reminderId);
      if (reminderId == 0xFF) {
        reminderController.ClearAll();
        NRF_LOG_INFO("[ReminderService] Cleared all reminders");
      } else {
        reminderController.Delete(reminderId);
        NRF_LOG_INFO("[ReminderService] Deleted reminder %u", reminderId);
      }

    } else if (ble_uuid_cmp(ctxt->chr->uuid, &syncCharUuid.u) == 0) {
      // Sync: 1 byte count + N * sizeof(Reminder) bytes
      if (packetLen < 1) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }

      uint8_t syncCount = 0;
      os_mbuf_copydata(ctxt->om, 0, 1, &syncCount);

      if (syncCount > Reminder::MaxReminders) {
        NRF_LOG_WARNING("[ReminderService] Sync count %u exceeds max", syncCount);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }

      size_t expectedSize = 1 + (syncCount * sizeof(Reminder));
      if (packetLen < expectedSize) {
        NRF_LOG_WARNING("[ReminderService] Sync packet too small: %u < %u", packetLen, expectedSize);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }

      reminderController.ClearAll();
      for (uint8_t i = 0; i < syncCount; i++) {
        Reminder reminder {};
        os_mbuf_copydata(ctxt->om, 1 + (i * sizeof(Reminder)), sizeof(Reminder), &reminder);
        if (reminder.id < Reminder::MaxReminders) {
          reminderController.AddOrUpdate(reminder);
        }
      }
      NRF_LOG_INFO("[ReminderService] Synced %u reminders", syncCount);
    }

  } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    if (ble_uuid_cmp(ctxt->chr->uuid, &listCharUuid.u) == 0) {
      // List all reminders: 1 byte count + N * sizeof(Reminder) structs
      uint8_t activeCount = reminderController.ActiveCount();
      int rc = os_mbuf_append(ctxt->om, &activeCount, sizeof(activeCount));
      if (rc != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
      }

      const auto& all = reminderController.GetAll();
      for (uint8_t i = 0; i < activeCount; i++) {
        rc = os_mbuf_append(ctxt->om, &all[i], sizeof(Reminder));
        if (rc != 0) {
          return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
      }
      NRF_LOG_INFO("[ReminderService] Listed %u reminders", activeCount);
    }
  }

  return 0;
}

void ReminderService::SendAckNotification(uint8_t reminderId, uint32_t timestamp) {
  uint16_t connectionHandle = systemTask.nimble().connHandle();
  if (connectionHandle == 0 || connectionHandle == BLE_HS_CONN_HANDLE_NONE) {
    return;
  }

  // 5 bytes: 1 byte ID + 4 bytes timestamp (little-endian)
  uint8_t data[5];
  data[0] = reminderId;
  data[1] = static_cast<uint8_t>(timestamp);
  data[2] = static_cast<uint8_t>(timestamp >> 8);
  data[3] = static_cast<uint8_t>(timestamp >> 16);
  data[4] = static_cast<uint8_t>(timestamp >> 24);

  auto* om = ble_hs_mbuf_from_flat(data, sizeof(data));
  ble_gattc_notify_custom(connectionHandle, ackHandle, om);
  NRF_LOG_INFO("[ReminderService] Sent ack for reminder %u", reminderId);
}
