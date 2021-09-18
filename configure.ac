AC_PREREQ(2.61)
AC_INIT(mdio-tools, m4_esyscmd_s(git describe --always --dirty --tags),
	https://github.com/wkz/mdio-tools/issues)
AC_CONFIG_AUX_DIR(aux)
AM_INIT_AUTOMAKE(1.11 foreign subdir-objects)
AM_SILENT_RULES(yes)

AC_CONFIG_HEADER(config.h)
AC_CONFIG_FILES([
	Makefile
	include/Makefile
	man/Makefile
	src/Makefile
	src/mdio/Makefile
	src/mvls/Makefile
])
AC_CONFIG_MACRO_DIRS(m4)

AC_PROG_CC
AC_PROG_INSTALL

PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([mnl],  [libmnl >= 0.2.0])

AC_OUTPUT
