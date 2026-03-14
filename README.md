mdio-tools
==========
[![License Badge][]][License] [![GitHub Status][]][GitHub]

The latest release is always available from GitHub at  
> https://github.com/wkz/mdio-tools/releases


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

OPTIONS
  -h   This help text
  -v   Show verision and contact information

Bus names may be abbreviated using glob(3) syntax, i.e. "fixed*"
would typically match against "fixed-0".

OBJECTS
  phy PHYAD
    Clause 22 (MDIO) PHY using address PHYAD.

    REG: u5

  mmd PRTAD[:DEVAD]
    Clause 45 (XMDIO) PHY using address PRTAD:DEVAD.

  mmd-c22 PRTAD[:DEVAD]
    Clause 45 (XMDIO) PHY addressed over Clause 22 using address
    PRTAD:DEVAD.

    REG: u16

  mva PHYAD
    Operate on Marvell Alaska (mv88e8xxx) PHY using address PHYAD.
    Register 22 is assumed to be the page register.

    REG: u8|"copper"|"fiber":u5

  mscc PHYAD
    Operate on Microsemi/Microchip (VSC85xx) PHY using address PHYAD.
    Register 31 is assumed to be the page register.

    REG: u8:u5

  mvls ID
    Operate on Marvell LinkStreet (mv88e6xxx) device attached to BUS
    using address ID. If ID is 0, single-chip addressing is used; all
    other IDs use multi-chip addressing.

    REG: u5|"global1"|"global2" u5

  xrs PHYAD
    Operate of Arrow/Flexibilis XRS700x device using address PHYAD.

    REG: u32 (Stride of 2, only even registers are valid)

OPERATIONS
  raw REG [DATA[/MASK]]
    Raw register access. Without DATA, REG is read. An unmasked DATA will
    do a single write to REG. DATA with MASK will run the atomic sequence
    write(REG, (read(REG) & MASK) | DATA)
    sequence.

    DATA: u16
    MASK: u16

  bench REG [DATA]
    Benchmark read performance. If DATA is supplied, it is written to REG,
    otherwise the current value in REG is read. REG is then read 1000
    times. Any unexpected values are reported, along with the total time.

    DATA: u16

EXAMPLES
  Show all available buses:
     ~# mdio
     30be0000.ethernet-1
     fixed-0

  List all Clause 22 addressable devices on a bus (using glob(3) pattern
  to abbreviate bus name):
    ~# mdio 3*
    DEV      PHY-ID  LINK
    0x01  0x01410dd0  up

  Read register 2 from PHY 1:
    ~# mdio 3* phy 1 raw 2
    0x0141

  Perform a reset on PHY 1:
    ~# mdio 3* phy 1 raw 0 0x8000/0x7fff

  Read register 0x1000 from MMD 4 on PHY 9:
    ~# mdio 3* mmd 9:4 raw 0x1000
    0x2040

  Read status register from the copper page of an Alaska PHY:
    ~# mdio 3* mva 1 raw copper:1
    0x796d

  Read register from page 1 of a Microsemi PHY:
    ~# mdio 3* mscc 1 raw 1:25
    0x1234

  Set the device number, of LinkStreet switch 4, to 10:
    ~# mdio 3* mvls 4 raw g1:28 0xa/0xfff0
```

Build
-----

At the moment, the kernel module (which requires at least kernel version 5.2)
has to be built separately. Set `KDIR` if building against a kernel in a
non-standard location.

    cd kernel/
	make all && sudo make install

When building from GIT, the `configure` script first needs to be generated, this
requires `autoconf` and `automake` to be installed.  A helper script to generate
configure is available:

    ./autogen.sh

Standard autotools incantation is then used, requires `pkg-config` to locate the
`libmnl` development files.

    ./configure --prefix=/usr && make all && sudo make install

[License]:       https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
[License Badge]: https://img.shields.io/badge/License-GPL%20v2-blue.svg
[GitHub]:        https://github.com/wkz/mdio-tools/actions/workflows/build.yml/
[GitHub Status]: https://github.com/wkz/mdio-tools/actions/workflows/build.yml/badge.svg
