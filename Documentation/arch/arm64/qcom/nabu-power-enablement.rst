Xiaomi Pad 5 high-power charging enablement
============================================

Current state
-------------

The Xiaomi downstream kernel describes a PM8150B charger, one of two possible
external charge-pump variants, Xiaomi QC/PD policy drivers, and several board
thermistors.  The corresponding Nabu Linux Device Tree keeps the optional
components in ``sm8150-xiaomi-nabu-power-scaffold.dtsi``.  All nodes in that
file are disabled and must remain disabled in production images until the
requirements below have been met.

The base PM8150B charger and fuel gauge are independent of this scaffold.  The
scaffold does not change their current, voltage, Type-C, or power-supply
configuration.

Downstream reference
--------------------

The downstream board file is
``arch/arm64/boot/dts/qcom/xiaomi/overlay/nabu/nabu-sm8150.dtsi``.  It contains:

* a ``ti,bq2597x-standalone`` device at I2C address 0x66;
* a ``lionsemi,ln8000`` device at I2C address 0x51;
* ``xiaomi,cp-qc30`` and ``xiaomi,usbpd-pm`` policy nodes;
* PM8150, PM8150B, and PM8150L ADC channels for the charger, connector,
  wireless-power area, and other board temperatures.

The two I2C descriptions represent alternate hardware populations.  They are
not evidence that both devices are fitted to one tablet.

Driver gaps
-----------

The generic mainline ``bq25980`` driver does not bind the downstream
``ti,bq2597x-standalone`` compatible.  This kernel also has no LN8000, Xiaomi
QC/PD policy, or SMB1390 charger driver matching the downstream implementation.
Enabling a Device Tree node before these gaps are resolved would not provide a
safe or complete charging path.

Activation gates
----------------

Before enabling any charge-pump or board-thermal node:

#. Identify the fitted charge-pump by read-only I2C probing and a documented
   chip-identification register; never probe both variants by writing control
   registers.
#. Port or upstream a driver with reviewed register limits and error handling.
#. Validate every thermistor at ambient temperature, with bounded warm and cool
   tests, and verify plausible polarity after suspend and resume.
#. Confirm standard USB charging and Type-C role handling before testing higher
   power modes.
#. Use an external USB-C power meter and a current-limited source for the first
   activation tests.
#. Add conservative thermal mitigation and over-voltage/over-current limits
   before enabling QC, PD, or PPS policy.
#. Re-test cable removal, charger removal, suspend, resume, and reboot.

The downstream maximum current and voltage values are deliberately not copied
into an active Linux policy.  They are implementation inputs, not safe defaults
without the matching downstream driver, battery policy, and thermal feedback.

GNSS
----

No onboard GNSS path is described by the Nabu downstream board Device Tree.
The generic Linux GNSS subsystem only provides a framework; enabling it does
not create a receiver.  External USB or Bluetooth GNSS receivers can be
supported separately without adding a fictitious onboard Device Tree node.
