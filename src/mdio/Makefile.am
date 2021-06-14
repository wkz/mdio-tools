sbin_PROGRAMS = mdio

mdio_SOURCES = bus.c main.c mdio.c mdio.h mva.c mvls.c phy.c print_phy.c xrs.c
mdio_CFLAGS  = -Wall -Wextra -Werror -Wno-unused-parameter -I $(top_srcdir)/include $(mnl_CFLAGS)
mdio_LDADD   = $(mnl_LIBS)
