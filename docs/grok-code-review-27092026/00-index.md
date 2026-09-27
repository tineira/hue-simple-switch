# Code review — hue-simple-switch

**Fecha:** 2026-09-27  
**Ref:** `main` @ `fb49517d`  
**Firmware:** `FIRMWARE_VERSION` 0.4.1 (`console.h`)  
**Chip:** XIAO ESP32-C6  
**Regla:** no se tocó código ni secrets. Solo esta carpeta.

## Archivos

| Archivo | Tema |
| --- | --- |
| [security.md](./security.md) | Tokens NVS, TLS Bridge vs consola, USB |
| [console-client.md](./console-client.md) | Register, poll, worker task |
| [gpio-and-loop.md](./gpio-and-loop.md) | Canales, debounce, hold/dim, bloqueo Hue |
| [hue-and-snapshot.md](./hue-and-snapshot.md) | Clip v2, snapshot, P4 |
| [nvs.md](./nvs.md) | Recipes, rev, bridge bind |
| [wifi-usb.md](./wifi-usb.md) | STA retry, Improv, HUESET |
| [contract.md](./contract.md) | vs console device-api / problems.md |
| [ci.md](./ci.md) | firmware.yml upload |

## Prioridad

1. Hue HTTP sigue en el loop GPIO (`channelFire`) — 8 s timeout, toggle hasta 16 s. Parte el double-click de 400 ms.
2. Confirmar parser JSON vs pretty-print (espacio tras `:`).
3. Contrato con consola: register ya manda `product:"simple"` y no pisa snapshot si Hue falla.
