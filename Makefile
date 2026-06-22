.PHONY: build test clean package

BUILD_DIR ?= .build

build:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR)

test:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target steamvr_bail_out_tests
	ctest --test-dir $(BUILD_DIR) --output-on-failure

package:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target package

clean:
	cmake -E rm -rf $(BUILD_DIR)
