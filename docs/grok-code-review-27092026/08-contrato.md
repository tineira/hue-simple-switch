# Contrato vs consola — simple

SSOT: `hue-switch-console` `docs/device-api.md`. Este repo no copia esos docs (`AGENTS.md`).

## Alineado

| Tema | Firmware |
| --- | --- |
| product | siempre `"simple"` |
| channels register | `id`, `gpio`, `label` (no `kind`) |
| Bearer | `hsw_…` validado al usar |
| GET config + `rev` | sí |
| 204 keep NVS | sí |
| `X-Poll-Sec` | clamp 30–3600 |
| snapshot vacío por fallo Clip | no se envía |
| `bridgeid` change | drop recipes/rev **antes** del poll |
| USB | HUESET / HUEGET / HUEPAIR / HUECLR / HUEBOOT + Improv |
| TLS consola | CA bundle |
| TLS Bridge | `setInsecure` solo LAN |
| FIRMWARE_VERSION | `0.4.1` en `console.h` |
| CI | `firmware.yml` → `POST /api/firmware/simple` |
| 401 | NVS recipes se quedan, poll 3600 s |
| confirm poll | sí, tras NVS write OK |

## Drift / riesgo

- Consola deriva recipes de `simple_channels` (`short`/`on`/`off`/`double_click` + hold). Firmware acepta `hold`/`dim` en el wire. Si un día alguien POSTea `hold` crudo al device config, el firmware lo corre. Hoy la consola es quien genera el body.
- `kMaxRecipes=16` / `kMaxSceneTargets=8` / `kMaxChannelSettings=8`. Device-api habla de 1–8 scenes. No documenta el tope de recipes en firmware. Extras se silencian.
- `http://` en URL no está en el wizard de prod; el firmware lo permite (dev localhost).
- No hay tests de contrato en este repo (CI = compile + upload).
- Simple no envía `kind` en register (correcto desde channel-types). Firmware &lt; 0.3.0 sí lo mandaba; la consola lo ignora.

## problems.md (consola) vs este código

| ID | Aquí |
| --- | --- |
| P3 rev=0 bind | Firmware aún pone rev=0 local al cambiar bridge; consola bump-ea. Primer poll 200 debería rellenar. |
| P4 empty snapshot | Mitigado (no POST si stream falla). Consola sigue abierta a otros clientes. |
| P8 poll en loop | Mitigado (task dedicada). |
| P11 apply sin bump | Simple no aplica nada si `localRev >= remote`. |
| P12 Wi-Fi no retry | Mitigado (retry 10 s). |
| P21 pretty JSON | Cerrado (`jsonGetString` salta whitespace). |

## Lo que este firmware no debe inventar

Cualquier campo nuevo en register/config, NVS key que el wizard escriba, o comando USB extra, va primero a un spec en la consola. `AGENTS.md` lo dice. No extraer una lib compartida con Round: el C6 no tiene PSRAM y es un core.
