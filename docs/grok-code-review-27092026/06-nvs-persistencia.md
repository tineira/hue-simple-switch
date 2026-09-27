# NVS — simple

Tres namespaces: `console`, `hue`, `recipes`. El driver Wi-Fi guarda STA por su cuenta. Flashear el app partition **no** borra NVS (AGENTS).

## console

| Key | Contenido |
| --- | --- |
| `url` | Base URL. Producto: `https://hue.tineira.com` |
| `token` | Bearer `hsw_…` |

Load al boot (`consoleLoadNvs`). `HUECLR` hace `p.clear()`.

## hue

| Key | Contenido |
| --- | --- |
| `ip` | IPv4 del Bridge |
| `key` | Hue application key |
| `bid` | `bridgeid` |

Validadores: `hueLooksLikeIp` (3 puntos, solo dígitos, sin `x`), `hueLooksLikeKey` (≥20, no `your-`).

## recipes

| Key | Contenido |
| --- | --- |
| `rev` | uint, último rev aplicado |
| `bid` | bridgeid al que pertenecen las recipes |
| `jsonb` | blob del wire shape (`channels` + `recipes`) |
| `json` | legacy string &lt; 0.3.0; se migra en load y se borra en save |
| `ls_boot` / `ls_d0` / … | último scene rid del canal |

`putBytes` para `jsonb` porque una lista de escenas puede pasar el límite de 4000 de `putString`.

Caps en RAM: 16 recipes, 8 scenes por recipe, 8 channel settings. Hardware: 4 pines.

## Apply / wipe

- `recipesSave` escribe rev + bid + jsonb. Si `putBytes` no cubre el length, `ok = false` y no se arma `gConsoleConfirmPoll`.
- `recipesClear` (cambio de bridge): count=0, rev=0, save. **No** borra `ls_*`. El ciclo de escenas del canal anterior puede arrancar en un rid que ya no existe (404 skip lo mitiga).
- `recipesWipe` (`HUECLR`): `prefs.clear()` del namespace + epoch++. Sí borra `ls_*`.
- `recipesBindBridge` compara case-insensitive. Si cambia, clear + return true para forzar re-register.

## MED

- `ls_*` sobreviven a `recipesClear`. Tras cambiar de casa/Bridge el primer double-click puede intentar un scene id viejo (un 404) antes de caer al primero de la lista nueva.
- `recipesSave` no es transaccional. Un reset a mitad deja rev nuevo con jsonb viejo o al revés. El confirm poll y el `localRev >= remote` limitan el daño.
- `malloc(len+1)` al cargar jsonb. Si NVS está corrupto y `len` es enorme, puede fallar el alloc y quedar recipes vacías con rev persistido.

## GOOD

- Blob en vez de string 4000.
- Migración `json` → `jsonb`.
- `gRecipeStage` global para no meter ~10 KB en el stack de la task.
- Mutex + `gNvsEpoch` contra apply tardío post-HUECLR.
- GPIO solo lee RAM; nunca llama a Vercel.
