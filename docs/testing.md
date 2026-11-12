# Testing

Following the ESPHome convention, the test suite is a set of **compile-time
integration tests** that exercise every platform, decode type, and option
(multiplier/offset, `on_erd_change` with each edge, distinct read/write ERDs,
diagnostic counters via lambda, etc.). If the schema, codegen, or generated
C++ regress, the build breaks.

Shared component config lives in
[`tests/components/gea/common.yaml`](../tests/components/gea/common.yaml)
(GEA3) and
[`tests/components/gea/common.gea2.yaml`](../tests/components/gea/common.gea2.yaml)
(GEA2). Each target framework has a thin platform wrapper:

| Target | GEA3 | GEA2 |
|--------|------|------|
| ESP32 + ESP-IDF | [`test.esp32-idf.yaml`](../tests/components/gea/test.esp32-idf.yaml) | [`test.esp32-idf.gea2.yaml`](../tests/components/gea/test.esp32-idf.gea2.yaml) |
| ESP32 + Arduino | [`test.esp32-arduino.yaml`](../tests/components/gea/test.esp32-arduino.yaml) | — |
| ESP8266 | [`test.esp8266.yaml`](../tests/components/gea/test.esp8266.yaml) | — |
| RP2040 | [`test.rp2040.yaml`](../tests/components/gea/test.rp2040.yaml) | — |

One extra fixture,
[`test.esp32-idf.erd-lookup.yaml`](../tests/components/gea/test.esp32-idf.erd-lookup.yaml),
enables `erd_lookup: true` to cover the `-DGEA_ERD_LOOKUP` codegen path — the
ERD table generated from the `erd-definitions` submodule. The fixtures above run
with it disabled.

CI compiles them in parallel on every PR.

## Running locally

```bash
esphome compile tests/components/gea/test.esp32-idf.yaml
```

> Only **ESP32-C3 (XIAO)** is verified on real hardware. The other platforms
> are exercised at build-time to catch portability regressions but have not
> been runtime-tested.

## Device configs

The ready-made appliance configs under [`devices/`](../devices/) are
configuration, not component code, so CI validates rather than compiles them: a
separate workflow runs `esphome config` over `devices/**/*.yaml` on every PR.
This catches schema mistakes (bad `decode`, malformed `options`, rejected
`byte_offset`/`erd`) without a per-device compile, and picks up new appliances
automatically — no CI matrix to edit when you add one. Component build coverage
stays with the fixtures above. Validate a config locally with:

```bash
esphome config devices/dishwasher/PDP715SYV0FS.yaml
```

## Lint

A separate workflow runs `clang-format --dry-run --Werror` on every C++ file
under `components/gea/` using the project's [`.clang-format`](../.clang-format).
Format your changes locally before pushing:

```bash
find components/gea -type f \( -name '*.h' -o -name '*.cpp' \) | xargs clang-format -i
```
