#pragma once

#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/Controllers.h"
#include "components/reminder/ReminderController.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {

      class Reminders : public Screen {
      public:
        Reminders(Pinetime::Controllers::ReminderController& reminderController);
        ~Reminders() override;
        void Refresh() override;

      private:
        Pinetime::Controllers::ReminderController& reminderController;

        lv_obj_t* labelSyncTime;
        lv_obj_t* labelCount;
        lv_obj_t* labelNextTime;
        lv_obj_t* labelNextMessage;

        lv_task_t* taskRefresh;
      };
    }

    template <>
    struct AppTraits<Apps::Reminders> {
      static constexpr Apps app = Apps::Reminders;
      static constexpr const char* icon = Screens::Symbols::list;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::Reminders(controllers.reminderController);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
