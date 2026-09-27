# Cliente de consola — Simple

**Fuente:** `console.h`.

## Arquitectura

Task FreeRTOS `console` (16 KB, prio 1) corre `consoleDoSync`: bind bridge, register si hace falta, `consoleFetchConfig`. El `loop` no hace HTTP a Vercel.

Cadencia:
- Sin `X-Poll-Sec`: 60 s si no hay recipes, 1 h si hay.
- Con header: clamp 30–3600 s.
- Tras aplicar NVS: `gConsoleConfirmPoll` pide un poll inmediato (204 esperado).
- Register con recipes: como máximo cada 1 h aunque el poll sea rápido.

## Register

POST `/api/device/register` con `product:"simple"`, `source:"xiao"`, `mac`, `firmware`, `channels[]` GPIO, snapshot. Si `hueBuildSnapshot` falla, **no** POSTEA (keep last good).

## Config

GET `/api/device/config?mac=&rev=`. 204 = keep NVS. 200 parsea `recipes` + `channels[]`. Si `localRev >= remote` no escribe. Epoch `gNvsEpoch` descarta applies stale (HUECLR / wipe).

## Hallazgos

### MED — Parse exige `recipes` en el body

`recipesParseConfig` requiere keys `rev` y `recipes`. Un payload solo de `channels` sin recipes array falla entero.

### LOW — Fallback 60 s / 1 h vs consola 30 / 900

Consolas viejas sin header: idle 1 h (ok). Empty 60 s vs spec 30 s — menor.

### LOW — Confirm poll no distingue 204 de fallo de red en el log de rev

Funciona; el log puede confundir.
