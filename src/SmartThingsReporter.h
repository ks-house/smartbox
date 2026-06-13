#ifndef SMARTTHINGS_REPORTER_H
#define SMARTTHINGS_REPORTER_H

#include "SmartBoxController.h"

class SmartThingsReporter {
private:
    static const char* webhookUrl;
    
public:
    static void init(const char* url = nullptr);
    static void reportStateChange(State prevState, State newState);
};

#endif // SMARTTHINGS_REPORTER_H
