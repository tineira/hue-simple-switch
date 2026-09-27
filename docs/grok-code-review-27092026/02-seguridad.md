# Seguridad — simple

Superficie: USB CDC (Improv + líneas ASCII), NVS (`console`, `hue`, `recipes`), HTTPS a la consola, HTTPS inseguro al Bridge LAN, discovery.meethue.com.

No hay servidor HTTP en la placa. No hay SoftAP. No hay OTA de producto.

## GOOD

- `config.h` (gitignored) solo tiene `SERIAL_DEBUG`. Ni SSID, ni token, ni Hue key se compilan. `config.example.h` / `config.product.h` lo confirman.
- Token validado al **usar**: `hsw_` + charset base64url + 20–80 chars (`consoleLooksLikeToken`). Un token basura no registra ni hace poll.
- Consola HTTPS: `NetworkClientSecure.useBuiltinCACertBundle()`. `setInsecure()` **no** se llama contra `CONSOLE_URL`.
- `discovery.meethue.com` también verifica el CA (`hueHttp(..., insecure=false)`).
- Bridge: `setInsecure()` solo contra IP LAN. Clip v2 exige HTTPS y el cert del Bridge es self-signed. Documentado en AGENTS.
- `HUEGET` responde `HUESTA` con `token=0|1` y `key=0|1`. No imprime el bearer ni la Hue application key.
- 401 de consola: sticky en RAM (`gConsoleAuthRejected`), poll al máximo (1 h), recetas NVS se quedan. Contrato device-api.
- 204 no lee body (`http.getString` se salta). Evita colgar el socket.
- ASCII USB acotado (`kUsbAsciiMax = 192`). Improv con checksum de 8 bits y bounds ssid ≤ 32 / pass ≤ 64.
- `HUECLR` borra STA + console + hue + recipes y sube `gNvsEpoch` para tirar un apply in-flight.

## MED

- **`HUESET url` acepta `http://`.** `consoleHttp` entonces usa `NetworkClient` plano y el bearer viaja en claro. El wizard de prod escribe `https://hue.tineira.com`. El protocolo lo permite para localhost. En un binario de producto, rechazar `http://` salvo loopback reduciría el riesgo de un USB físico malicioso o un copy-paste.
- Token, URL, Hue key e IP viven en NVS en claro. Esperado: quien tiene el USB es dueño del dispositivo. No hay TEE en C6.
- `consoleSetToken` **guarda lo que reciba** si NVS acepta. El charset se chequea en `consoleConfigured()`, no en el write. Un token malformado se persiste y la placa queda “no configured” hasta el siguiente HUESET bueno.
- `SERIAL_DEBUG=1` mezcla logs con paquetes Improv en el mismo CDC. Release CI fuerza 0 (`config.product.h`). Un flash de dev con debug=1 rompe el wizard.
- Logs de register / config 4xx pueden incluir el body de error de la consola (`LOGLN(body)`). No es el token, pero sí `details` de servidor.
- `hueLooksLikeKey` solo exige largo ≥ 20 y que no contenga `"your-"`. No hay checksum. Suficiente para no tomar el placeholder.

## LOW

- No hay rate limit local de HUESET/HUECLR. El atacante necesita USB físico.
- `HUEBOOT` pone el C6 en ROM download y reinicia. NVS intacta. Intencional para flashear sin el botón BOOT; cualquiera con USB puede reflashar.
- Fallback `HUE_BRIDGE_IP` / `HUE_APP_KEY` desde `config.h` si NVS está vacío. En producto ambos quedan `""`.
- Wi-Fi STA password queda en NVS del driver Arduino. `HUEGET` no la expone; `HUECLR` la borra.

## No es vulnerabilidad

- Clip `setInsecure` en LAN. El Bridge no ofrece un CA público. Mitigar con pinning del cert del Bridge sería trabajo extra y se rompe al reemplazar el Bridge.
- MAC en register y en `HUESTA`. Es el identificador del contrato.
