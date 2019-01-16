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

gaol.h : | compiler.h mmu.h list.h util.h execvm.h ioring.h

gaol : execvm.c mmu.c ioring.c
gaol : | gaol.h
gaol : PKGS+=libelf

ioring.c : | ioring.h

guest.c : | compiler.h ioring.h
guest : ioring.c
guest : CCLDFLAGS+=-Wl,--export-dynamic

clean :
	rm -vf $(TARGETS) *.E *.o *.a *.so core.* vgcore.*

# vim:ft=make
#
