# Lo que no hay que romper — simple

Lista corta. Si un cambio futuro toca una de estas piezas, el costo es alto.

1. **Task consola vs GPIO separado.** El poll de Vercel no puede volver al `loop()`.
2. **Snapshot abort si Clip ≠ 200.** No POSTear `[]` “porque falló el stream”.
3. **Token shape check** (`hsw_` + charset + largo) antes de hablar con la consola.
4. **TLS de consola verificado.** Nunca `setInsecure()` contra `hue.tineira.com`.
5. **`HUEGET` sin secretos.** `token=0|1`, `key=0|1`.
6. **BOOT siempre usable para re-pair** cuando no hay recipe hold.
7. **NVS recipes como blob (`jsonb`)** — las listas de escenas no caben en un string 4000.
8. **`gNvsEpoch` contra apply tardío post-HUECLR.**
9. **204 no lee body.**
10. **`X-Poll-Sec` clamp 30–3600** y fallback 60 s / 1 h.
11. **401 no borra NVS.** Las luces de la casa siguen andando.
12. **Confirm poll** después de escribir un rev nuevo.
13. **product `"simple"` explícito** en register. No inferir.
14. **Scan Improv asíncrono** (ACK inmediato). El wizard de `/setup` depende de eso.
15. **`HUEBOOT` no borra NVS.** El wizard flashea y el token sigue.
16. **Dim no apaga.** Off → on al mínimo y sube. Stop en release.
17. **404 de scene salta** al siguiente de la lista.
18. **CI: changelog + THIRD_PARTY + SERIAL_DEBUG 0** como gate del upload.

El bloqueo GPIO (05) es la excepción: sí hay que tocarlo, con el patrón de Round, sin devolver HTTP al loop.
