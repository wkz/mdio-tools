mdio-tools
==========

`mdio` is a low-level Linux debug tool for communicating with devices
attached an MDIO bus. It improves on existing tools in this space in a
few important ways:

- MDIO buses are directly addressable. Previous solutions relied on at
  least one Ethernet PHY on the bus being attached to a net device,
  which is typically not the case when the device is an Ethernet
  switch for example.
- Complex operations can be performed atomically. The old API only
  supported a single read or write of a single register. `mdio` sends
  byte code to the `mdio-netlink` kernel module that can perform
  multiple operations, store intermediate values, loop etc. As a
  result, things like read/mask/write operations and accesses to paged
  PHYs can be performed safely.

```
Usage: mdio COMMAND OPTIONS

Device agnostic commands:

  help
    Show this usage message.

  list [BUS]
    List buses matching BUS, or all if not specified.

  dump BUS [PORT:]DEV [REG[-REG|+NUM]]
    Dump multiple registers. For Clause 22 devices, all registers are
    dumped by default. For Clause 45 the default range is [0-127].

  raw BUS [PORT:]DEV REG [VAL[/MASK]]
    Raw register access. Without VAL, REG is read. An unmasked VAL will
    do a single write to REG. A masked VAL will perform a read/mask/write
    sequence.

Device specific commands:

  mv6
    Commands related to Marvell's MV88E6xxx series of Ethernet switches.

Common options:
  BUS           The ID of an MDIO bus. Use 'mdio list' to see available
                buses. Uses glob(3) matching to locate bus, i.e. "fixed*"
                would typically match against "fixed-0".

  [PORT:]DEV    MDIO device address, either a single 5-bit integer for a
                Clause 22 address, or PORT:DEV for a Clause 45 ditto.

  REG           Register address, a single 5-bit integer for a Clause 22
                access, or 16 bits for Clause 45.

  VAL[/MASK]    Register value, 16 bits. Optionally masked using VAL/MASK
                which will read/mask/write the referenced register.
```


Build
-----

Standard autotools procedure, requires `pkg-config` to locate the `libmnl`
development files.

    ./configure && make all && sudo make install

At the moment, the kernel module has to be built separately, set
`KDIR` if building against a kernel in a non-standard location.

When building from GIT, the `configure` script first needs to be generated, this
requires `autoconf` and `automake` to be installed.  A helper script to generate
configure is available:

    ./autogen.sh
