# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Host tests (Linux g++). From the project root: `make check`.
#   test_cds_values   parsing and display rows (src/cds_values.hpp)
#   app_host_test     host build of src/engine_data.cpp, loaded with dlopen against fake_appframework.cpp
OUT := build
CXX ?= g++
WARN := -Wall -Wextra

.PHONY: check clean
check: $(OUT)/test_cds_values $(OUT)/app_host_test $(OUT)/libEngineData.so
	$(OUT)/test_cds_values
	$(OUT)/app_host_test $(OUT)/libEngineData.so

$(OUT):
	mkdir -p $(OUT)

# The app is C++98 (QNX SDP 6.5 / GCC 4.4); keep the host build in the same dialect.
$(OUT)/test_cds_values: test_cds_values.cpp ../src/cds_values.hpp | $(OUT)
	$(CXX) -std=gnu++98 $(WARN) -I../src -o $@ test_cds_values.cpp

$(OUT)/libEngineData.so: ../src/engine_data.cpp ../src/cds_values.hpp | $(OUT)
	$(CXX) -std=gnu++98 $(WARN) -fPIC -shared -I../src -o $@ ../src/engine_data.cpp -ldl -lpthread

# -rdynamic: the library looks up bmw::Runnable's vtable with dlsym(RTLD_DEFAULT) and needs the fake's symbols.
$(OUT)/app_host_test: app_host_test.cpp fake_appframework.cpp fake_appframework.hpp | $(OUT)
	$(CXX) -std=gnu++11 $(WARN) -rdynamic -o $@ app_host_test.cpp fake_appframework.cpp -ldl

clean:
	rm -rf $(OUT)
