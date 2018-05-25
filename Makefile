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

LDLIBS	?=
PKGS	=

% : %.c
	$(CCLD) $(CCLDFLAGS) $(CPPFLAGS) -o $@ $^ $(LDLIBS)

%.so :
	$(CCLD) $(CCLDFLAGS) $(CPPFLAGS) $(SOFLAGS) \
          -Wl,-soname,$@.$(VERSION) \
          -o $@ $^ $(LDLIBS)

gaol : | gaol.h relocate.h compiler.h page.h list.h
gaol : PKGS+=libelf
gaol : relocate.c

clean :
	rm -vf $(TARGETS) *.E *.o *.a *.so

# vim:ft=make
#
