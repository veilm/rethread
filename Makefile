# -------- settings --------
CEF_VERSION ?= 139.0.28+g55ab8a8+chromium-139.0.7258.139
CEF_ROOT    ?= $(PWD)/third_party/cef/cef_binary_$(CEF_VERSION)_linux64
OUT         ?= $(PWD)/out/Release
CXX         ?= g++

CXXFLAGS += -std=c++17 -O2 -fPIC -pthread -D_FILE_OFFSET_BITS=64
CXXFLAGS += -I$(CEF_ROOT)/include
# Keep it lean; add -Wall -Wextra if you like.

LDFLAGS  += -Wl,-rpath,'$$ORIGIN' -L$(CEF_ROOT)/Release -lcef -lpthread -ldl

# -------- wrapper (static lib) --------
WRAP_DIR   := $(CEF_ROOT)/libcef_dll
# Compile *all* .cc in libcef_dll except non-Linux platform files:
WRAP_SRCS  := $(shell find $(WRAP_DIR) -name '*.cc' ! -name '*_win*.cc' ! -name '*_mac*.cc')
WRAP_OBJS  := $(patsubst $(WRAP_DIR)/%.cc,$(OUT)/libcef_dll/%.o,$(WRAP_SRCS))
WRAP_LIB   := $(OUT)/libcef_dll/libcef_dll_wrapper.a

# -------- app --------
APP_SRCS := src/main_linux.cc src/simple_app.cc src/simple_handler.cc
APP_OBJS := $(patsubst src/%.cc,$(OUT)/app/%.o,$(APP_SRCS))
APP_BIN  := $(OUT)/mybrowser

# -------- rules --------
all: $(APP_BIN) copy-resources

$(OUT)/libcef_dll/%.o: $(WRAP_DIR)/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(WRAP_LIB): $(WRAP_OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(OUT)/app/%.o: src/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(APP_BIN): $(WRAP_LIB) $(APP_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^ $(LDFLAGS)

copy-resources:
	# Put runtime files next to the binary
	cp -f $(CEF_ROOT)/Release/libcef.so $(OUT)/
	cp -f $(CEF_ROOT)/icudtl.dat $(OUT)/
	cp -f $(CEF_ROOT)/chrome_100_percent.pak $(OUT)/ || true
	cp -f $(CEF_ROOT)/chrome_200_percent.pak $(OUT)/ || true
	cp -f $(CEF_ROOT)/resources.pak $(OUT)/
	mkdir -p $(OUT)/locales && cp -rf $(CEF_ROOT)/locales/* $(OUT)/locales/

run: all
	# Dev only: --no-sandbox avoids SUID setup for chrome-sandbox
	# cd $(OUT) && ./mybrowser --no-sandbox --url=https://google.com
	# 1764380483 idk what that means but a sandbox is fine from my testing
	cd $(OUT) && ./mybrowser --url=https://google.com

clean:
	rm -rf $(OUT)
