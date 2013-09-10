
mork:	mork.c parseMork.c vCard.c
	gcc -Wall mork.c parseMork.c vCard.c -o $@

install:	/usr/local/bin/mork

/usr/local/bin/mork: mork
	cp -p mork /usr/local/bin/mork

test:
	./mork -v -V test.abook.vcf test.abook.mab >test.abook.out
