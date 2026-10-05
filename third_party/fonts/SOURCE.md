# Font sources

`5x7.bdf`, `6x10.bdf`, `7x13.bdf`, `9x15.bdf` and `10x20.bdf` are five of the "misc-fixed"
Unicode bitmap fonts maintained by Markus Kuhn, taken from
https://www.cl.cam.ac.uk/~mgk25/download/ucs-fonts.tar.gz. They are in the
public domain; `README` is the file that accompanies them.

`make fonts` converts them into `src/font_data.c` with `tools/genfont.py`.
