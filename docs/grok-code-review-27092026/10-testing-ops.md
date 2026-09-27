# Testing y ops — simple

## CI

| Workflow | Trigger | Qué hace |
| --- | --- | --- |
| `build.yml` | pull_request | compile `xiao-c6`, `upload.maximum_size=1966080`, `SERIAL_DEBUG` 0 vía `config.product.h`. Sin secretos. |
| `firmware.yml` | push `main` + `workflow_dispatch` | compile, artifact `usb-installer-simple`, `POST https://hue.tineira.com/api/firmware/simple` con `FIRMWARE_UPLOAD_TOKEN`, release rolling `usb-installer`. |

El upload exige:

- secret `FIRMWARE_UPLOAD_TOKEN` presente (si falta, el job falla; no se loguea el valor);
- `CHANGELOG.md` con heading `### 0.4.1 — …` para la versión de `console.h`;
- `THIRD_PARTY.json` cubre cada platform/library de `sketch.yaml` a la misma versión.

`409 version_exists` = warning, no pisa bins. Hay que bump-ear `FIRMWARE_VERSION` para shippear bytes nuevos. Docs-only push actualiza notes.

Permissions: `contents: write` para el release de GitHub. El token de consola es un Actions secret, no está en el repo.

## Tests que no existen

- Parse de `recipesParseConfig` (body Simple, body Round, pretty-print, `rev` faltante, `channels` ausente = defaults).
- `recipeFromObject` (dim solo en hold, recall_scene targets vs target viejo, rtype inválido).
- Snapshot abort si stream ≠ 200; snapshot 200 vacío sí construye `[]`.
- `consoleLooksLikeToken` (corto, charset, sin `hsw_`).
- Debounce / double-click / hold no se pueden testear sin GPIO fake.

No hay host test target. Todo es `.h` inline + `.ino`. Extraer los parsers a `.cpp` testeable en x86 sería el camino; no es un pedido de esta review.

## Partitions / release

`partitions.csv` min_spiffs, ~1.9 MB APP × 2 (layout OTA) aunque OTA de producto no existe. Flash 4 MB. No Zigbee.

`sketch.yaml` pinnea Arduino-ESP32 **3.3.12**. Un bump de core sin bump de `THIRD_PARTY.json` falla el upload.

## Observabilidad

- `SERIAL_DEBUG` 0 en producto: cero logs USB. Diagnóstico = `HUEGET` + LED.
- LED: patrones de setup (ver `docs/specs/finished/led-status.md` en este repo).
- La consola ve `applied_rev` / `config_status` gracias al confirm poll y al `rev` query. Si el confirm se pierde, la UI muestra “pending” hasta el siguiente ciclo.

## MED

- `firmware.yml` imprime el body HTTP de la consola en el log de Actions (`echo "${BODY}"`). No debería traer el upload token (va en header). Sí puede traer `details` del API.
- Release rolling `usb-installer` clobber: un tag fijo, no un tag por versión. El historial de bins en GitHub Releases se pisa. La consola es la fuente de versiones.
- PR compile no corre `firmware.yml`. Un PR no puede romper el upload hasta merge a main.

## GOOD

- PR compile sin secretos.
- Credits y changelog bloquean el release.
- `SERIAL_DEBUG` 0 en el binario que se instala desde `/setup`.
