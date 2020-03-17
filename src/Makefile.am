sbin_PROGRAMS = mdio

mdio_SOURCES = main.c mdio.c mv6.c print_phy.c
mdio_CFLAGS  = -Wall -Wextra -Werror -Wno-unused-parameter -I $(top_srcdir)/include $(mnl_CFLAGS)
mdio_LDADD   = $(mnl_LIBS)
