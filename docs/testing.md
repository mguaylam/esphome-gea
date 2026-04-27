# Testing

Following the ESPHome convention, the test suite is a set of **compile-time
integration tests** that exercise every platform, decode type, and option
(multiplier/offset, `on_erd_change` with each edge, distinct read/write ERDs,
diagnostic counters via lambda, etc.). If the schema, codegen, or generated
C++ regress, the build breaks.

Shared component config lives in
[`tests/components/gea/common.yaml`](../tests/components/gea/common.yaml).
Each target framework has a thin platform wrapper:

| Target | File |
|--------|------|
| ESP32 + ESP-IDF | [`tests/components/gea/test.esp32-idf.yaml`](../tests/components/gea/test.esp32-idf.yaml) |
| ESP32 + Arduino | [`tests/components/gea/test.esp32-arduino.yaml`](../tests/components/gea/test.esp32-arduino.yaml) |
| ESP8266 | [`tests/components/gea/test.esp8266.yaml`](../tests/components/gea/test.esp8266.yaml) |
| RP2040 | [`tests/components/gea/test.rp2040.yaml`](../tests/components/gea/test.rp2040.yaml) |

CI compiles all four in parallel on every PR.

## Running locally

```bash
esphome compile tests/components/gea/test.esp32-idf.yaml
```

> Only **ESP32-C3 (XIAO)** is verified on real hardware. The other platforms
> are exercised at build-time to catch portability regressions but have not
> been runtime-tested.

## Lint

A separate workflow runs `clang-format --dry-run --Werror` on every C++ file
under `components/gea/` using the project's [`.clang-format`](../.clang-format).
Format your changes locally before pushing:

```bash
find components/gea -type f \( -name '*.h' -o -name '*.cpp' \) | xargs clang-format -i
```
