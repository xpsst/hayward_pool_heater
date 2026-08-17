# Loxone and PV Surplus Integration

This integration keeps the heat-pump controller as the normal start/stop authority. Loxone sends climate requests through the ESP32/PC1001 bus. A Shelly 1PM measures real electrical power; its relay should remain on during normal automation instead of hard-cutting a running compressor.

## ESPHome Configuration

The Loxone routes are disabled by default. Enable them only on a web server protected with a dedicated LAN-only Basic Auth account. Loxone Virtual HTTP Input supports Basic Auth, not Digest Auth.

```yaml
web_server:
  auth:
    type: basic
    username: !secret loxone_hwp_user
    password: !secret loxone_hwp_password

climate:
  - platform: hwp
    id: pool_heater
    name: "Pool Heater"
    pin_txrx: GPIO22
    update_interval: 10s
    web_ui:
      enabled: true
      path: /hwp
      loxone_api: true
```

Do not expose this HTTP service to the internet. Basic Auth protects the routes but does not encrypt the password. Keep the ESP32 and Miniserver on a trusted LAN or isolated automation VLAN.

## Status Route

Poll this route no faster than every 10 seconds:

```text
http://USER:PASSWORD@ESP_IP/hwp/loxone/state
```

The response is compact JSON. Numeric codes are stable for Loxone command recognition:

| Field | Values |
|---|---|
| `online` | `0` offline, `1` receiving heater frames |
| `control_enabled` | `0` passive/read-only, `1` Active Mode enabled |
| `xps100_detected` | `0` not identified, `1` PC1001 signature detected |
| `mode_code` | `-1` unknown, `0` off, `1` heat, `2` cool, `3` auto |
| `action_code` | `-1` unknown, `0` off, `1` idle, `2` heating, `3` cooling, `4` defrosting, `5` fan, `6` drying |
| `water_flow` | `-1` unknown, `0` no/disabled, `1` yes/enabled; verify S02 behavior on the installed heater before using it as an interlock |
| `fault` | `0` PC1001 status `S00`, `1` any other/unknown status |
| `current_temperature` | T02 inlet/current pool-water temperature in degrees Celsius, or `null` |
| `outlet_temperature` | T03 outlet temperature in degrees Celsius, or `null`; diagnostic until confirmed on this XPS-100 |
| `ambient_temperature` | T05 ambient temperature in degrees Celsius, or `null`; diagnostic until confirmed on this XPS-100 |
| `coil_temperature` | T04 coil temperature in degrees Celsius, or `null`; diagnostic until confirmed on this XPS-100 |
| `target_temperature` | Current PC1001 setpoint in degrees Celsius, or `null` |
| `max_heating_temperature` | Decoded R11 heating limit in degrees Celsius, or `null` |

Create one **Virtual HTTP Input** and child commands with these recognition strings:

```text
"online":\v
"control_enabled":\v
"current_temperature":\v
"outlet_temperature":\v
"ambient_temperature":\v
"coil_temperature":\v
"target_temperature":\v
"mode_code":\v
"action_code":\v
"water_flow":\v
"fault":\v
"max_heating_temperature":\v
"last_heater_frame_age_ms":\v
```

## Control Routes

Create a Loxone **Virtual Output** with address:

```text
http://USER:PASSWORD@ESP_IP
```

Add virtual commands whose **Command on ON** values are:

```text
/hwp/loxone/control?mode=off
/hwp/loxone/control?mode=heat
/hwp/loxone/control?mode=cool
/hwp/loxone/control?mode=auto
```

For an analog target command use:

```text
/hwp/loxone/control?target=\v
```

Validate the Loxone analog input to the currently proven range and use 0.5 C steps. A request receives HTTP `202` only when Active Mode is enabled and heater frames are online. Passive mode returns `409`; an offline heater returns `503`; invalid values return `400` or `422`. The API deliberately has no route that can enable Active Mode. After every ESP restart, Active Mode is off and must be re-enabled deliberately in Home Assistant during the commissioning phase.

Send a mode request first, wait until `mode_code` confirms it, then send the target. This avoids applying a heating setpoint while the controller still reports another mode.

## Supervised Commissioning

Target changes and HEAT have already been observed on the XPS-100. Confirm the remaining operations one at a time before connecting automatic logic:

1. Keep the Shelly relay on and confirm `online=1`, `fault=0`, valid T02, and the expected target.
2. Enable **Active Mode** manually.
3. Change the target by only 0.5 or 1.0 C and wait for the PC1001 echo.
4. Test OFF while the compressor is not urgently required; verify `mode_code=0` and falling Shelly power.
5. Test HEAT and AUTO separately and verify the echoed mode after each request.
6. Test COOL only when the hydraulics and operating conditions make cooling safe.
7. Disable Active Mode after the test. Preserve logs for any request that is not echoed.

## Shelly Role

Use Shelly active power as confirmation that the compressor is really running and to measure the XPS-100's actual demand. Do not cycle PV surplus by opening the Shelly relay. Normal stop is `mode=off` through PC1001, allowing the controller to perform its fan/pump delays and compressor protection.

The exact Shelly endpoint depends on the model generation. For Plus/Gen2-family devices, `GET /rpc/Switch.GetStatus?id=0` exposes `apower`; older Shelly 1PM firmware uses a different status route. Confirm the exact label and generation before creating the Loxone template. If the Shelly relay is in the heat-pump supply, an electrician must verify motor/compressor suitability and whether a contactor is required; relay ampere ratings alone do not cover compressor inrush.

## Recommended PV State Machine

Use Loxone's Energy Manager or equivalent logic with these signals: grid import/export, PV production, T02, target, mode/action, PC1001 fault, heater online age, Shelly power, pool circulation availability, manual PV enable, and a comfort/boost temperature limit.

Recommended initial behavior:

1. **Ready:** require heater online, `fault=0`, circulation available, Active Mode enabled, and T02 below the PV target by at least 0.5 C.
2. **Start:** require continuous surplus above the measured running power plus a reserve for 5 minutes. Request HEAT, wait for mode confirmation, then request the PV target.
3. **Run:** confirm `action_code=2` and Shelly power above the measured compressor-on threshold. Ignore short clouds.
4. **Stop normally:** request OFF when the PV target is reached or grid import exceeds the chosen limit continuously for 5-10 minutes. Keep Shelly power connected.
5. **Protect:** use at least 20 minutes minimum run time and 10 minutes minimum off time for PV decisions. Fault, loss of heater communication, or loss of circulation may bypass the run timer and request OFF.
6. **Fail safe:** if the ESP or bus is unavailable, stop issuing commands and alarm. Do not use a blind periodic ON command.

Measure one complete heating cycle with the Shelly before choosing watt thresholds. The start threshold should be derived from that measurement, not from the heat pump's catalog rating.
