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

LDLIBS	=
PKGS	=

% : | %.c
	$(CCLD) $(CCLDFLAGS) $(CPPFLAGS) -o $@ $^ $(LDLIBS)

%.so :
	$(CCLD) $(CCLDFLAGS) $(CPPFLAGS) $(SOFLAGS) \
          -Wl,-soname,$@.$(VERSION) \
          -o $@ $^ $(LDLIBS)

guest : guest.c
guest : | util.h
gaol : gaol.c relocate.c
gaol : | gaol.h relocate.h util.h

clean :
	rm -vf $(TARGETS) *.E *.o *.a *.so

# vim:ft=make
#
