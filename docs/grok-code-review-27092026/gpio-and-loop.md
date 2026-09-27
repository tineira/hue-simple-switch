# GPIO y loop — Simple

**Fuente:** `channels.h`, `hue-simple-switch.ino`.

Pines: boot/GPIO9 momentary; d0/d1/d2 GPIO 0/1/2 maintained por defecto. Kind real viene de `channels[]` del poll; BOOT siempre momentary (re-pair 3 s).

Debounce 50 ms. Double-click 400 ms. Hold 800 ms. Dim: GET brillo + ramp Bridge 5 s; stop en release.

## Hallazgo principal

### HIGH — Hue HTTP en el loop de GPIO (P8 residual)

`channelFire` llama `hueExecute` / `hueRecallScene` / `hueGetOn` / `hueDimStart` con timeout **8 s** (`hue.h`). Toggle = GET + PUT (hasta ~16 s). El comentario en `channelsPoll` admite que el loop se bloquea en el PUT de dim start.

Impacto: durante esa espera no hay sample de pines. Un segundo toque en la ventana de 400 ms se pierde o se ve como otro evento. problems.md decision 8 pedia sacar snapshot/register del loop (hecho) y que el dedo no espere a Hue (no hecho para acciones).

Recomendacion: cola de acciones a un hueJob como en Round; el loop solo encola event+channel.

### MED — Maintained double_click fallback a on

Si no hay recipe double_click, dispara `on`. Alineado con definitions para la pared.

### LOW — boot siempre se configura

Aunque la consola no liste boot, el pin corre. 3 s hold re-pair si no hay recipe hold.

### LOW — Scene cycle local (`ls_<id>` en NVS)

404 salta a la siguiente. Off limpia el cursor. No consulta status.active.
