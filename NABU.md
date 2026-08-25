# Xiaomi Pad 5 (nabu) kernel branch

This branch carries the Linux kernel changes used by the Nabu Linux project.
It is based on the SM8150 mainline kernel at commit
`c340f8ec4fa41eb94b82f233ddcce15fd0aaec17`.

## Branches

- `6.17.0`: hardware-tested SENEMOS Nabu kernel used by the Fedora Rawhide
  v1.37 test release.
- The 7.2 port is maintained separately and must retain its real upstream
  version. Do not label a 7.2.0 tree as 7.2.9 without the corresponding
  upstream stable release.

## Hardware status

| Area | Status | Notes |
| --- | --- | --- |
| Display | Partially validated | Native 60 and 120 Hz modes and their transitions are physically validated. The experimental 90 Hz timing is hidden by default after a physical dual-DSI scanout failure. |
| Touchscreen | Partially validated | 60 and 120 Hz are validated. The retained 90 Hz timing and scan-band path remain disabled pending display and long-duration touch validation. |
| Rotation sensor | Validated | Automatic rotation works in the Plasma session. |
| Ambient light sensor | Validated | Automatic brightness is exposed and enabled in the Plasma session. |
| Internal speakers | Experimental | Four-channel routing is under development. Do not assume balanced or distortion-free output. |

The v1.4.0.6 candidate preserves the vblank-synchronised 60/120 Hz transition
path and keeps the experimental 90 Hz timing compiled but hidden by default.
It also keeps Nabu DSPP resources reserved so CRTC color-temperature changes
do not force a full dual-DSI modeset, and defers optional XHCI probing until
userspace so a failed pogo
keyboard enumeration cannot hold the kernel before `/init`. It also fixes an
early SLIMbus workqueue initialisation warning and
removes an unused CDSP secure-memory reservation that overlapped EFI memory.
Framebuffer console rotation and the Terminus 16x32 font are enabled for a
readable landscape boot console.

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
