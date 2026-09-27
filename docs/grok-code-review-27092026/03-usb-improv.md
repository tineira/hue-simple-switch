# USB e Improv — simple

Archivo: `usb.h`. Parser único sobre `Serial` (CDC). Estados `USB_IDLE` / `USB_IMPROV` / `USB_ASCII`.

## Comandos ASCII (contrato devices.md §6)

| Línea | Respuesta | Efecto |
| --- | --- | --- |
| `HUESET token <tok>` | `HUEOK token` / `HUEERR …` | NVS `console/token`, limpia 401, `gNeedConsoleSync` |
| `HUESET url <url>` | `HUEOK url` / `HUEERR url` | NVS `console/url` si empieza por `http://` o `https://` |
| `HUEGET` | `HUESTA mac=… product=simple ver=… chip=c6 ssid=… wifi=… ip=… bid=… bip=… url=… token=0\|1 key=0\|1` | No secretos |
| `HUEPAIR` | `HUEOK pair` / `HUEERR no-wifi` | Arranca state-machine de pair (`huePairSessionBegin`) |
| `HUECLR` | `HUEOK clear` | STA + console + hue + recipes + epoch |
| `HUEBOOT` | `HUEOK boot` + reset | `LP_AON_FORCE_DOWNLOAD_BOOT` + `esp_restart()` |
| otra cosa | `HUEERR unknown` | — |

Valores de `HUESTA` van percent-encoded (`usbAppendPct`).

## Improv Serial

- Magic `IMPROV` + ver 1 + type + len + payload + checksum 8-bit + `\n`.
- RPC: Wi-Fi settings, state, info (`hue-simple-switch`, `FIRMWARE_VERSION`, `XIAO_ESP32C6/esp32-c6`), scan.
- Scan **asíncrono**: el ACK de estado sale ya; `scanNetworks` corre en el siguiente `usbPoll`. Evita 4 s de silencio que el wizard lee como muerto.
- Scan retry cada ~400 ms hasta 15 s si `WIFI_SCAN_FAILED` o 0 resultados tempranos.
- Connect timeout 20 s (`kImprovConnectMs`). Error `0x03`.
- `wifiBootConnect` no pisa un scan/connect en curso (`usbWifiBusy`).
- `Serial.setTxTimeoutMs(100)` en `setup()`: sin host USB, `write()` no bloquea para siempre.

## MED

- Línea ASCII máxima 192. Un `HUESET url https://…` de self-host con path largo se trunca **antes** de validar y el firmware responde `HUEERR` o guarda una URL cortada. Prod usa `https://hue.tineira.com` (cabe).
- `HUESET token` no llama `consoleLooksLikeToken` en el write path. El token se guarda; el poll no arranca hasta que el shape es válido.
- Cerrar Serial Monitor en C6 no resetea igual que el S3, pero DTR desde Web Serial sí resetea. El `.ino` hace `usbPump(1000)` al boot para no perder Scan/ping.

## GOOD

- Un parser, no dos stacks USB.
- Improv y ASCII no se pisan: un byte `I` entra a Improv; el resto ASCII printable.
- Timeout de 500 ms aborta un frame Improv a medias.
- `HUECLR` incrementa `gNvsEpoch` **y** llama `recipesWipe` (namespace entero, incluye `ls_*`).
- `HUEBOOT` hace `Serial.flush()` + 100 ms para que `HUEOK boot` salga antes del reset. NVS no se toca: el token sobrevive al flash del wizard.

## LOW

- `wifiBootConnect` loguea el SSID si `SERIAL_DEBUG`. No la password.
- No hay `HUEOK` de confirmación con echo del token (correcto: no hay que devolverlo).
