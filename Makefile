BUILD_DIR ?= build
GENERATOR ?=

.PHONY: all browser cli clean run

all: browser cli

browser cli:
	@cmake -S . -B $(BUILD_DIR) $(GENERATOR)
	@cmake --build $(BUILD_DIR) --target $@

run: browser cli
	@$(BUILD_DIR)/rethread browser --url=https://veilm.github.io/rethread/

clean:
	@rm -rf $(BUILD_DIR)
