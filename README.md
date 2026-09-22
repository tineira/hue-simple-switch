# hue-simple-switch

Interruptor de pared Wi-Fi para **Seeed XIAO ESP32-C6**. Llama a la API local Clip v2 (HTTPS) del Bridge Hue. No es Zigbee.

Canales GPIO v1 (cerrado = pin a GND, `INPUT_PULLUP`):

| id | GPIO | kind | label |
| --- | --- | --- | --- |
| `boot` | 9 | `momentary` | BOOT |
| `d0` | 0 | `maintained` | D0 |
| `d1` | 1 | `maintained` | D1 |
| `d2` | 2 | `maintained` | D2 |

Cada canal tiene recetas por evento (`on` / `off` / `double_click` en maintained; `short` en BOOT). Las asigna la consola. El GPIO ejecuta NVS → Bridge; no espera a Vercel.

**BOOT** (GPIO9) y **RST** (CHIP_PU) son **botones**, no LEDs. En la placa hay dos luces:

Alfabeto del naranja, **implementado** en firmware 0.2.7: `docs/specs/finished/led-status.md`.

| Luz | Dónde | Quién la mueve |
| --- | --- | --- |
| Naranja (user, GPIO15 `LED_BUILTIN`) | Lado derecho, junto a RST | Este firmware |
| Roja (carga) | Junto al USB-C | Hardware del cargador, el sketch no la toca |

### LED naranja (firmware)

Un solo patrón a la vez, de arriba abajo. Al cambiar de peldaño la ráfaga empieza de cero. `LOW` en GPIO15 enciende el naranja (activo en bajo).

| Qué ves | Significa |
| --- | --- |
| Encendido fijo | Error de sistema: no se creó la tarea de consola, o la consola respondió 401. El 401 sigue fijo hasta una respuesta con código distinto de 401, o un `HUESET token` nuevo. Gana aunque no haya Wi-Fi. |
| Parpadeo continuo ~2 Hz (250 ms on / 250 ms off) | Sin Wi-Fi STA, o scan Improv. Aunque haya SSID guardado. |
| 2 destellos y pausa | Wi-Fi ok, sin consola (URL o token vacíos, o placeholder `your-…`). |
| 3 destellos y pausa | Wi-Fi y consola, sin Hue pareado, o pairing en curso / timeout. Hold 3 s en BOOT (re-pair) usa este patrón. |
| 4 destellos y pausa | Wi-Fi, consola y Hue, ninguna receta. |
| Un destello corto cada ~3 s | Armado: una o más recetas. |

Ráfaga (#2–#4): 100 ms encendido, 200 ms entre destellos, 1400 ms de pausa. El destello armado dura 80 ms y se repite cada 3 s.

### LED rojo (carga, no es el sketch)

| Qué ves | Significa |
| --- | --- |
| Encendido ~30 s al conectar USB sin batería | USB presente; luego se apaga. |
| Parpadea | Batería LiPo conectada y cargando por USB. |
| Apagado con USB y batería | Carga completa (o no hay ciclo de carga). |

Contrato: `hue-switch-console/docs/definiciones.md` y `docs/device-api.md`.

## Setup

1. Copia `config.example.h` a `config.h` y rellena `WIFI_SSID`, `WIFI_PASSWORD` (red **2.4 GHz**), `CONSOLE_URL` y `CONSOLE_TOKEN` (API key de aparato `hsw_…` creada en la consola).
2. El XIAO descubre el Bridge (mDNS `_hue._tcp`, NVS, `discovery.meethue.com`) y empareja la key Hue (tres destellos en el naranja → botón del Bridge). IP y key quedan en NVS, no en `config.h`.
3. Register: `POST /api/device/register` con `product: "simple"`, MAC, `channels[]` y snapshot (lights/rooms/scenes). Poll: `GET /api/device/config?mac=` (~1 min sin recetas; al boot y cada 1 h si hay).
4. Arduino IDE 2.3.10: abre `hue-simple-switch.ino`, placa **XIAO_ESP32C6**.
5. O arduino-cli: `arduino-cli compile --profile xiao-c6 .`

Listar lámparas (diagnóstico):

```
curl -k -H "hue-application-key: KEY" https://BRIDGE_IP/clip/v2/resource/light
```

`config.h` no se sube a git. Alta de producto = Arduino + `config.h` (no hay instalador web en este v1).
