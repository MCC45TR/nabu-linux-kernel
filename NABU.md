# Xiaomi Pad 5 (nabu) kernel branch

This branch carries the Linux kernel changes used by the Nabu Linux project.
It is based on the SM8150 mainline kernel at commit
`c340f8ec4fa41eb94b82f233ddcce15fd0aaec17`.

## Hardware status

| Area | Status | Notes |
| --- | --- | --- |
| Display | Validated | 60 Hz and 120 Hz modes are available. |
| Touchscreen | Validated | Touch scan mode follows 60/120 Hz display changes. |
| Rotation sensor | Validated | Automatic rotation works in the Plasma session. |
| Ambient light sensor | Experimental | Sensor data is available; userspace calibration and dimming behavior still need work. |
| Internal speakers | Experimental | Four-channel routing is under development. Do not assume balanced or distortion-free output. |

The panel firmware command sequence currently supports only 60 Hz and 120 Hz.
A 90 Hz mode is intentionally not exposed without a verified panel and touch
controller sequence.

## Build

Use an AArch64 cross compiler or build natively on AArch64. For example:

```sh
make ARCH=arm64 defconfig
make ARCH=arm64 -j"$(nproc)" Image.gz dtbs modules
```

The image builder and userspace hardware profiles are maintained separately:

- <https://github.com/MCC45TR/nabu-linux-builder>
- <https://github.com/MCC45TR/nabu-linux-hardware-support>

## Contributing

Keep hardware changes separated by subsystem and include the exact device state
used for validation. Reports should distinguish compilation, boot, and physical
hardware testing. Avoid committing firmware, signing keys, credentials, build
outputs, or generated disk images.

## License

The kernel is licensed under GPL-2.0-only with the Linux syscall exception; see
`COPYING` for the complete licensing information.
