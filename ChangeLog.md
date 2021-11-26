ChangeLog
=========

All notable changes to the project are documented in this file.

[v1.0.1] - 2021-11-26
---------------------

Primarily fixes a few issues in the kernel module that were found
during a quick review from Russell King:

https://lore.kernel.org/netdev/YYPThd7aX+TBWslz@shell.armlinux.org.uk/
https://lore.kernel.org/netdev/YYPU1gOvUPa00JWg@shell.armlinux.org.uk/

### Added
- mdio: The mvls subcommand now supports flushing the ATU

### Fixed
- mdio-netlink: Plug some glaring holes around integer overflows of
  the PC.
- mdio-netlink: Release reference to MDIO bus after a transaction
  completes.


[v1.0.0] - 2021-09-17
---------------------

### Added
- Basic usage text, `mvls -h`
- Manuals

### Changes
- Reworked command syntax to be more ergonomic
- Improved error output

### Fixes
- Fix #4: buffer alignment on Arm9 systems

### Removed
- References to the dump operation in mdio(8) which is not supported
  at the moment

v1.0.0-beta1 - 2021-05-21
-------------------------

Initial public release.


[UNRELEASED]: https://github.com/wkz/mdio-tools/compare/1.0.0-beta1...HEAD
[v1.0.0]:     https://github.com/wkz/mdio-tools/compare/1.0.0-beta1...1.0.0
