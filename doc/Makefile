tetris-display.pdf tetris-display.xdv: tetris-display.tex cites.bib
	latexmk -pdflatex $<

mostlyclean:
	texclear tetris-display.tex
	$(RM) tetris-display.xdv
clean: mostlyclean
	$(RM) tetris-display.pdf
