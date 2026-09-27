# GPIO y Hue — simple

`channels.h` + `hue.h` + `hue_discover.h` + `snapshot.h`. Pins compilados: `boot` GPIO9 momentary; `d0/d1/d2` GPIO 0/1/2 maintained por default. `channels[]` de consola puede ignorar un pin (`CHK_NONE`). BOOT **siempre** momentary para re-pair 3 s, aunque la consola no lo liste.

## HIGH — loop bloqueante

`channelFire` corre **en `loop()`**: `hueGetOn`, `hueSetOn`, `hueRecallScene`, `hueDimStart`. Timeout Hue 8 s; toggle = GET+PUT ≤ 16 s. Comentario en `channelsPoll`:

> The GPIO loop blocks during the start PUT, so a release that came meanwhile is seen here right after it.

Durante ese rato:

- no hay debounce/double-click fiable (ventana 400 ms);
- un segundo canal no se lee;
- LED y Wi-Fi retry esperan;
- `usbPoll` solo corre si el caller de Hue invoca `gOnHueWait` (el `.ino` lo setea; sirve sobre todo al pair `hueWaitMs`, no a cada PUT de `hueHttp`).

Round ya movió Hue a `hue_job` (user slot last-wins + background refresh). Simple no.

**Fix sugerido:** cola de jobs como Round, o al menos dim/toggle fuera del loop. El C6 es un solo core: la task de Hue tendría que ser prio baja y el loop solo encolar + leer el pin.

## Comportamiento de canales

- Debounce 50 ms, double-click 400 ms, hold dim 800 ms, BOOT re-pair `kLongPressMs` = 3 s (`hue_discover.h`).
- Maintained: cerrado = `on`; abierto espera 400 ms; re-cierre en la ventana = `double_click` (fallback `on` si no hay recipe).
- Momentary: short en release; con `double_click` recipe espera segunda pulsación; hold a 800 ms si hay recipe `hold`.
- Si BOOT tiene hold recipe, **deja de re-parear** por botón (solo USB `HUEPAIR` / wizard). Contrato device-api.
- Escenas: ciclo + NVS `ls_<id>`; 404 salta a la siguiente; `off` resetea el ciclo aunque no haya recipe off.
- Dim: GET bri; off → enciende al mínimo (`kDimMinBrightness = 1`) y sube; ≥95 baja; ≤5 sube; GET fail invierte dirección. Nunca apaga. Stop en release (`dimming_delta.stop`).
- Wi-Fi down: `channelFire` retorna true sin llamar Hue (no pierde el evento a medias… salvo que el dim stop se salte y el ramp del Bridge siga 5 s).

`channelsResolveModes` relée kind/double/hold cuando `gRecipesGen` cambia. Un pin que pasa de momentary a maintained se re-prime sin disparar.

## Hue HTTP

- Clip v2 HTTPS + `setInsecure` + header `hue-application-key`.
- Snapshot stream 20 s timeout, sink incremental (`JsonDataSink`, `kMaxObj = 20480`). Salta `scene.actions` para no overflow.
- 401/403 con key (salvo “link button not pressed”) → `gHueAuthRejected`. Grace 20 s post-pair.
- Pair: state machine no bloqueante desde USB (`huePairPoll`); boot `hueEnsureReady()` sí puede bloquear hasta 90 s de pair (`huePairAppKey`) con `gOnHueWait` bombeando USB/LED.
- Discover: mDNS `_hue._tcp` → cache NVS → `HUE_BRIDGE_IP` → `discovery.meethue.com` (TLS verificado).
- `kMaxOwners = 64` luces para mapear device→light en rooms/zones. Casas grandes recortan owners: `light_ids` de un room puede quedar incompleto.

## Snapshot y P4

`hueBuildSnapshot` aborta si light, room, zone o scene stream ≠ 200. No construye `[]` a medias. Register no POSTea. Last good snapshot en consola se queda.

Un 200 vacío real (Bridge nuevo, cero luces) sí se envía. Eso es correcto según device-api (`lights` required array, may be empty).

## LOW

- Hold `dim` es evento extra vs check SQL histórico de `recipes` en consola (`on|off|double_click|short`). Hoy device-api ya lista `dim` en Simple ≥ 0.4.0. Firmware 0.3.x droppea la recipe dim y keep el resto.
- `hueExecute` acepta `recall_scene` con `rtype/rid` (path viejo). El path actual usa `hueRecallScene` + lista.
- Logs de PUT fallido incluyen body del Bridge (puede ser largo; no debería traer la key).
