# Console poll / register — simple

`console.h`. Task FreeRTOS `"console"` 16 KB, prio 1. El `loop()` GPIO no espera ese HTTP. Eso cierra P8.

## Register — `POST /api/device/register`

Campos enviados:

- `mac` 12 hex minúsculas (`deviceMacHex`)
- `firmware` = `FIRMWARE_VERSION` (`0.4.1`)
- `bridgeid`, `bridge_ip`
- `product: "simple"`, `source: "xiao"`
- `channels[]` con `id`, `gpio`, `label` (sin `kind`; la consola lo elige)
- `lights` / `rooms` / `scenes` del snapshot compacto

Condiciones para ni siquiera POSTear:

- no hay URL+token válidos
- no hay Bridge id/IP
- `hueBuildSnapshot` falla (cualquier stream Clip ≠ 200)

Esto cierra P4 **desde esta placa** cuando Clip falla. Un Clip **200 con arrays vacíos** (casa sin luces, o Bridge raro) sí se POSTea. La consola igual puede vaciar el snapshot si otro cliente (`push-from-bridge`) manda `[]`.

Cadencia: al boot (vía `gNeedConsoleSync`) y luego como máximo cada 1 h si hay recipes. Sin recipes el fallback de poll es 60 s; el register sigue atado a “no registered o hourly”.

## Config — `GET /api/device/config?mac=&rev=`

- 204: keep NVS. No parse.
- 200: exige keys `rev` y `recipes`. Parse a `gRecipeStage` (buffer global, no stack). Si `localRev >= remote` keep NVS (P11-like, más estricto que Round: no toca timeout porque Simple no tiene).
- `X-Poll-Sec` clamp 30–3600. Sin header: 60 s vacío / 1 h armado.
- 401: poll 3600 s, no borra NVS.
- Otros errores: mantienen el intervalo anterior.
- `gNvsEpoch` aborta apply si hubo `HUECLR` a mitad de request.
- Tras save OK: `gConsoleConfirmPoll` para que la consola vea el rev aplicado (siguiente GET debería ser 204).

`consoleHttp` timeout 15 s. Authorization Bearer copiado de NVS en el momento del call (Simple no usa mutex de token; Round sí). Un HUESET concurrente puede ver un token a medias solo en teoría: `consoleSetToken` escribe String completo y luego la RAM.

## MED

- `recipesBindBridge`: si cambia `bridgeid`, `recipesClear()` pone **rev=0** local y save. La consola (2026-09-27) hace bump de rev al cambiar bridge (P3 fixed). El primer poll debería traer config nueva. Si una consola vieja no bump-ea, la placa se queda en 0 y puede ignorar un remote 0.
- Parse exige `recipes`. Un body Round (`pages` sin recipes shape) falla: correcto, este producto no es Round.
- `kMaxRecipes=16`. La consola no topea igual de duro en el wire; extras se droppean en `recipesParseOne`.
- Body de error de consola se loguea entero si `SERIAL_DEBUG`.

## GOOD

- GPIO no comparte el HTTP de consola.
- Mutex `gRecipesMux` entre task y GPIO.
- Confirm poll después de NVS write.
- 204 no lee body.
- Register no pisa snapshot si Hue no respondió 200.
