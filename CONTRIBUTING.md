# Contributing

Thanks for helping. This is the firmware for the **Simple** switch (XIAO ESP32-C6, `product: "simple"`). It is one of three repos:

- [`hue-switch-console`](https://github.com/tineira/hue-switch-console) is the hub. It owns the contract every switch implements, and its [CONTRIBUTING.md](https://github.com/tineira/hue-switch-console/blob/main/CONTRIBUTING.md) has the project-wide rules.
- [`hue-round-switch`](https://github.com/tineira/hue-round-switch) is the other switch. It shares the contract with this one, not the code.

Everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.md). Report problems to [conduct@tineira.com](mailto:conduct@tineira.com).

## Ground rules

- **English everywhere:** code comments, docs, commit messages, issues.
- **The console owns the contract.** What this firmware sends to and expects from the console (endpoints, JSON fields, error handling, the NVS keys the console writes over USB) follows the console's `docs/device-api.md` and `docs/definitions.md`. If your change needs a protocol change, open a "Contract change" issue in the console repo first. Do not invent one here.
- **Never break the console contract for boards already on the wall.** The console ships a change first and stays backward compatible.
- **No secrets in the tree.** `config.h` is gitignored and holds only `SERIAL_DEBUG`. Wi-Fi, the console URL and the device token are written over USB by the console, never compiled in.

## Building

Install [arduino-cli](https://arduino.github.io/arduino-cli/). The `xiao-c6` profile in `sketch.yaml` pins the core and libraries, so you don't install them by hand.

```bash
cp config.example.h config.h
arduino-cli compile --profile xiao-c6 .
arduino-cli upload --profile xiao-c6 -p <port> .
```

Host tests for the JSON and config parsers run on your PC, no board needed (a C++17 compiler; `CXX` picks it, default `g++`):

```bash
test/host/run.sh
```

Provision the board once from the console's **Setup** page in Chrome or Edge. `arduino-cli upload` doesn't erase NVS, so Wi-Fi and the token survive later flashes. Set `SERIAL_DEBUG` to `1` in `config.h` for USB logs.

Hardware: Seeed XIAO ESP32-C6. Channels are GPIO contacts to GND (see the README table).

## Pull requests

**A push to `main` is a release.** CI builds the image and uploads it to the console, which then offers it to every new install. So pull requests are merged by the maintainer only, after a test on real hardware.

1. For anything bigger than a small fix, open or comment on an issue first.
2. Keep a PR to one change, and match the style of the surrounding code.
3. It must compile with `arduino-cli compile --profile xiao-c6 .` and pass `test/host/run.sh`. If you change a parser in `json_util.h`, `recipes.h` or `ota_offer.h`, add a case to `test/host/test_parsers.cpp`.
4. Leave `FIRMWARE_VERSION` (in `console.h`) and `CHANGELOG.md` alone: the maintainer bumps them when releasing. Describe the user-visible change in the PR instead.
5. If you add or upgrade the core or a library, update `sketch.yaml` and [`THIRD_PARTY.json`](THIRD_PARTY.json) in the same PR (CI runs `python3 scripts/check-credits.py` and fails otherwise). Every component must have an MIT-compatible license.
6. Say which board you tested on and what you checked.

## License

There is no CLA and no sign-off. By contributing, you agree that your contribution is licensed under this repo's license, [MIT](LICENSE).
