#include "SmartThingsReporter.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

struct StateChangeEvent {
    State prevState;
    State newState;
};

static QueueHandle_t eventQueue = NULL;
const char* SmartThingsReporter::webhookUrl = NULL;

void SmartThingsReporter::reporterTask(void* parameter) {
    StateChangeEvent ev;
    while(true) {
        // Block indefinitely until an event arrives in the queue
        if (eventQueue != NULL && xQueueReceive(eventQueue, &ev, portMAX_DELAY) == pdPASS) {
            // Only attempt connection if Wifi is connected and webhookUrl is registered
            if (WiFi.status() == WL_CONNECTED && SmartThingsReporter::webhookUrl != NULL) {
                HTTPClient http;
                http.begin(SmartThingsReporter::webhookUrl);
                http.addHeader("Content-Type", "application/json");
                
                String payload = "{";
                payload += "\"prevState\":" + String((int)ev.prevState) + ",";
                payload += "\"newState\":" + String((int)ev.newState);
                payload += "}";
                
                Serial.printf("[REPORTER] Dispatching SmartThings payload: %s\n", payload.c_str());
                int response = http.POST(payload);
                
                if (response > 0) {
                    Serial.printf("[REPORTER] Event dispatched successfully. Code: %d\n", response);
                } else {
                    Serial.printf("[REPORTER] Event dispatch failed: %s\n", http.errorToString(response).c_str());
                }
                http.end();
            } else {
                Serial.println("[REPORTER] Network offline or Webhook URL unregistered. Event skipped.");
            }
        }
    }
}

void SmartThingsReporter::init(const char* url) {
    webhookUrl = url;
    
    // Create thread-safe queue holding up to 10 events
    eventQueue = xQueueCreate(10, sizeof(StateChangeEvent));
    
    if (eventQueue != NULL) {
        // Spawn background worker task with low priority (Priority 1) to protect execution of core FSM
        xTaskCreate(
            reporterTask,
            "ST_ReporterTask",
            4096, // stack depth
            NULL,
            1,    // task priority
            NULL
        );
        Serial.println("[REPORTER] Background webhook dispatch task started.");
    } else {
        Serial.println("[REPORTER] Failed to initialize FreeRTOS queue.");
    }
}

void SmartThingsReporter::reportStateChange(State prevState, State newState) {
    if (eventQueue == NULL) return;
    
    StateChangeEvent ev = {prevState, newState};
    
    // Attempt non-blocking queue push (fails immediately if full to prevent main loop slowdown)
    if (xQueueSend(eventQueue, &ev, 0) != pdPASS) {
        Serial.println("[REPORTER] Event queue saturated! Dropped state report.");
    }
}
