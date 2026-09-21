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

Contrato: `hue-switch-console/docs/definiciones.md` y `docs/device-api.md`.

## Setup

1. Copia `config.example.h` a `config.h` y rellena `WIFI_SSID`, `WIFI_PASSWORD` (red **2.4 GHz**), `CONSOLE_URL` y `CONSOLE_TOKEN` (API key de aparato `hsw_…` creada en la consola).
2. El XIAO descubre el Bridge (mDNS `_hue._tcp`, NVS, `discovery.meethue.com`) y empareja la key Hue (LED parpadea → botón del Bridge). IP y key quedan en NVS, no en `config.h`.
3. Register: `POST /api/device/register` con `product: "simple"`, MAC, `channels[]` y snapshot (lights/rooms/scenes). Poll: `GET /api/device/config?mac=` (~1 min sin recetas; al boot y cada 1 h si hay).
4. Arduino IDE 2.3.10: abre `hue-simple-switch.ino`, placa **XIAO_ESP32C6**.
5. O arduino-cli: `arduino-cli compile --profile xiao-c6 .`

Listar lámparas (diagnóstico):

```
curl -k -H "hue-application-key: KEY" https://BRIDGE_IP/clip/v2/resource/light
```

`config.h` no se sube a git. Alta de producto = Arduino + `config.h` (no hay instalador web en este v1).
