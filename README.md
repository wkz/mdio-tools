mdio-tools
==========
[![License Badge][]][License] [![GitHub Status][]][GitHub]


Table of Contents
-----------------
* [Introduction](#introduction)
* [Usage](#usage)
* [Build](#build)


Introduction
------------

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


Usage
-----

```
    mdio            -- List available buses
    mdio BUS        -- Probe BUS for active devices
    mdio BUS OBJ    -- Show status of OBJ
    mdio BUS OBJ OP -- Perform OP on OBJ

Options:
  -h   This help text
  -v   Show verision and contact information

Bus names may be abbreviated using glob(3) syntax, i.e. "fixed*"
would typically match against "fixed-0".

Objects:
  phy PHYAD
    Clause 22 (MDIO) PHY using address PHYAD.

    REG: u5

  mmd PRTAD[:DEVAD]
    Clause 45 (XMDIO) PHY using address PRTAD:DEVAD.

    REG: u16

  mva PHYAD
    Operate on Marvell Alaska (mv88e8xxx) PHY using address PHYAD.
    Register 22 is assumed to be the page register.

    REG: u8|"copper"|"fiber":u5

  mvls ID
    Operate on Marvell LinkStreet (mv88e6xxx) device attached to BUS
    using address ID. If ID is 0, single-chip addressing is used; all
    other IDs use multi-chip addressing.

    REG: u5|"global1"|"global2" u5

  xrs PHYAD
    Operate of Arrow/Flexibilis XRS700x device using address PHYAD.

    REG: u32 (Stride of 2, only even registers are valid)

Operations:
  raw REG [DATA[/MASK]]
    Raw register access. Without DATA, REG is read. An unmasked DATA will
    do a single write to REG. A masked DATA will perform a read/mask/write
    sequence.

    DATA: u16
    MASK: u16
```

Build
-----

Standard autotools procedure, requires `pkg-config` to locate the `libmnl`
development files.

    ./configure --prefix=/usr && make all && sudo make install

At the moment, the kernel module has to be built separately, set
`KDIR` if building against a kernel in a non-standard location.

When building from GIT, the `configure` script first needs to be generated, this
requires `autoconf` and `automake` to be installed.  A helper script to generate
configure is available:

    ./autogen.sh

[License]:       https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
[License Badge]: https://img.shields.io/badge/License-GPL%20v2-blue.svg
[GitHub]:        https://github.com/wkz/mdio-tools/actions/workflows/build.yml/
[GitHub Status]: https://github.com/wkz/mdio-tools/actions/workflows/build.yml/badge.svg
