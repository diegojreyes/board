# Zephyr™ Mechanical Keyboard (ZMK) Firmware

[![Discord](https://img.shields.io/discord/719497620560543766)](https://zmk.dev/community/discord/invite)
[![Build](https://github.com/zmkfirmware/zmk/workflows/Build/badge.svg)](https://github.com/zmkfirmware/zmk/actions)
[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-v2.0%20adopted-ff69b4.svg)](CODE_OF_CONDUCT.md)

[ZMK Firmware](https://zmk.dev/) is an open source ([MIT](LICENSE)) keyboard firmware built on the [Zephyr™ Project](https://www.zephyrproject.org/) Real Time Operating System (RTOS). ZMK's goal is to provide a modern, wireless, and powerful firmware free of licensing issues.

Check out the website to learn more: https://zmk.dev/.

You can also come join our [ZMK Discord Server](https://zmk.dev/community/discord/invite).

To review features, check out the [feature overview](https://zmk.dev/docs/). ZMK is under active development, and new features are listed with the [enhancement label](https://github.com/zmkfirmware/zmk/issues?q=is%3Aissue+is%3Aopen+label%3Aenhancement) in GitHub. Please feel free to add 👍 to the issue description of any requests to upvote the feature.

[Keychron](https://keychron.com/) uses ZMK source code for B Pro series keyboards and have made big changes for B Pro. Keychron also added proprietary 2.4 GHz communication.

To build the firmware, for example, Keychron B1 Pro:

Prepare:

```
    mkdir keychron
    cd keychron
    git clone -b keychron_bpro https://github.com/keychron/zmk.git
    cd zmk
    west init -l app/
    west update
```

Patch Zephyr:

```
    cd zephyr
    git am ../001-esb-nrf-fix.patch
```

Build firmware:

```
    cd app
    west build -b keychron -p -- -DSHIELD=keychron_b1_us
```
