#ifndef SMARTBOX_RECOVERY_OTA_REQUEST_H
#define SMARTBOX_RECOVERY_OTA_REQUEST_H
#include <cstdint>
// Broker-authorized control request; this validates freshness/shape, not a new identity.
bool validateRecoveryOtaRequest(const char* requestId,uint64_t expiresAt,uint64_t now,uint32_t& ttlMs);
#endif
