# Simple switch — LED naranja (status)

Documento de **requisitos**. Cubre solo `hue-simple-switch` (XIAO ESP32-C6, GPIO15 `LED_BUILTIN`, naranja). Round usa el disco, no este alfabeto.

El LED rojo de carga y los botones BOOT/RST no forman parte de este spec.

**Estado:** implementado (firmware 0.2.7). Spec archivado. No es un hueco de implementación.

**Cerrado el 2026-09-21 (grilling):** tick durante el POST, key rechazada vs PUT, #6 solo 401/task, ráfaga desde cero. No reabrir.

---

## 1. Para quién

Checklist de **alta en el banco** (se ve el XIAO). En la caja de la pared el LED casi no se mira; no es UI de uso diario.

No enseña *cómo* configurar. Solo *en qué peldaño estás* o *hay un fallo de sistema*.

---

## 2. Peldaños (prioridad)

Se evalúa **de arriba abajo**. El primero que cumpla gana. Un solo patrón a la vez.

| # | Condición | Patrón |
| --- | --- | --- |
| 6 | Error de **sistema** (abajo) | Encendido fijo |
| 1 | Sin Wi-Fi STA (`WL_CONNECTED` falso) | Rápido continuo ~2 Hz |
| 2 | Wi-Fi ok, sin URL o sin token de consola en NVS `console` | **2** destellos, pausa |
| 3 | Wi-Fi + consola, sin Bridge pareado (no hay key Hue usable en NVS, o pairing en curso / timeout) | **3** destellos, pausa |
| 4 | Wi-Fi + consola + Hue, **cero** recetas en NVS | **4** destellos, pausa |
| 5 | Armado: Wi-Fi + consola + Hue + ≥1 receta | Un destello corto cada ~3 s |

Hold 3 s en BOOT (re-pair) entra en **#3** mientras dura el pairing, no un patrón extra.

Al **cambiar de peldaño**, el patrón empieza de cero (primer destello, o el primer semiciclo de #1/#5). No se muestra el resto de una ráfaga anterior.

Scan Improv o asociación (`WL_CONNECTED` falso, aunque la NVS tenga SSID) es **#1**. Un token o URL que `consoleConfigured()` rechaza (vacío o mal formado) es “sin consola” → **#2** si hay Wi-Fi.

---

## 3. Timing (contable, no Hertz distintos)

Constantes (ms), una sola tabla en código:

| Símbolo | ms | Uso |
| --- | --- | --- |
| `PULSE_ON` | 100 | Destello de ráfaga (#2–4) |
| `PULSE_GAP` | 200 | Apagado entre destellos de la misma ráfaga |
| `BURST_PAUSE` | 1400 | Apagado después del último destello, antes de repetir |
| `FAST_ON` / `FAST_OFF` | 250 / 250 | #1 (~2 Hz). No se cuenta. |
| `HEART_ON` | 80 | #5 |
| `HEART_OFF` | 2920 | #5 → periodo 3 s |

Ráfaga de *n* destellos: `n × PULSE_ON + (n − 1) × PULSE_GAP`, luego `BURST_PAUSE`.

Ejemplo #3: on 100 — off 200 — on 100 — off 200 — on 100 — off 1400 — repetir. Se cuentan **tres**.

#6: pin en `LOW` (encendido), sin tick de parpadeo.

No usar 5 Hz vs 10 Hz vs 1 Hz. Un humano no los distingue en 3 mm.

---

## 4. Qué es error de sistema (#6)

Lista cerrada. Nada más enciende el fijo:

- La task de consola no se creó.
- La consola respondió **401** (token rechazado). Las recetas en NVS se conservan.

El 401 queda **pegado** hasta una respuesta real de la consola con código **> 0 y distinto de 401**, o hasta un `HUESET token` nuevo. Un intento que no llega (timeout, `code == -1`, Wi-Fi caído) **no** despega el fijo. Mientras siga pegado, el fijo gana aunque no haya Wi-Fi (no se ve #1).

**No** son #6:

- Timeout de pairing o Bridge no encontrado → **#3**.
- Wi-Fi caído, sin 401 pegado → **#1**.
- PUT Hue fallido en un toque GPIO (on/off/double/short), o Bridge inalcanzable → **el LED no cambia**.
- Poll de consola, `rev`, snapshot, debounce, un `LOG` cualquiera.

---

## 5. Hue “pareado”

Para #3 vs #4/#5: hay key en NVS que `hueLooksLikeKey` acepta **y** hay IP de Bridge. **No** hacer GET Clip en cada tick del LED.

Eso se pierde, y se pasa a **#3**, solo si un HTTP Hue **que envió la key** dice que no sirve: **401 o 403**, y el cuerpo no es “link button not pressed”. Un 401/403 de un pedido sin key (`/api/config`, discovery) no cuenta. Un PUT que sí llevó la key y vuelve 401/403 sí baja a #3. Un timeout, un 5xx o el Bridge apagado **no** bajan el peldaño. No #6.

Los **20 s** después de que el POST de pairing acaba de entregar la key, un 401 o 403 **no** baja a #3. La key acaba de salir del Bridge. Una llamada posterior, ya fuera de esos 20 s, sí baja.

---

## 6. Recetas vacías

#4 = `gRecipeCount == 0` (ningún canal). Una receta en un solo canal ya es armado (#5). No distinguir “faltan los otros tres”.

---

## 7. Arranque

Tras `pinMode(LED_BUILTIN, OUTPUT)` corre el mismo clasificador. Los primeros cientos de ms pueden verse como #1 (aún no hay STA). No hay pantalla `UI_BOOT` aparte.

USB Improv / `HUESET` no añaden patrón: sin STA → #1; STA sin token/url (aún en NVS) → #2.

---

## 8. Hecho en 0.2.7

- Tick de LED por timer, **sin `delay()`**, cada **~50 ms**, también durante el POST de pairing. El GPIO no espera al LED (`channelsPoll` primero).
- El patrón #3 lo pone el clasificador, no `hueBlink`.
- Polaridad: `LOW` = encendido (GPIO15, activo en bajo). `HIGH` apaga.
- No se toca el LED rojo de carga.
- `FIRMWARE_VERSION` 0.2.7. La tabla del README coincide.

Fuera de este recorte: Round (disco), Improv copy, consola.

---

## 9. Prueba de banco (aceptación)

Con el XIAO a la vista, sin abrir Serial:

1. Sin SSID / Wi-Fi down → continuo ~2 Hz.
2. Wi-Fi, borrar o vaciar URL/token → se cuentan **2**.
3. Wi-Fi+consola, hold 3 s BOOT o sin key → se cuentan **3**; pulsar el Bridge pasa a 4 o 5.
4. Pareado, consola sin recetas → se cuentan **4**.
5. Una receta en cualquier canal → un destello cada ~3 s.
6. Token consola inválido (401) → fijo. Restaurar token → vuelve a 4 o 5.
7. Durante 4 o 5, un GPIO cuyo PUT falle → el patrón **no** pasa a fijo.
