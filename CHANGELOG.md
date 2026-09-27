# Changelog

User-facing release notes for the Simple switch (XIAO ESP32-C6 with wired buttons). One `### <version> — <YYYY-MM-DD>` heading per `FIRMWARE_VERSION`, newest first, written in the same commit that bumps `FIRMWARE_VERSION`, in the wording of the person using the switch. CI sends the entry for the current version to the console as the release notes.

Start a bullet with `Important: ` when the person must know or do something before or right after updating (a button to press, a setting to redo). The console shows those first, with an Important label, when Setup offers the update. Do not use it for new features.

### 0.4.4 — 2026-09-27

- The switch no longer risks restarting when you pair it with the Hue Bridge again, or clear its settings, while it is checking in with the console.

### 0.4.3 — 2026-09-27

- While one switch is waiting on a slow Hue Bridge, the other switches and buttons still respond right away.
- Quick flicks of a switch are no longer missed while the Bridge is busy with another one: the lights end up matching where you left each switch.
- A button press that could not reach the Bridge within about 10 seconds is dropped, instead of changing the lights long after you pressed it.

### 0.4.2 — 2026-09-27

- If the switch loses power or runs out of storage while saving new settings from the console, it now gets those settings again on its next check-in. Before, it could keep its old settings while the console showed it as up to date.

### 0.4.1 — 2026-09-26

- The console shows whether the switch has your latest settings.
- Changes you save in the console reach the switch in about 30 seconds while you are working on it, and in about 5 minutes otherwise, instead of up to an hour.

### 0.4.0 — 2026-09-25

- A push button can dim its lights while you hold it, if you choose Dim for its hold in the console. Let go to stop at the level you want.
- Each hold dims the other way from the last one. A light at full brightness always dims down, and one near its lowest always brightens.
- Holding to dim a light that is off turns it on at its lowest level and brightens it. Dimming down never turns a light off.

### 0.3.0 — 2026-09-25

- Each input can be a toggle switch or a push button, whichever you choose for it in the console.
- Important: An input you have not set up in the console does nothing. After updating, set up each wired input in Switches.
- A push button responds as soon as you let go, unless you gave it a double-click action.
- Double-clicking a toggle switch steps through your chosen scenes, one per double-click, and starts again from the first after you switch the lights off.
- Holding BOOT can run a Hue action instead of re-pairing with the Bridge, if you choose that in the console.

### 0.2.11 — 2026-09-24

- Important: After this update, installing new firmware from Setup no longer needs the BOOT button. Press RESET once when the write finishes.

### 0.2.10 — 2026-09-22

- Pairing with the Bridge no longer fails if the first check right after pairing does not go through.

### 0.2.9 — 2026-09-22

- The switch ignores a console token that is not a real API key.

### 0.2.8 — 2026-09-22

- Devices can read the switch's status, start pairing, and clear its saved settings over USB.

### 0.2.7 — 2026-09-22

- The orange LED shows the setup step as a count of blinks. See How-to.

### 0.2.6 — 2026-09-21

- The Wi-Fi network list during setup tries again if the first scan fails.

### 0.2.5 — 2026-09-21

- USB setup stays connected while you pick a Wi-Fi network.

### 0.2.4 — 2026-09-21

- USB setup still works right after the switch restarts.

### 0.2.3 — 2026-09-20

- The Wi-Fi network list during setup is more reliable while the switch is starting.

### 0.2.2 — 2026-09-20

- The switch answers a Wi-Fi scan request during setup straight away.

### 0.2.1 — 2026-09-20

- The Wi-Fi network list during setup is no longer empty when the scan is slow.

### 0.2.0 — 2026-09-20

- You can set up the switch from the console over USB: Wi-Fi and the console link are saved on the switch.

### 0.1.1 — 2026-09-20

- Buttons stay responsive while the switch checks in with the console. If a check-in fails, the switch keeps its last good settings.

### before 0.1.1 — 2026-09-19

- First Simple switch firmware. The BOOT button toggles a light, and the switch finds and pairs with the Hue Bridge.
