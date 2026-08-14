# Manual Hardware-In-The-Loop Validation

## Purpose

Compile, schema, fixture, and native byte tests are necessary but not sufficient for real heater safety. The tmp Arduino simulator frames and recent ESPHome logs provide hardware-derived packet evidence; this procedure records the minimum manual gates before claiming a live control behavior is operationally safe.

## Passive Validation

1. Use a known-good wiring setup and keep active mode disabled.
2. Confirm ESPHome boots, logs frames, and does not enqueue control frames.
3. Confirm decoded climate state, temperatures, flow, fan fields, and helper entities update from heater-originated packets.
4. Confirm offline/online transitions do not publish connected or stale state as current truth.
5. Capture logs for any unknown frames or checksum failures and convert them into fixtures before changing decode logic.

### XPS-100 / PC1001 RX Validation

Keep **Active Mode** off for this entire check. After flashing revision
`2026.05.15.11-xps100-sensors`, confirm the setup log reports that exact revision and
`Registered frame decoders: 12` before checking received values.
Change only the physical PC1001 target from 30 to 31 to 32 C and wait at least
one climate update interval after each change. The log should report
`XPS100 debug: short D2 target` with raw values `0x1E`, `0x1F`, and `0x20`, and
Home Assistant should follow with 30, 31, and 32 C respectively.

For the current temperature, record the first
`XPS100 debug: D1 inlet` or `XPS100 debug: D1B inlet` line. Its raw byte is full
D1 packet byte 9. After the short-D2 XPS signature is detected, this byte uses
extended half-degree encoding; the captured `0x83` candidate decodes to 35.5 C.
The XPS-100/PC1001 field test confirmed that physical target changes, including
20 C, reach Home Assistant after a few seconds and that the decoded T02 value
matches the displayed current/inlet water temperature.

### XPS-100 Sensor Check

After every reboot, first confirm **Active Mode** is off and **Update Sensors**
is on. Keep the bus passive during this check and wait at least one minute for
filtered helper values to publish.

1. Compare the climate current temperature with **Inlet Temperature**; both are
   sourced from T02 and should agree.
2. Compare the climate target with **Heating Setpoint**; the short XPS D2 frame
   is the authoritative passive fallback when CONFIG_1 is absent or delayed.
3. Check **Water Flow**, heater status, R08-R11 limits, defrost values, and fan
   values. A value may remain unavailable when the matching frame is not sent
   by this controller; do not invent a fallback from an unrelated byte.
4. Treat **Auxiliary COND 2 Temperature** as diagnostic only. Its byte position
   and decoding are fixture-backed, but its physical sensor identity is not.
5. Record short `DD` frames alongside the PC1001 T01-T06 service-menu values
   before assigning names to any of their bytes.

## Active-Control Smoke Test

Run active-control tests only when the heater can be supervised locally and stopped quickly.

Preconditions:

- Passive validation has already passed in the same wiring setup.
- The heater is in a safe operating state for the setting being changed.
- The operator knows how to disable active mode, power down the controller, and restore keypad settings.
- Only one setting is changed at a time.
- The expected command bytes are already covered by tests or reviewed against fixtures.

Procedure:

1. Enable active mode.
2. Change one low-risk helper or climate setting.
3. Watch the queued command log and the next heater-originated config frame.
4. Confirm the heater echoes the expected value and no unrelated fields changed.
5. Disable active mode after the observation window.

For the first XPS-100 active test, change only the climate target by 1 C. Verify
the queued CONFIG_1/R02 command, the physical PC1001 value, and the next
heater-originated target frame, then switch **Active Mode** off immediately.
Do not start with defrost, flow-meter, fan voltage, or fan timing controls.

Stop criteria:

- Unexpected command bytes are logged.
- Heater state changes outside the targeted field.
- Unknown, invalid, or repeated frames appear after a command.
- Physical heater behavior is surprising or unsafe.

## Observed Hardware Results

- ESP-IDF 5 RMT passive RX is field-stable on the hardware-test branch after moving RMT callback work out of ISR context. The component boots with bus startup enabled, decodes live heater frames, and continues publishing climate state.
- XPS-100/PC1001 passive RX is hardware-confirmed for the physical target and inlet/current water temperature. Target changes down to 20 C appear in Home Assistant after the next received update.
- CONFIG_5 defrost eco mode has passed a supervised active TX smoke test in both directions. The heater echoed `d06 defrost: ECO` after the ECO command and later echoed `d06 defrost: NORMAL` after the NORMAL command.
- TX/RX recovery remained stable after those writes. The duplicate RX re-arm warning `Failed to arm RMT RX: 259` was resolved by avoiding a second receive arm after transmit.
- Hardware echo showed the heater may normalize adjacent CONFIG_5 bytes while accepting the defrost mode change. Treat byte preservation around D05/U02-adjacent fields as protocol evidence to capture before broadening active-control claims.

## Next CONFIG_5 Target

The next low-risk active TX candidate is `u02_pulses_per_liter`, provided `u01_flow_meter` is still disabled in the current heater config. Test it one step away from the current value and then restore the original value in the same supervised session. Capture both command and heater echo packets, including any adjacent-byte normalization, before adding another active-TX fixture.

## Fan Control Notes

F02-F13 fan controls have compile coverage and native byte-helper tests based on hardware-derived packet evidence. Start with read/echo validation and the least disruptive settings before touching fan voltage limits or timing behavior.
