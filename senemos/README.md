# Senemos Nabu Linux 7.2 port

The `7.2.0` branch rebases Xiaomi Pad 5 (`nabu`) support onto the verified
Linux 7.2 release tarball. The upstream archive SHA-256 is:

`f9fef3d14c0df53819026f4be74459835c2a0b0dcbf5b5bbd9ea19f0829402b3`

The comparison baseline is the consolidated `6.17.0` source branch at commit
`d5731dcf7d5e418483d62f65b03266206b133f88`, whose Linux 6.17 archive
SHA-256 is:

`9b607166a1c999d8326098121222feb080a20a3253975fcdfa2de96ba7f757a7`

## Port scope

The branch carries the reviewed Nabu display/touch synchronization, stable
60/120 Hz modes, opt-in 90 Hz experiment, Night Light-safe color updates,
double-tap wake, PM8150B charger and fuel gauge, legacy ADSP/APR audio,
FastRPC mount matrices, tablet-mode reporting, late optional XHCI policy and
IDT P9418 stylus charging support.

The final 6.17 delta is also carried: pogo-keyboard power/detect and dynamic
tablet-mode reporting, Xiaomi HID pointer filtering, WCN3990 single-channel
scan handling, post-enable DSI/touch wake ordering, and the P9418 event and
stylus-MAC userspace ABI used by automatic BlueZ pairing.

The optional high-power charge-pump and board-thermistor scaffold stays in the
source tree but is excluded from the production DTB. It must not be enabled
until its Linux drivers, limits and thermal behavior have been validated on
physical Nabu hardware.

## Build contract

Run:

```sh
./senemos/build.sh
```

The script recreates the output configuration from the Fedora Rawhide arm64
base plus `nabu-minimal.config`, and builds `Image`, the Nabu DTB and all
modules. The resulting ABI is:

`7.2.0-nabu-senemos-v7.2.0`

The default output directory is:

`/workspace/kernel-builds/linux-nabu-senemos-v7.2.0-linux-7.2`

Source review, successful compilation, DT schema validation and RPM/COPR
publication are separate gates. None of them proves boot, display, touch,
charging, audio, suspend or Wi-Fi behavior on a physical tablet; those remain
part of the later BOOT/HIL phase.
