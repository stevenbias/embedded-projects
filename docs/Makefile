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
SCRIPTS_DIR  := scripts

# Pandoc settings
PANDOC       := pandoc
PANDOC_FLAGS := --standalone --toc --toc-depth=3 \
                --highlight-style=tango \
                --metadata-file=metadata.yml

# Source files (explicitly ordered)
SRCS := $(SRC_DIR)/00-index.md \
        $(SRC_DIR)/00a-prerequisites.md \
        $(SRC_DIR)/00b-emulator-setup.md \
        $(SRC_DIR)/00c-gdb-survival-guide.md \
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
COMBINED_HTML := $(OUTPUT_DIR)/combined-html-ready.md

# Output files
HTML_OUT := $(OUTPUT_DIR)/tutorial.html
PDF_OUT  := $(OUTPUT_DIR)/tutorial.pdf
EPUB_OUT := $(OUTPUT_DIR)/tutorial.epub

# Templates
HTML_TEMPLATE := $(TEMPLATE_DIR)/html.html

.PHONY: all html pdf epub pages clean combined help search-index search-data serve deploy

# Default: build all
all: html pdf epub pages

# Combine all source files into one
combined: $(COMBINED)

$(COMBINED): $(SRCS) | $(OUTPUT_DIR)
	@echo "==> Combining source files..."
	@cat $^ > $@
	@echo "    -> $@"

$(COMBINED_HTML): $(COMBINED)
	@sed 's/\.md)/\.html)/g; s/\.md"/\.html"/g' $< > $@

# ---- HTML ----
html: $(HTML_OUT)

$(HTML_OUT): $(COMBINED_HTML) $(OUTPUT_DIR)/assets/search-index.json | $(OUTPUT_DIR)
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

# ---- Individual Pages ----
pages: search-data $(OUTPUT_DIR)/.pages-built

$(OUTPUT_DIR)/.pages-built: $(SRCS) $(OUTPUT_DIR)/assets/css/style.css $(OUTPUT_DIR)/assets/js/search-data.js
	@echo "==> Building individual HTML pages..."
	@for i in $$(seq 0 $$(($(words $(SRCS)) - 1))); do \
		src=$$(echo $(SRCS) | cut -d' ' -f$$((i+1))); \
		name=$$(basename $$src .md); \
		prev_idx=$$i; \
		next_idx=$$((i + 2)); \
		if [ $$i -gt 0 ]; then \
			prev=$$(basename "$$(echo $(SRCS) | cut -d' ' -f$$prev_idx)" .md); \
		else \
			prev=""; \
		fi; \
		if [ $$next_idx -le $(words $(SRCS)) ]; then \
			next=$$(basename "$$(echo $(SRCS) | cut -d' ' -f$$next_idx)" .md); \
		else \
			next=""; \
		fi; \
		sed 's/\.md)/\.html)/g; s/\.md"/\.html"/g' $$src > $(OUTPUT_DIR)/$$name.tmp.md; \
		$(PANDOC) $(PANDOC_FLAGS) \
			--to html5 \
			--template=templates/page.html \
			--css=assets/css/style.css \
			--css=assets/css/dark.css \
			--metadata prev="$$prev" \
			--metadata next="$$next" \
			--metadata title="Embedded Mastery: C, Ada, Rust & Zig" \
			-o $(OUTPUT_DIR)/$$name.html $(OUTPUT_DIR)/$$name.tmp.md; \
		rm -f $(OUTPUT_DIR)/$$name.tmp.md; \
		echo "    -> $(OUTPUT_DIR)/$$name.html"; \
	done
	@touch $@

$(OUTPUT_DIR)/assets/css/style.css: | $(OUTPUT_DIR)
	@cp -r $(ASSETS_DIR) $(OUTPUT_DIR)/

# ---- PDF ----
pdf: $(PDF_OUT)

$(PDF_OUT): $(COMBINED_HTML) | $(OUTPUT_DIR)
	@echo "==> Building PDF (via WeasyPrint)..."
	@cp -r $(ASSETS_DIR) $(OUTPUT_DIR)/
	@$(PANDOC) $(PANDOC_FLAGS) \
		--to html5 \
		--template=templates/pdf.html \
		--css=assets/css/print.css \
		--metadata title="Embedded Mastery: C, Ada, Rust & Zig" \
		-o $(OUTPUT_DIR)/pdf-temp.html $<
	@weasyprint file://$(CURDIR)/$(OUTPUT_DIR)/pdf-temp.html $@
	@rm -f $(OUTPUT_DIR)/pdf-temp.html
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

# ---- Search Index (JSON + inline JS) ----
search-index: $(OUTPUT_DIR)/assets/search-index.json
search-data: $(OUTPUT_DIR)/assets/js/search-data.js

$(OUTPUT_DIR)/assets/search-index.json: $(SRCS) | $(OUTPUT_DIR)
	@echo "==> Generating search index (JSON)..."
	@mkdir -p $(OUTPUT_DIR)/assets
	@python3 $(SCRIPTS_DIR)/generate-search-index.py json $(OUTPUT_DIR)/assets/search-index.json $(SRCS)
	@echo "    -> $(OUTPUT_DIR)/assets/search-index.json"

$(OUTPUT_DIR)/assets/js/search-data.js: $(SRCS) | $(OUTPUT_DIR)
	@echo "==> Generating search index (JS)..."
	@mkdir -p $(OUTPUT_DIR)/assets/js
	@python3 $(SCRIPTS_DIR)/generate-search-index.py js $(OUTPUT_DIR)/assets/js/search-data.js $(SRCS)
	@echo "    -> $(OUTPUT_DIR)/assets/js/search-data.js"

# ---- Help ----
help:
	@echo "Embedded Mastery Tutorial — Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make html     Build HTML (single-page with dark/light theme)"
	@echo "  make pdf      Build PDF (via WeasyPrint)"
	@echo "  make epub     Build EPUB e-book"
	@echo "  make pages    Build individual HTML pages per project"
	@echo "  make search-index  Generate search index JSON"
	@echo "  make search-data   Generate inline search JS (for file:// usage)"
	@echo "  make serve         Start local HTTP server (localhost:8080)"
	@echo "  make all      Build all formats (default)"
	@echo "  make clean    Remove generated files"
	@echo "  make help     Show this help"
	@echo ""
	@echo "Output directory: $(OUTPUT_DIR)/"

# ---- Serve ----
serve: pages
	@echo "==> Starting local server at http://localhost:8080"
	@cd $(OUTPUT_DIR) && python3 -m http.server 8080

# ---- Deploy (GitHub Pages) ----
deploy: pages
	@echo "==> Preparing for deployment..."
	@cp $(OUTPUT_DIR)/00-index.html $(OUTPUT_DIR)/index.html
	@echo "    -> $(OUTPUT_DIR)/index.html"
