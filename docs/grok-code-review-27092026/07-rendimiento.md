# Rendimiento — simple

C6 de un solo core, 4 MB flash, sin PSRAM. El presupuesto es el `loop()` de GPIO.

## Coste del loop

Cada vuelta: `usbPoll` + `channelsPoll` + `ledPoll`. Barato salvo:

1. **Hue HTTP en `channelFire`** (HIGH, ver 05). 8–16 s con el core ocupado en `HTTPClient`.
2. Pair al primer Wi-Fi up: `hueEnsureReady()` puede hacer mDNS + GETs + hasta 90 s de POSTs de pair. `gOnHueWait` bombea USB/LED; GPIO no se lee.
3. Improv scan: `scanNetworks` se aplaza un tick, pero el radio se desconecta (`WiFi.disconnect`) y el loop de GPIO sigue.

La task de consola (prio 1, 16 KB) corre el GET/POST de Vercel. Bien. Compite por el radio con Hue: un poll y un dim a la vez se serializan en el stack TCP.

## Snapshot

Cuatro streams Clip (light, room, zone, scene), 20 s cada uno en el peor caso. Sink de 20 KB. `jsonEachArrayObject` hace `malloc` por objeto. En una casa grande (decenas de scenes) hay ráfaga de alloc/free en C6.

`kMaxOwners = 64`: rooms con más de 64 luces-vía-device pierden `light_ids`. La consola entonces no puede validar targets contra ese room.

## Heap / stack

- Task consola 16 KB. Parse usa `gRecipeStage` global. `String` del body de config vive en heap.
- Loop task default Arduino (~8 KB). `channelFire` copia `HueRecipe` al stack (pequeño).
- `JsonDataSink` 20 KB heap por stream.
- Payload de register: concat de tres Strings de snapshot + 256. Casas grandes = pico de heap en la task de consola.

## Poll cadence

Sin header: 60 s si no hay recipes, 1 h si hay. Con header: 30–3600 s (la consola hoy manda 30 s “while editing” y 900 s idle). Confirm poll extra tras apply.

Un GET config 200 con body grande se parsea entero aunque `channels[]` no haya cambiado (no hay diff). Aceptable a 900 s; en 30 s durante edición es el coste de producto.

## MED

- Snapshot + register en la misma task que el GET config: un register lento (4 streams) retrasa el apply de un rev nuevo.
- No hay backoff exponencial en fallos de red; se reintenta al intervalo anterior. Un Bridge caído no satura (intervalo se mantiene), bien.
- Wi-Fi retry 10 s + `WiFi.begin()` puede pelear con Improv scan si el usuario enchufa USB a mitad.

## GOOD

- 204 no descarga body.
- Stream sink no materializa el JSON Clip crudo.
- GPIO no espera Vercel.
