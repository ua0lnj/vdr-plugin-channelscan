#
# Makefile for a Video Disk Recorder plugin
#
# $Id$

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.
#

# if REELVDR is defined, some hardware and software dependencies will be
# aktivated for REELBOX lite


### The version number of this plugin (taken from the main source file):
VERSION = $(shell grep 'static const char \*VERSION *=' channelscan.h | awk '{ print $$6 }' | sed -e 's/[";]//g')

### The C++ compiler and options:

#CXX      ?= ccache g++
#CXXFLAGS = -fPIC -O2 -Wall -Woverloaded-virtual -fno-strict-aliasing

### The directory environment:

PKGCFG = $(if $(VDRDIR),$(shell pkg-config --variable=$(1) $(VDRDIR)/vdr.pc),$(shell pkg-config --variable=$(1) vdr || pkg-config --variable=$(1) ../../../vdr.pc))
LIBDIR = $(DESTDIR)$(call PKGCFG,libdir)
LOCDIR = $(DESTDIR)$(call PKGCFG,locdir)
#

TMPDIR ?= /tmp

### Allow user defined options to overwrite defaults:
export CFLAGS   = $(call PKGCFG,cflags)
export CXXFLAGS = $(call PKGCFG,cxxflags)
LDFLAGS = -lbz2 -lz

-include $(VDRDIR)/Make.config

PLUGIN = channelscan

CFLAGS += -fPIC
CXXFLAGS += -fPIC
### The version number of VDR (taken from VDR's "config.h"):
APIVERSION = $(call PKGCFG,apiversion)
#APIVERSION = $(shell grep 'define APIVERSION ' $(VDRDIR)/config.h | awk '{ print $$3 }' | sed -e 's/"//g')

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### The name of the shared object file:

SOFILE = libvdr-$(PLUGIN).so

### Includes and Defines (add further entries here):

INCLUDES +=

#DEFINES += -D_GNU_SOURCE -DPLUGIN_NAME_I18N='"$(PLUGIN)"' -DVDRDIR=\"$(VDRDIR)\" -DDEBUG_CHANNELSCAN
DEFINES += -DPLUGIN_NAME_I18N='"$(PLUGIN)"' -DVDRDIR=\"$(VDRDIR)\"  -DNDEBUG

ifdef REELVDR
  DEFINES += -DREELVDR
endif
# rotor
ifeq ($(shell test -f $(VDRDIR)/PLUGINS/src/rotor/rotor.h; echo $$?),0)
  DEFINES += -DHAVE_ROTOR
endif
# mcli
ifeq ($(shell test -f $(VDRDIR)/PLUGINS/src/mcli/mcli_service.h; echo $$?),0)
ifeq ($(shell pkg-config --exists libnetceiver && echo 1),1)
  NETCV_INC:=$(shell pkg-config --cflags libnetceiver)
  DEFINES += -DUSE_MCLI
  INCLUDES += -I. $(NETCV_INC)
endif
endif

### Debug
#DEFINES +=-DDEBUG_CSMENU
#DEFINES +=-DDEBUG_SCAN
#DEFINES +=-DDEBUG_TRANSPONDER
#DEFINES +=-DDEBUG_SDT
#DEFINES +=-DDEBUG_PAT_PMT
#DEFINES +=-DDEBUG_NIT

### causes segfaults Premiere Direkt sometimes
#DEFINES += -DWITH_EIT

#if dvbchan patch for duplicate sid nid tid applied
#else may be problems with iptv channels which have nonuniques pids
#see http://linuxdvb.org.ru/wbb/index.php?page=Thread&threadID=59
ifdef DVBCHANPATCH
DEFINES += -DDVBCHANPATCH
endif

### The object files (add further files here):

ifdef REELVDR
OBJS = channelscan.o csmenu.o dirfiles.o filter.o scan.o transponders.o channellistbackupmenu.o rotortools.o
else
OBJS = channelscan.o csmenu.o filter.o scan.o transponders.o channellistbackupmenu.o rotortools.o
endif


### Targets:
all: $(SOFILE) i18n

plug: $(SOFILE)

### Implicit rules:

%.o: %.c
	@echo CC $@
	$(Q)$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<

# Dependencies:

MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.c) > $@

-include $(DEPFILE)

### Internationalization (I18N):

PODIR     = po

I18Npo    = $(wildcard $(PODIR)/*.po)
I18Nmo    = $(addsuffix .mo, $(foreach file, $(I18Npo), $(basename $(file))))
I18Nmsgs  = $(addprefix $(LOCDIR)/, $(addsuffix /LC_MESSAGES/vdr-$(PLUGIN).mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot   = $(PODIR)/$(PLUGIN).pot

%.mo: %.po
	@echo MO $@
	$(Q)msgfmt -c -o $@ $<

$(I18Npot): $(wildcard *.c)
	@echo GT $@
	$(Q)xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP --msgid-bugs-address='<tobias.bratfisch@reel-multimedia.com>' -o $@ `ls $^`

%.po:
	@echo PO $@
	$(Q)msgmerge -U --no-wrap --no-location --backup=none -q -N $@ $<
	@touch $@

$(I18Nmsgs): $(LOCDIR)/%/LC_MESSAGES/vdr-$(PLUGIN).mo: $(PODIR)/%.mo
	install -D -m644 $< $@

### Targets:

.PHONY: i18n
i18n: $(I18Nmo) $(I18Npot)

install-i18n: $(I18Nmsgs)

.PHONY: i18n-dist
i18n-dist: $(I18Nmsgs)

all: libvdr-$(PLUGIN).so i18n

$(SOFILE): $(OBJS)
	@echo LD $@
	$(Q)$(CXX) $(CXXFLAGS) -shared $(OBJS) $(LDFLAGS) $(LIBS) -o $@

install-lib: $(SOFILE)
	install -D $^ $(LIBDIR)/$^.$(APIVERSION)

install: install-lib install-i18n

dist: clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz

clean:
	@-rm -f $(PODIR)/*.mo $(PODIR)/*.pot
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tgz core* *~

target-for-compatibility-with-vanilla-vdr:
	$(LIBDIR)/$@.$(APIVERSION)