# Método y límites — simple

## Qué se leyó

Árbol completo de `main`. Lectura línea a línea de:

- Contrato: `AGENTS.md`, `README.md`, `CHANGELOG.md`, `docs/firmware-artifacts.md`
- Runtime: `hue-simple-switch.ino`, `console.h`, `channels.h`, `recipes.h`, `hue.h`, `hue_discover.h`, `snapshot.h`, `usb.h`, `json_util.h`, `led.h`, `log.h`
- Build: `sketch.yaml`, `partitions.csv`, `config.example.h`, `config.product.h`, `THIRD_PARTY.json`
- CI: `.github/workflows/build.yml`, `.github/workflows/firmware.yml`
- Contrato de consola: `hue-switch-console/docs/device-api.md`, `AGENTS.md`

No se flasheó una placa. No se ejecutó el sketch. No se llamó a `hue.tineira.com` ni al Bridge.

## Qué no es esta review

- No es un spec. Los specs viven en la consola (`docs/specs/`).
- No es un diff de parche. No hay PRs ni cambios de código.
- No se auditó el core Arduino-ESP32 3.3.12 ni Improv como protocolo externo.
- No se midió heap/stack en dispositivo; los tamaños salen del código (`16384` task, `kMaxObj = 20480`, etc.).

## Criterio

Se compara el firmware contra:

1. Su propio `AGENTS.md` (producto `simple`, GPIO, USB, poll).
2. `docs/device-api.md` de la consola (endpoints, 204, `X-Poll-Sec`, snapshot, rev).
3. La review de consola del mismo día (P3/P4/P8/P11/P12).
4. Prácticas de firmware embebido: no bloquear el loop de input, no filtrar secretos por USB/logs, no `setInsecure()` contra la consola.

## IDs cruzados (consola)

| ID | En este firmware |
| --- | --- |
| P3 rev=0 al cambiar bridge | Firmware pone `rev=0` local (`recipesClear`). Consola ya hace bump. El primer poll debería traer body. |
| P4 snapshot vacío | Mitigado aquí: `hueBuildSnapshot` aborta si un stream ≠ 200. Un 200 con arrays vacíos reales sí se POSTea. |
| P8 poll en loop | Mitigado: task `"console"` 16 KB. |
| P11 apply con rev sin bump | Simple **no** aplica nada si `localRev >= remote`. Más estricto que Round. |
| P12 Wi-Fi no retry | Mitigado: retry cada 10 s. |
| P21 parser y espacio tras `:` | Cerrado: `jsonGetString` salta space/nl/tab. |

## Confianza

Alta en seguridad USB/TLS, contrato register/config y el bloqueo GPIO (está escrito en el comentario de `channelsPoll`). Media en tamaños NVS/heap de casas grandes (64 owners, malloc por objeto JSON) porque no se midió en placa.
