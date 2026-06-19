#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include "SmartBoxController.h"
#include <ESPAsyncWebServer.h>

class WebDashboard {
private:
    static AsyncWebServer server;
    static SmartBoxController* controllerPtr;
    static void nasOtaTaskFunction(void *pvParameters);

public:
    static void init(SmartBoxController& controller);
    static void update();
};

#endif // WEB_DASHBOARD_H
