# Changelog

User-facing release notes for the Simple switch (XIAO ESP32-C6 with wired buttons). One `### <version> — <YYYY-MM-DD>` heading per `FIRMWARE_VERSION`, newest first, written in the same commit that bumps `FIRMWARE_VERSION`, in the wording of the person using the switch. CI sends the entry for the current version to the console as the release notes.

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
