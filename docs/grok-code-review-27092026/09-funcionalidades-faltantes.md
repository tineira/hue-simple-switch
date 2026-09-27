# Funcionalidades faltantes — simple

No es un backlog de producto. Son huecos que el código o el contrato ya insinúan.

## HIGH / producto visible

- **Cola Hue.** Sin ella el interruptor “se queda tonto” 8–16 s. Round ya lo tiene. Es la feature que más se siente en la pared.
- **OTA.** `partitions.csv` reserva dos APP (~1.9 MB × 2) y la consola tiene `docs/specs/ota.md` abierto. Este firmware no baja bins ni verifica firma. El usuario reflasha por USB.

## MED

- **Re-pair por BOOT cuando hay hold.** Contrato: si hay recipe hold, BOOT no re-parea. No hay gesto de escape en la placa (solo USB). Un usuario que configuró dim en BOOT y perdió el PC no puede re-parear sin desmontar.
- **Limpieza de `ls_*` al cambiar Bridge.** Ver 06.
- **Tope de owners / recipes visible.** Una casa de 80 luces pierde mapping room→light sin feedback. La consola no avisa “snapshot truncado”.
- **Rechazar `http://` en producto.** Ver 02.
- **Estado LED vs 401 consola.** El LED cuenta setup (led.h). Un token rechazado con recipes en NVS sigue “armado”; correcto, pero no hay patrón que diga “token malo” (Round pone un punto rojo).

## LOW / DX

- No hay comando USB para dump de recipes (a propósito: no filtrar topología por serial).
- No hay métrica de heap/stack en `HUEGET`.
- `FIRMWARE_VERSION` vive en `console.h`, no en el `.ino`. Round lo tiene en el `.ino`. CI de Simple parsea `console.h`; no romper esa línea.
- Changelog de usuario no menciona el bloqueo GPIO. No hace falta en user notes; sí en esta carpeta.

## Fuera de alcance (y está bien)

- Pantalla / pages / swipe (eso es Round).
- SoftAP / captive portal. El producto es USB + Improv.
- Zigbee / matter. AGENTS lo prohíbe.
- Cloud Hue. Solo Clip v2 local.
