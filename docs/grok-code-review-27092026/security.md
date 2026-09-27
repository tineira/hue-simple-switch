# Seguridad — Simple

## Bien

- Token de consola solo NVS (`console` namespace), no compilado. `consoleLooksLikeToken` exige prefijo `hsw_` + charset base64url, longitud 20–80.
- HTTPS a la consola: `useBuiltinCACertBundle()` (`console.h` `consoleHttp`).
- Bridge Hue: `setInsecure()` documentado — certificado self-signed en LAN. No es cloud.
- 401 de consola: sticky en RAM, poll al máximo (3600 s), NVS recipes se conservan.
- HUESET nuevo limpia el flag 401.
- `SERIAL_DEBUG` 0 en el binario de instalador (`config.product.h`) — menos fuga de logs USB.

## Hallazgos

### MED — Token y URL en NVS en claro

Cualquiera con USB + HUEGET / dump NVS lee el Bearer. Esperado para un switch físico; el riesgo es un PC que flashea y deja el token.

### MED — Bridge `setInsecure`

MITM en la LAN puede leer/escribir Clip v2 con la application key. Aceptable en casa; no en red compartida hostil.

### LOW — Logs pueden incluir cuerpos HTTP

`LOGLN(body)` en register/config fallidos. Con `SERIAL_DEBUG 1` un monitor USB ve errores de Neon (`details`) si la consola los filtra.

### LOW — `gOnHueWait` llama `usbPoll` durante esperas Hue

Bien para no perder Improv; no cifra nada extra.

### NIT — No hay rate-limit local de POSTs a Vercel

El worker respeta `X-Poll-Sec`. Un bug que setee `gNeedConsoleSync` en bucle saturaría la consola.
