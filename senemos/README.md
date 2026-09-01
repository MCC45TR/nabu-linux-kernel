# Senemos Nabu Linux 7.2.2 test port

The `7.2.2-test` branch rebases Xiaomi Pad 5 (`nabu`) support onto Linux
7.2.2.  It is an alpha test line built on the current camera/Iris integration,
not a replacement for the known-good 6.17 kernel.

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

The protected LN8000 driver and board thermal policy are included for staged
qualification.  The direct 2:1 path remains disabled because the normal Nabu
DTB deliberately omits `lionsemi,allow-direct-charging`; it must not be enabled
until instrumented physical validation proves every protection and limit.

The detailed Iris comparison is in `7.2.2-iris-audit.md`.  The sensor-first,
GNOME-first but distribution-neutral test sequence is in
`7.2.2-hil-plan.md`; the 6.17-to-7.2.2 source/HIL ledger is in
`6.17-to-7.2.2-port-status.md`.  `hil/nabu-hil-collect.sh` captures read-only
evidence at each physical gate.

## Build contract

Run:

```sh
./senemos/build.sh
```

The script recreates the output configuration from the Fedora Rawhide arm64
base plus `nabu-minimal.config`, and builds `Image`, the Nabu DTB and all
modules. The resulting ABI is:

`7.2.2-nabu-senemos-mainline-alpha`

The default output directory is:

`/workspace/kernel-builds/linux-nabu-senemos-7.2.2-test`

Source review, successful compilation, DT schema validation and RPM/COPR
publication are separate gates. None of them proves boot, display, touch,
charging, audio, suspend or Wi-Fi behavior on a physical tablet; those remain
part of the later BOOT/HIL phase.
