# GEA Refrigerator ERD Reference

This document contains reverse-engineered ERD information for GE Appliances using the GEA2 protocol.

The information below has been gathered through live testing of a GE Profile refrigerator using the ESPHome GEA component. Entries marked as **Confirmed** have been verified through testing. Entries marked **Assumed** or **Unknown** require additional validation.

Contributions and corrections are welcome.


# GEA Refrigerator ERD Reference

This document contains reverse-engineered GEA (GE Appliance) ERDs for GE refrigerators.

## Test Appliance

| Item | Value |
|------|-------|
| Manufacturer | GE Appliances |
| Model | GYE21JYMCFFS |
| Serial | LD383631 |
| Protocol | GEA2 |
| Appliance Address | 0xF0 |
| ESPHome Component | esphome-gea |

---

# Confirmed ERDs

## ERD 0x0001 - Model Number

**Length:** Variable (ASCII)

Example

```
GYE21JYMCFFS
```

Status: ✅ Confirmed

---

## ERD 0x0002 - Serial Number

**Length:** Variable (ASCII)

Example

```
LD383631
```

Status: ✅ Confirmed

---

## ERD 0x0007 – Temperature Display Units

**Length:** 1 byte

| Value | Meaning |
|------:|---------|
| 0x00 | Fahrenheit |
| 0x01 | Celsius |

Status: ✅ Confirmed

Verified by changing the refrigerator display units from the control panel.

---

## ERD 0x1004 - Display Temperature

**Length:** 2 bytes

| Byte | Description |
|-----:|-------------|
| 0 | Refrigerator temperature (°F) |
| 1 | Freezer temperature (signed °F) |

Examples

| Raw | Refrigerator | Freezer |
|------|-------------:|---------:|
| 2104 | 33°F | 4°F |
| 240F | 36°F | 15°F |
| 21FF | 33°F | -1°F |

Status: ✅ Confirmed

---

## ERD 0x1005 - Desired Temperature

**Length:** 2 bytes

| Byte | Description |
|-----:|-------------|
| 0 | Refrigerator setpoint |
| 1 | Freezer setpoint |

Example

```
2500
```

↓

```
37°F
0°F
```

Status: ✅ Confirmed

---

## ERD 0x1009 - Water Filter Status

**Length:** 9 bytes

Example

```
00006200B400000000
```

| Byte | Meaning |
|-----:|---------|
| 2 | Remaining filter life (%) |
| 4 | Days Remaining |

Status: 🟡 Partially decoded

---

## ERD 0x100B – Temperature Limits

Length: 4 bytes

| Byte | Meaning |
|------|---------|
| 0 | Refrigerator minimum setpoint |
| 1 | Refrigerator maximum setpoint |
| 2 | Freezer minimum setpoint (signed) |
| 3 | Freezer maximum setpoint (signed) |

Example:
22 2C FA 06

34°F
44°F
-6°F
6°F

Status: ✅ Confirmed

---

## ERD 0x1019 - Water Filter Type

**Length:** 1 byte

| Value | Meaning |
|------:|---------|
| 0x05 | XWFE |

Status: ✅ Confirmed

---

## ERD 0x1101 - Door Totals

**Length:** 16 bytes

Observed to increment after opening refrigerator/freezer doors.

Purpose appears to be lifetime counters.

Status: 🟡 Under investigation

---

## ERD 0x1109 - Engineering Snapshot

**Length:** 32 bytes

Contains changing engineering values.

Example

```
1047753007FFFC78FA2E1BCE7530753075307530753011FC07FF753075307530
```

Purpose unknown.

Status: ❓ Unknown

---

# Enumerations

## 0x0007 Temperature Display Units

| Value | Meaning |
|------:|---------|
| 0 | Fahrenheit |
| 1 | Celsius |

---

## 0x1019 Water Filter Type

| Value | Meaning |
|------:|---------|
| 5 | XWFE |

---

# Unknown ERDs

| ERD | Description | Status |
|------|-------------|--------|
| 0x1003 | Undocumented | Unknown |
| 0x1005 | Engineering Snapshot | Unknown - Current value is all zeros on a healthy refrigerator. |
| 0x1006 | Door Alarm Alert | Not decoded |
| 0x1007 | Ice Maker Bucket Status | Not decoded |
| 0x100A | Ice Maker Control | Returned FFFF |
| 0x100B | Temperature Limits | Not decoded |
| 0x100C | Humidity Control | Present in firmware but likely not applicable to GYE21JYMCFFS - Returns 0xFF -Unsupported |
| 0x100D | Quick Ice Status | Not decoded |
| 0x100E | Turbo Freeze Status | Not decoded |
| 0x100F | Turbo Cool Status | Not decoded |
| 0x1012 | Deli Pan Selection | Not decoded |
| 0x1013 | Deli Pan Desired Temperature | Not decoded |
| 0x1016 | Door Status | Does not appear to report live door state |
| 0x101C | Odor Filter Status | Returns 0xFF - No odor filter installed / unsupported |
| 0x101E | Nighttime Snack Mode | Not decoded |
| 0x101F | Nighttime Snack Timeout | Not decoded |
| 0x1102 | Dispenser Status | Did not change during dispenser testing |
| 0x1103 | Most Recent Cycle Status 1 | Not decoded |
| 0x1104 | Most Recent Cycle Status 2 | Not decoded |
| 0x110B | Undocumented | Unknown |
| 0x1111 | Undocumented | Unknown |
| 0x1112 | Undocumented | Unknown |
| 0x1113 | Undocumented | Unknown |
| 0x1114 | Undocumented | Unknown |
| 0x1115 | Undocumented | Unknown |
| 0x111B | Undocumented | Unknown |
| 0x1150 | Undocumented | Unknown |

---

Confirmed
✅ 0x0001 Model Number
✅ 0x0002 Serial Number
✅ 0x0007 Temperature Units
✅ 0x1004 Display Temperature
✅ 0x1005 Desired Temperature
✅ 0x100B Temperature Limits
✅ 0x1019 Water Filter Type
Partially Decoded
🟡 0x1009 Water Filter Status
Awaiting Test Method
⏳ 0x0009 Sabbath Mode
⏳ 0x000A Sound Level
⏳ 0x100A Ice Maker Control
⏳ 0x100C Humidity Control
⏳ 0x100D Quick Ice
⏳ 0x100E Turbo Freeze
⏳ 0x100F Turbo Cool

---
# Testing Notes

- Refrigerator uses the GEA2 protocol.
- ERD discovery identified 54 supported ERDs.
- Refrigerator temperature uses an unsigned byte.
- Freezer temperature uses a signed byte.
- Water filter model identified as XWFE.
- Water/Cubed/Crushed selection has not yet been located.
- Live door status has not yet been located.


# Test History

## Dispenser Selection

Test:
- Water
- Cubed
- Crushed

Observed ERDs:
- 0x100A
- 0x1102
- 0x1109
- 0x110B
- 0x1111
- 0x1112
- 0x1113
- 0x1114
- 0x1115
- 0x111B
- 0x1150

Result:
No confirmed ERD representing the active dispenser selection was identified.

---

## Door Status

Observed ERD:
0x1016

Result:
Does not appear to represent the real-time door open/closed state.

---

## Door Counters

Observed ERD:
0x1101

Result:
Values increment after opening refrigerator and freezer doors.
Likely contains lifetime counters or accumulated open time.
