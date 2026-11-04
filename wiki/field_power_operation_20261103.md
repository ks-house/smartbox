# SmartBox field power operation - 2026-11-03

## Reconnected device and authorization

Owner explicitly requests status, emergency release and close, then later requests fully open. HA device metadata and fresh MQTT telemetry confirm0.2.2-gdb96897, valid INA219/driver1/fault0/config0x399f/cal4096. Actual state is BATTERY_LOW_SHUTDOWN / power PROTECTED, rather than the separate emergency-stop FSM. Existing signed SSH status fails sudo password-required; owner later logs into Chrome HA and existing MQTT publish/listen is used. No credential, ACL, firewall, service or firmware changes.

## One requested close

Durable exclusive intent precedes one close_for_charging request with32hex ID/60s expiry/QoS0/non-retained. Live preflight:11.94803V/0.5mA, valid age778ms, close unused, no OTA/pending. Device admits at uptime1130907ms,11.90802V and close code1/used1. A later valid under-load observation is11.211V/1368.9mA, terminal close code3 LOW_VOLTAGE_STOP at uptime1131918ms with sample age812ms. This is below unchanged11.3V cutoff. Controller timestamp and measurement age are distinct; do not call collector time the exact physical stop time or infer full stroke duration.

Influx/Grafana collector receipts independently preserve20:13:23KST code1 and20:13:25 code3. Subsequent20:14/20:15/20:19 samples recover about11.94V/0.4mA while protection/code3/used1 remain. One motion request only; no retry/reboot or protection lowering. Owner cannot observe actual lid movement, so physical closed position and solar charging remain unknown.

## Later requested open

Owner then requests fully open. A second exclusive intent precedes one ordinary force_open publish, QoS0/non-retained. Source forceOpen requires power.actuationAllowed and excludes BATTERY_LOW_SHUTDOWN. Fresh before/after telemetry remains PROTECTED, actuation_allowed0, firmware0.2.2,11.94V and0.2-0.5mA idle motor branch. No opening state transition is observed. The old command has no command-specific admission ACK; publication is not successful actuation or physical open evidence. No emergency-reset, reboot or OTA commands are sent.

## Remaining boundaries and findings

- Requested full close and later full open are not established. Under-load voltage sag causes the close stop; battery capacity versus wiring/contact voltage loss is not distinguished by one measurement location.
- New owner buttons and new power entities are not visible in HA device UI. Current SGK scripts/render_broker_acl.py static discovery inventory still lists old IDs and excludes the new IDs. Operating ACL has not been read, so treat this as a supported candidate cause requiring live verification; no new permission is installed.
- Direct authenticated HA MQTT operation works in the owner's current session; this does not repair the separate signed-root sudo path or establish permanent unattended access.
- Data are motor-branch measurements, not solar current, whole-device current, SOC or energy. Normal release remains12.0V/300s; current close attempt is consumed for this boot.
- Private intents, sanitized live message receipts and Grafana screenshots remain outside Git. Field sensor/network runtime accepted; physical movement/charging and actual OTA health remain pending. Native57 and default C6 documentation-policy checks pass; generated header restored and dummy output not deployed.
