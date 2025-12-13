# -------- settings --------
CEF_VERSION ?= 139.0.28+g55ab8a8+chromium-139.0.7258.139
CEF_ROOT    ?= $(PWD)/third_party/cef/cef_binary_$(CEF_VERSION)_linux64
OUT         ?= $(PWD)/out/Release
CXX         ?= g++

# Detect where libcef.so lives
CEF_BIN_DIR := $(shell \
  if [ -f "$(CEF_ROOT)/Release/libcef.so" ]; then echo "$(CEF_ROOT)/Release"; \
  elif [ -f "$(CEF_ROOT)/libcef.so" ]; then echo "$(CEF_ROOT)"; \
  elif [ -f "$(CEF_ROOT)/Resources/libcef.so" ]; then echo "$(CEF_ROOT)/Resources"; \
  else echo "$(CEF_ROOT)/Release"; fi)

# Detect where resources (icudtl.dat, *.pak) live
CEF_RES_DIR := $(shell \
  if [ -f "$(CEF_ROOT)/icudtl.dat" ]; then echo "$(CEF_ROOT)"; \
  elif [ -f "$(CEF_ROOT)/Resources/icudtl.dat" ]; then echo "$(CEF_ROOT)/Resources"; \
  else echo "$(CEF_ROOT)"; fi)

# Detect locales dir
CEF_LOCALES_DIR := $(shell \
  if [ -d "$(CEF_ROOT)/locales" ]; then echo "$(CEF_ROOT)/locales"; \
  elif [ -d "$(CEF_ROOT)/Resources/locales" ]; then echo "$(CEF_ROOT)/Resources/locales"; \
  else echo "$(CEF_ROOT)/locales"; fi)

BASE_CXXFLAGS := -std=c++17 -O2 -fPIC -pthread -D_FILE_OFFSET_BITS=64 -I$(CEF_ROOT) -I$(CEF_ROOT)/include
GLIB_CFLAGS   := $(shell pkg-config --cflags glib-2.0 2>/dev/null)
APP_CXXFLAGS  := $(BASE_CXXFLAGS) -DUSING_CEF_SHARED -Isrc $(GLIB_CFLAGS)
WRAP_CXXFLAGS := $(BASE_CXXFLAGS) -DWRAPPING_CEF_SHARED

LDFLAGS  += -Wl,-rpath,'$$ORIGIN' -L$(CEF_BIN_DIR) -lcef -lpthread -ldl
X11_LIBS := $(shell pkg-config --libs x11 xrandr xcursor xi xcomposite xdamage xrender xfixes xext 2>/dev/null)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0 2>/dev/null)
LDFLAGS  += $(X11_LIBS)
LDFLAGS  += $(GLIB_LIBS)

# -------- wrapper (static lib) --------
WRAP_DIR   := $(CEF_ROOT)/libcef_dll
WRAP_SRCS  := $(shell find $(WRAP_DIR) -name '*.cc' ! -name '*_win*.cc' ! -name '*_mac*.cc')
WRAP_OBJS  := $(patsubst $(WRAP_DIR)/%.cc,$(OUT)/libcef_dll/%.o,$(WRAP_SRCS))
WRAP_LIB   := $(OUT)/libcef_dll/libcef_dll_wrapper.a

# -------- app --------
APP_SRCS := \
	src/main_linux.cc \
	src/app/app.cc \
	src/app/tab_cli.cc \
	src/common/theme.cc \
	src/common/debug_log.cc \
	src/browser/tab_strip.cc \
	src/browser/tab_manager.cc \
	src/browser/tab_ipc_server.cc \
	src/browser/client.cc \
	src/browser/client_linux.cc \
	src/browser/windowing.cc
APP_OBJS := $(patsubst src/%.cc,$(OUT)/app/%.o,$(APP_SRCS))
APP_BIN  := $(OUT)/rethread

# -------- rules --------
all: $(APP_BIN) copy-resources

$(OUT)/libcef_dll/%.o: $(WRAP_DIR)/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(WRAP_CXXFLAGS) -c $< -o $@

$(WRAP_LIB): $(WRAP_OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(OUT)/app/%.o: src/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(APP_CXXFLAGS) -c $< -o $@

$(APP_BIN): stop-rethread $(WRAP_LIB) $(APP_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $(APP_OBJS) $(WRAP_LIB) $(LDFLAGS)

.PHONY: stop-rethread
# Kill any stale dev instance so the linker doesn't trigger a core dump while overwriting the binary
stop-rethread:
	@echo "Stopping running rethread instances (if any)..."
	- pkill -x rethread >/dev/null 2>&1 || true

copy-resources:
	# Put runtime files next to the binary
	cp -f $(CEF_BIN_DIR)/libcef.so $(OUT)/
	# GPU / EGL libs expected next to the binary
	- cp -f $(CEF_BIN_DIR)/libEGL.so $(OUT)/
	- cp -f $(CEF_BIN_DIR)/libGLESv2.so $(OUT)/
	- cp -f $(CEF_BIN_DIR)/libvk_swiftshader.so $(OUT)/
	- cp -f $(CEF_BIN_DIR)/libvulkan.so.1 $(OUT)/
	- cp -f $(CEF_BIN_DIR)/vk_swiftshader_icd.json $(OUT)/
	# Core resources
	cp -f $(CEF_RES_DIR)/icudtl.dat $(OUT)/
	cp -f $(CEF_RES_DIR)/resources.pak $(OUT)/
	# Optional (present in most bundles)
	- cp -f $(CEF_RES_DIR)/chrome_100_percent.pak $(OUT)/
	- cp -f $(CEF_RES_DIR)/chrome_200_percent.pak $(OUT)/
	# V8 snapshot (present when needed)
	- cp -f $(CEF_BIN_DIR)/v8_context_snapshot.bin $(OUT)/
	# Locales
	mkdir -p $(OUT)/locales
	cp -rf $(CEF_LOCALES_DIR)/* $(OUT)/locales/

run: all
	# Dev-only: --no-sandbox avoids SUID setup for chrome-sandbox
	cd $(OUT) && ./rethread --url=https://veilm.github.io/rethread/

# Optional: run without copying by pointing to in-place resources
run-dev: $(APP_BIN)
	cd $(OUT) && ./rethread --no-sandbox \
	  --resources-dir-path="$(CEF_RES_DIR)" \
	  --locales-dir-path="$(CEF_LOCALES_DIR)" \
	  --url=https://veilm.github.io/rethread/

clean:
	rm -rf $(OUT)
