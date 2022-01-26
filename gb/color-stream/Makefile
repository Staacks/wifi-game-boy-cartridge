ASM	= rgbasm
LINK = rgblink
FIX = rgbfix

BINS	= color-stream.gb

all:	$(BINS)

compile.bat: Makefile
	@echo "REM Automatically generated from Makefile" > compile.bat
	@make -sn | sed y/\\//\\\\/ | grep -v make >> compile.bat

# Compile and link single file in one pass
%.gb:	%.asm
	$(ASM) -o $@.o $<
	$(LINK) -t -o $@ -n $@.sym $@.o
	$(FIX) -v -p 0 $@

clean:
	rm -f *.o *.lst *.map *.gb *~ *.rel *.cdb *.ihx *.lnk *.sym *.noi

