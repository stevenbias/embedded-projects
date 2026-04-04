# ============================================================
# Embedded Mastery Tutorial — Build System
# ============================================================
# Usage:
#   make html        Build single-page HTML
#   make pdf         Build PDF via WeasyPrint
#   make epub        Build EPUB e-book
#   make all         Build all formats
#   make clean       Remove generated files
# ============================================================

# Directories
SRC_DIR      := src
ASSETS_DIR   := assets
TEMPLATE_DIR := templates
OUTPUT_DIR   := output

# Pandoc settings
PANDOC       := pandoc
PANDOC_FLAGS := --standalone --toc --toc-depth=3 \
                --highlight-style=tango \
                --metadata-file=metadata.yml

# Source files (explicitly ordered)
SRCS := $(SRC_DIR)/00-index.md \
        $(SRC_DIR)/00a-prerequisites.md \
        $(SRC_DIR)/00b-emulator-setup.md \
        $(SRC_DIR)/01-led-blinker.md \
        $(SRC_DIR)/02-uart-echo.md \
        $(SRC_DIR)/03-button-interrupts.md \
        $(SRC_DIR)/04-i2c-sensor.md \
        $(SRC_DIR)/05-spi-flash.md \
        $(SRC_DIR)/06-pwm-motor.md \
        $(SRC_DIR)/07-cooperative-scheduler.md \
        $(SRC_DIR)/08-ring-buffer.md \
        $(SRC_DIR)/09-bootloader.md \
        $(SRC_DIR)/10-rtos-kernel.md \
        $(SRC_DIR)/11-can-bus.md \
        $(SRC_DIR)/12-data-logger.md \
        $(SRC_DIR)/13-usb-cdc.md \
        $(SRC_DIR)/14-pid-motor.md \
        $(SRC_DIR)/15-safety-critical.md

# Combined source for single-document outputs
COMBINED := $(OUTPUT_DIR)/combined.md

# Output files
HTML_OUT := $(OUTPUT_DIR)/tutorial.html
PDF_OUT  := $(OUTPUT_DIR)/tutorial.pdf
EPUB_OUT := $(OUTPUT_DIR)/tutorial.epub

# Templates
HTML_TEMPLATE := $(TEMPLATE_DIR)/html.html

.PHONY: all html pdf epub clean combined help

# Default: build all
all: html pdf epub

# Combine all source files into one
combined: $(COMBINED)

$(COMBINED): $(SRCS) | $(OUTPUT_DIR)
	@echo "==> Combining source files..."
	@cat $^ > $@
	@echo "    -> $@"

# ---- HTML ----
html: $(HTML_OUT)

$(HTML_OUT): $(COMBINED) | $(OUTPUT_DIR)
	@echo "==> Building HTML..."
	@cp -r $(ASSETS_DIR) $(OUTPUT_DIR)/
	$(PANDOC) $(PANDOC_FLAGS) \
		--to html5 \
		--template=$(HTML_TEMPLATE) \
		--css=assets/css/style.css \
		--css=assets/css/dark.css \
		--metadata title="Embedded Mastery: C, Ada, Rust & Zig" \
		-o $@ $<
	@echo "    -> $@"

# ---- PDF ----
pdf: $(PDF_OUT)

$(PDF_OUT): $(HTML_OUT) | $(OUTPUT_DIR)
	@echo "==> Building PDF (via WeasyPrint)..."
	weasyprint file://$(CURDIR)/$< $@
	@echo "    -> $@"

# ---- EPUB ----
epub: $(EPUB_OUT)

$(EPUB_OUT): $(SRCS) | $(OUTPUT_DIR)
	@echo "==> Building EPUB..."
	$(PANDOC) $(PANDOC_FLAGS) \
		--to epub3 \
		--epub-cover-image=$(ASSETS_DIR)/cover.png \
		--metadata title="Embedded Mastery: C, Ada, Rust & Zig" \
		--metadata author="Embedded Mastery Project" \
		--metadata lang=en \
		--css=$(ASSETS_DIR)/css/style.css \
		-o $@ $^
	@echo "    -> $@"

# ---- Clean ----
clean:
	@echo "==> Cleaning output directory..."
	rm -rf $(OUTPUT_DIR)
	@echo "    Done."

# Create output directory
$(OUTPUT_DIR):
	@mkdir -p $@

# ---- Help ----
help:
	@echo "Embedded Mastery Tutorial — Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make html     Build HTML (single-page with dark/light theme)"
	@echo "  make pdf      Build PDF (via WeasyPrint)"
	@echo "  make epub     Build EPUB e-book"
	@echo "  make all      Build all formats (default)"
	@echo "  make clean    Remove generated files"
	@echo "  make help     Show this help"
	@echo ""
	@echo "Output directory: $(OUTPUT_DIR)/"
