sbin_PROGRAMS = mvls

mvls_SOURCES = mvls.c mvls.h devlink.c devlink.h mvls-show.c
mvls_CFLAGS  = -Wall -Wextra -Werror -Wno-unused-parameter -I $(top_srcdir)/include $(mnl_CFLAGS)
mvls_LDADD   = $(mnl_LIBS)
