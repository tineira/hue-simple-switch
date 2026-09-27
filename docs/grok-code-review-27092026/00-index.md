# Code review — hue-simple-switch

**Fecha:** 2026-09-27  
**Repo:** `tineira/hue-simple-switch` @ `main`  
**Firmware:** `0.4.1` (`console.h` `FIRMWARE_VERSION`)  
**Hardware:** Seeed XIAO ESP32-C6, Arduino-ESP32 3.3.12, flash 4 MB `min_spiffs`  
**Contrato:** `hue-switch-console` `docs/device-api.md` (SSOT; este repo no lo copia)  
**Regla de esta entrega:** solo documentos. No se cambió firmware ni secretos.

Relacionado: review de consola en `hue-switch-console/docs/grok-code-review-27092026/`.

## Cómo leer

Cada archivo es un concepto. Severidades:

| Tag | Significado |
| --- | --- |
| CRIT | Explotable o pérdida de control del dispositivo / flota |
| HIGH | Debe corregirse pronto; impacto real en uso o seguridad |
| MED | Deuda o riesgo con condiciones |
| LOW | Calidad, DX, hygiene |
| GOOD | Vale la pena no romperlo |

## Archivos

1. [01-metodo.md](01-metodo.md) — método y límites
2. [02-seguridad.md](02-seguridad.md) — tokens, TLS, NVS, superficie USB
3. [03-usb-improv.md](03-usb-improv.md) — Improv, HUESET/GET/PAIR/CLR/BOOT
4. [04-console-poll.md](04-console-poll.md) — register, GET config, task
5. [05-gpio-hue.md](05-gpio-hue.md) — canales, dim, loop bloqueante, Clip v2
6. [06-nvs-persistencia.md](06-nvs-persistencia.md) — recipes, rev, last scenes
7. [07-rendimiento.md](07-rendimiento.md) — hot path GPIO, snapshot, heap
8. [08-contrato.md](08-contrato.md) — vs device-api.md y problems de consola
9. [09-funcionalidades-faltantes.md](09-funcionalidades-faltantes.md) — huecos de producto
10. [10-testing-ops.md](10-testing-ops.md) — CI, changelog, observabilidad
11. [11-lo-que-esta-bien.md](11-lo-que-esta-bien.md) — no romper

## Prioridad sugerida

1. Sacar HTTP Hue del `loop()` GPIO (`channelFire` / toggle / dim) — HIGH UX. Round ya lo resolvió con `hue_job`.
2. Tests de parse de config (`recipesParseConfig`) y de “snapshot abort si stream ≠ 200” — HIGH deuda.
3. No aceptar `http://` en `HUESET url` en builds de producto — MED.
4. Limpiar `ls_*` al cambiar de Bridge (`recipesClear` deja last-scene keys) — MED.
5. Documentar que P4 está mitigado **en este firmware** (no POSTea si Clip falla) pero sigue vivo en la consola si otro cliente manda `[]`.
