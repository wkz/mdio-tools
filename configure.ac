AC_PREREQ(2.61)
AC_INIT(mdio-tools, m4_esyscmd_s(git describe --always --dirty),
	https://github.com/wkz/mdio-tools/issues)

AM_INIT_AUTOMAKE(1.11 foreign subdir-objects)
AM_SILENT_RULES(yes)

AC_CONFIG_HEADER(config.h)
AC_CONFIG_FILES([
	Makefile
	src/Makefile
	src/mdio/Makefile
])
AC_CONFIG_MACRO_DIRS(m4)

AC_PROG_CC
AC_PROG_INSTALL

PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([mnl],  [libmnl >= 0.2.0])

AC_OUTPUT
