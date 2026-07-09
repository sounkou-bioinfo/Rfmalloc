# Monorepo driver. Per-package builds keep using packages/<pkg>/Makefile;
# these targets orchestrate the stack in dependency order and run the
# cross-package pieces (integration tests, fixtures, the root README).
#
#   make install               # install all four packages, dependency order
#   make test                  # install + every suite against the co-installed stack
#   make check                 # R CMD check --no-manual for each package
#   make check PKGS=Rgguf      # ... or a subset
#   make rd                    # roxygenise every package
#   make rdm                   # re-render the root README.Rmd (runs the live demo)
#   make fixtures              # regenerate Rgguf's ggml-reference codec fixture
#   make clean                 # scrub build artifacts everywhere

# Dependency order matters: Rfmalloc and Rggml are roots, Rgguf needs
# Rfmalloc, Rllm needs all three.
PKGS = Rfmalloc Rggml Rgguf Rllm Rpgen

.PHONY: all install test check rd rdm fixtures clean

all: test

install:
	@for p in $(PKGS); do \
		echo "==== R CMD INSTALL packages/$$p ===="; \
		R CMD INSTALL --preclean packages/$$p || exit 1; \
	done

test: install
	Rscript tests/integration.R

check:
	@for p in $(PKGS); do \
		echo "==== R CMD check packages/$$p ===="; \
		R CMD check --no-manual packages/$$p || exit 1; \
	done

rd:
	@for p in $(PKGS); do \
		R -e "roxygen2::roxygenise('packages/$$p')" || exit 1; \
	done

rdm: install
	R -e 'rmarkdown::render("README.Rmd", output_format = "github_document", quiet = TRUE)'
	@rm -f README.html

fixtures: install
	Rscript tools/make_codec_fixtures.R

clean:
	@rm -rf packages/*.Rcheck *.Rcheck
	@for p in $(PKGS); do \
		rm -rf packages/$$p/src/*.o packages/$$p/src/*.so packages/$$p/src/*.dSYM \
		       packages/$$p/*.tar.gz packages/$$p/$$p.Rcheck; \
	done
