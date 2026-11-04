# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Engine data PluginApp for BMW NBT Evo (QNX Neutrino 6.5, ARMv7 little endian, GNU libstdc++ variant to match HmiMain3D).
#
#   make             build libGaugeApp.so and the text/image DBs      (needs qcc, see below)
#   make db          only the DBs (python3 + Pillow)
#   make check       host tests with the Linux g++ (no QNX needed)
#   make clean
#
# RHMI_LIBDIR must hold copies of the car's libAppFramework.A.so and libCoreFramework.A.so (opt/hmi/ID5/lib in a
# firmware dump). They are BMW's files and are not part of this project; copy them from your own head unit.

QCC ?= qcc
RHMI_LIBDIR ?= ./lib
RHMI_LIBS = $(wildcard $(RHMI_LIBDIR)/libAppFramework.A.so $(RHMI_LIBDIR)/libCoreFramework.A.so)

PLUGIN_CXX      = $(QCC) -Vgcc_ntoarmv7le_gpp
PLUGIN_CXXFLAGS = -O2 -Wall -D_QNX_SOURCE -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp -fPIC -I./src

.PHONY: all lib db check clean
all: lib db

lib: libGaugeApp.so

libGaugeApp.so: src/engine_data.cpp src/cds_values.hpp
	@test -n "$(RHMI_LIBS)" || { echo "put libAppFramework.A.so and libCoreFramework.A.so from the car into $(RHMI_LIBDIR) (or set RHMI_LIBDIR)"; exit 1; }
	@echo [BUILD] $@
	$(PLUGIN_CXX) $(PLUGIN_CXXFLAGS) -shared -Wl,-soname,libGaugeApp.so -o $@ src/engine_data.cpp $(RHMI_LIBS) -Wl,--allow-shlib-undefined -lang-c++
	@echo [DONE] $@ - install with GaugeApp.manifest, GaugeApp.xml and the two zips into Plugins/GaugeApp

db:
	python3 make_db.py

check:
	$(MAKE) -C tests check

clean:
	rm -f libGaugeApp.so GaugeApp_ALL_Texts.zip GaugeApp_ALL_Images.zip
	$(MAKE) -C tests clean
