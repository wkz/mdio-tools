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
