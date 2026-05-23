# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build paper.pdf + slides.pdf using latexmk.

LATEXMK ?= latexmk
LATEXMK_BASE := -pdf -interaction=nonstopmode -file-line-error -shell-escape

.PHONY: all paper slides clean distclean

all: paper slides

paper: paper.pdf
slides: slides.pdf

paper.pdf: paper.tex refs.bib
	$(LATEXMK) $(LATEXMK_BASE) paper.tex

slides.pdf: slides.tex refs.bib
	$(LATEXMK) $(LATEXMK_BASE) slides.tex

clean:
	$(LATEXMK) -C paper.tex slides.tex 2>/dev/null || true
	rm -f *.aux *.log *.out *.toc *.bbl *.blg *.bcf *.run.xml \
	      *.nav *.snm *.vrb *.fls *.fdb_latexmk

distclean: clean
	rm -f paper.pdf slides.pdf
