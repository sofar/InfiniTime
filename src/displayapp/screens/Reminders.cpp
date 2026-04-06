#include "displayapp/screens/Reminders.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/InfiniTimeTheme.h"
#include <cstdio>

using namespace Pinetime::Applications::Screens;

__attribute__((used)) static void RefreshTaskCallback(lv_task_t* task) {
  auto* screen = static_cast<Reminders*>(task->user_data);
  screen->Refresh();
}

Reminders::Reminders(Pinetime::Controllers::ReminderController& reminderController)
  : reminderController {reminderController} {

  // Title
  lv_obj_t* icon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::orange);
  lv_label_set_text_static(icon, Symbols::bell);
  lv_obj_align(icon, nullptr, LV_ALIGN_IN_TOP_LEFT, 10, 10);

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::orange);
  lv_label_set_text_static(title, "Reminders");
  lv_obj_align(title, icon, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

  // Last sync
  lv_obj_t* syncLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(syncLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_label_set_text_static(syncLabel, "Synced");
  lv_obj_align(syncLabel, nullptr, LV_ALIGN_IN_TOP_LEFT, 10, 45);

  labelSyncTime = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(labelSyncTime, "--");
  lv_obj_align(labelSyncTime, syncLabel, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

  // Count
  labelCount = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(labelCount, "0 active");
  lv_obj_align(labelCount, nullptr, LV_ALIGN_IN_TOP_LEFT, 10, 72);

  // Divider line
  static lv_point_t linePoints[] = {{10, 0}, {230, 0}};
  lv_obj_t* line = lv_line_create(lv_scr_act(), nullptr);
  lv_line_set_points(line, linePoints, 2);
  lv_obj_set_style_local_line_color(line, LV_LINE_PART_MAIN, LV_STATE_DEFAULT, Colors::gray);
  lv_obj_set_style_local_line_width(line, LV_LINE_PART_MAIN, LV_STATE_DEFAULT, 1);
  lv_obj_align(line, nullptr, LV_ALIGN_IN_TOP_LEFT, 0, 100);

  lv_obj_t* nextLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(nextLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_label_set_text_static(nextLabel, "Next");
  lv_obj_align(nextLabel, nullptr, LV_ALIGN_IN_TOP_LEFT, 10, 108);

  // Next reminder time — large font
  labelNextTime = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(labelNextTime, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_obj_set_style_local_text_color(labelNextTime, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
  lv_label_set_text_static(labelNextTime, "--:--");
  lv_label_set_align(labelNextTime, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(labelNextTime, nullptr, LV_ALIGN_CENTER, 0, 20);

  // Next reminder message — word-wrapped
  labelNextMessage = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_long_mode(labelNextMessage, LV_LABEL_LONG_BREAK);
  lv_obj_set_width(labelNextMessage, 220);
  lv_label_set_align(labelNextMessage, LV_LABEL_ALIGN_CENTER);
  lv_label_set_text_static(labelNextMessage, "");
  lv_obj_align(labelNextMessage, nullptr, LV_ALIGN_CENTER, 0, 68);

  taskRefresh = lv_task_create(RefreshTaskCallback, 10000, LV_TASK_PRIO_MID, this);
  Refresh();
}

Reminders::~Reminders() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void Reminders::Refresh() {
  uint8_t activeCount = reminderController.ActiveCount();
  uint8_t enabledCount = reminderController.EnabledCount();

  // Sync time
  auto syncTime = reminderController.LastSyncTime();
  auto tt = std::chrono::system_clock::to_time_t(
    std::chrono::time_point_cast<std::chrono::system_clock::duration>(syncTime));
  if (tt > 0) {
    tm* t = std::localtime(&tt);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d",
             t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);
    lv_label_set_text(labelSyncTime, buf);
  } else {
    lv_label_set_text_static(labelSyncTime, "Never");
  }

  // Count
  char countBuf[20];
  snprintf(countBuf, sizeof(countBuf), "%d reminders", enabledCount);
  lv_label_set_text(labelCount, countBuf);

  // Find next enabled reminder
  auto now = reminderController.GetDateTime().CurrentDateTime();
  time_t ttNow = std::chrono::system_clock::to_time_t(
    std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));

  const Pinetime::Controllers::Reminder* best = nullptr;
  int64_t bestSeconds = -1;

  for (uint8_t i = 0; i < activeCount; i++) {
    const auto& all = reminderController.GetAll();
    const auto& r = all[i];
    if (!r.IsEnabled()) continue;

    // Simple: compute seconds to today's occurrence
    tm* tmNow = std::localtime(&ttNow);
    tm candidate = *tmNow;
    candidate.tm_hour = r.hours;
    candidate.tm_min = r.minutes;
    candidate.tm_sec = 0;
    candidate.tm_isdst = -1;
    time_t candidateTime = std::mktime(&candidate);

    int64_t diff = static_cast<int64_t>(difftime(candidateTime, ttNow));
    if (diff > 0 && (bestSeconds < 0 || diff < bestSeconds)) {
      bestSeconds = diff;
      best = &r;
    }
  }

  if (best != nullptr) {
    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", best->hours, best->minutes);
    lv_label_set_text(labelNextTime, timeBuf);
    lv_label_set_text(labelNextMessage, best->message);
  } else if (activeCount > 0) {
    lv_label_set_text_static(labelNextTime, "--:--");
    lv_label_set_text_static(labelNextMessage, "No more today");
  } else {
    lv_label_set_text_static(labelNextTime, "--:--");
    lv_label_set_text_static(labelNextMessage, "No reminders");
  }

  lv_obj_align(labelNextTime, nullptr, LV_ALIGN_CENTER, 0, 20);
  lv_obj_align(labelNextMessage, nullptr, LV_ALIGN_CENTER, 0, 68);
}
