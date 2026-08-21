# Senemos Nabu Linux 7.2 port

This branch rebases the Xiaomi Pad 5 (`nabu`) support on the verified Linux
7.2 release tarball. The imported tarball SHA-256 is:

`f9fef3d14c0df53819026f4be74459835c2a0b0dcbf5b5bbd9ea19f0829402b3`

The initial port is intentionally boot-minimal. It contains the board DT and
the fixed-refresh Nabu CSOT panel support from the traceable 6.17 publication
baseline. It does not contain the later seamless DFPS, custom SM8150 audio,
FastRPC mount-matrix, SMP2P sleep-state, or experimental power scaffold code.

The Fedora base configuration was extracted from the Rawhide aarch64
`kernel-core-7.3.0-0.rc0.260819gbd5f485f3f02.5.fc46` RPM and is normalized for
Linux 7.2 with `nabu-minimal.config`.

Expected first-stage limitations:

- audio is deliberately not enabled in the board DT;
- the downstream Novatek SPI touchscreen driver still needs a separate port;
- SLPI uses the upstream generic compatible, but Nabu-specific register and
  FastRPC/IOMMU behavior still requires device validation;
- battery, charger, RTC offset, seamless DFPS and suspend policy are deferred;
- a successful build is not physical Nabu HIL evidence.

Build from the Rawhide toolchain container with:

```sh
./senemos/build.sh
```

The default release is `7.2.0-senemos.nabu.v2.0`. Set
`SENEMOS_EXTRA_LOCALVERSION` only when an additional build suffix is required.
