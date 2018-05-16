#
# Makefile
# Peter Jones, 2018-05-16 13:48
#
default : all

NAME = antikernel
TOPDIR	:= $(shell echo $$PWD)
include $(TOPDIR)/Makefile.version
include $(TOPDIR)/Makefile.rules
include $(TOPDIR)/Makefile.defaults
include $(TOPDIR)/Makefile.scan-build
include $(TOPDIR)/Makefile.coverity

TARGETS	= guest.so launch
all: $(TARGETS)

LDLIBS	=
PKGS	=

% : | %.c
	$(CCLD) $(CCLDFLAGS) $(CPPFLAGS) -o $@ $^ $(LDLIBS)

%.so :
	$(CCLD) $(CCLDFLAGS) $(CPPFLAGS) $(SOFLAGS) \
          -Wl,-soname,$@.$(VERSION) \
          -o $@ $^ $(LDLIBS)

guest.so : guest.c
guest.so : | util.h
launch : launch.c relocate.c
launch : | launch.h relocate.h util.h

clean :
	rm -vf $(TARGETS) *.E *.o *.a *.so

# vim:ft=make
#
