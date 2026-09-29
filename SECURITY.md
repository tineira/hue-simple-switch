# Security policy

## Reporting a vulnerability

Report privately through GitHub: [open a security advisory](https://github.com/tineira/hue-simple-switch/security/advisories/new). Please do not open a public issue or pull request for a vulnerability.

Include what you found, how to reproduce it, and what an attacker could do with it. You will get a first reply within a week. Once a fix ships, the advisory is published and you are credited unless you ask not to be.

This is a community project with one maintainer and no bug bounty.

## Supported versions

Only the current firmware release, the one the console's `/setup` page installs. Fixes ship as a new release; boards pick it up by over-the-air update or by reinstalling from `/setup`.

A problem in the console, the device API or the USB installer page belongs in [`hue-switch-console`](https://github.com/tineira/hue-switch-console/security/advisories/new). One in the other switch belongs in [`hue-round-switch`](https://github.com/tineira/hue-round-switch/security/advisories/new). If you are not sure, report it here.

## In scope

Firmware for the Seeed XIAO ESP32-C6:

- **Device token** (`hsw_…`): how it is stored on the board and sent to the console.
- **TLS to the console:** the board must verify the console's certificate. It skips verification only for the Hue Bridge on the LAN.
- **Over-the-air updates** (`ota.h`): an image must match the size and sha256 the console offered before the board boots it.
- **USB setup** (`usb.h`): the `HUESET` commands and Improv provisioning that write Wi-Fi, console and Hue settings over serial.
- **The Hue app key** stored on the board.

## Out of scope

- The Philips Hue Bridge and its local API. Report those to Signify.
- Attacks that need the owner's Wi-Fi password or control of their LAN.
- Reconfiguring or reflashing a board through its USB port. Anyone who can plug into it can set it up, by design. A USB command that crashes the board or leaks a stored secret is in scope.
- Denial of service by flooding the board over Wi-Fi.
