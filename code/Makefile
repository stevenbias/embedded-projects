# Top-level orchestrator for all projects and languages
# Delegates to per-project, per-language Makefiles.

PROJECTS = 01-led-blinker

LANGS_C   = $(addprefix code/$(patsubst code/%,%,$@)/,c)
LANGS_RUST = $(addprefix code/$(patsubst code/%,%,$@)/,rust)
LANGS_ADA  = $(addprefix code/$(patsubst code/%,%,$@)/,ada)
LANGS_ZIG  = $(addprefix code/$(patsubst code/%,%,$@)/,zig)

# ------------------------------------------------------------------
# Build targets
# ------------------------------------------------------------------

.PHONY: all clean
.PHONY: doc lint test fuzz format format-check check
.PHONY: $(PROJECTS)

all: $(PROJECTS)

$(PROJECTS):
	$(MAKE) -C code/$@/c
	$(MAKE) -C code/$@/rust

clean:
	@for p in $(PROJECTS); do \
		$(MAKE) -C code/$$p/c clean 2>/dev/null || true; \
		$(MAKE) -C code/$$p/rust clean 2>/dev/null || true; \
	done

# ------------------------------------------------------------------
# Quality targets (passthrough to each language Makefile)
# Targets that don't exist yet are silently skipped.
# ------------------------------------------------------------------

define QUALITY_TARGET
.PHONY: $(1)
$(1):
	@for p in $(PROJECTS); do \
		echo "--- $$p / c : $(1) ---"; \
		$(MAKE) -C code/$$p/c $(1) 2>/dev/null || true; \
		echo "--- $$p / rust : $(1) ---"; \
		$(MAKE) -C code/$$p/rust $(1) 2>/dev/null || true; \
	done
endef

$(eval $(call QUALITY_TARGET,doc))
$(eval $(call QUALITY_TARGET,lint))
$(eval $(call QUALITY_TARGET,test))
$(eval $(call QUALITY_TARGET,fuzz))
$(eval $(call QUALITY_TARGET,format))
$(eval $(call QUALITY_TARGET,format-check))
$(eval $(call QUALITY_TARGET,check))
