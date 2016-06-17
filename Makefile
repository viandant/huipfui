LDLIBS = -lconfig

% : %.c
	$(CC) $(LDLIBS) $(CFLAGS) $(CPPFLAGS) $< -o $@

all: in2hid hid2out

in2hid: in2hid.c hipipe.h

hid2out: hid2out.c hipipe.h

README.md: README.tex
	pandoc README.tex -o README.md

README.pdf: README.tex readme_frame.tex
	pdflatex readme_frame && pdflatex reamde_frame && mv readme_frame.pdf README.pdf

