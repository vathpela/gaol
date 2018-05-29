#
# Makefile
# Peter Jones, 2018-05-16 13:48
#
default : all

NAME = gaol
TOPDIR	:= $(shell echo $$PWD)
include $(TOPDIR)/Makefile.version
include $(TOPDIR)/Makefile.rules
include $(TOPDIR)/Makefile.defaults
include $(TOPDIR)/Makefile.scan-build
include $(TOPDIR)/Makefile.coverity

TARGETS	= guest gaol
all: $(TARGETS)

LDLIBS	+= -ldl
PKGS	=

gaol.h : | compiler.h page.h list.h execvm.h

gaol : execvm.c
gaol : | gaol.h
gaol : PKGS+=libelf

clean :
	rm -vf $(TARGETS) *.E *.o *.a *.so

# vim:ft=make
#
