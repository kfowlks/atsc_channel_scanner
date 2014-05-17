#
# Makefile for ATSC Autoscan with Service Location Decoder
# Author: Kevin Fowlks

INC    = -I/usr/src/dvb-kernel/linux/include
all: atsc_channel_scan

hdtvrecorder: 
	gcc hdtvrec.c -o hdtvrecorder -Wall -O3 -lpthread -lm -lrt
	
atsc_channel_scan: channel_scan_atsc.o hex_dump.o
	gcc -Wall -g -o atsc_channel_scan channel_scan_atsc.o hex_dump.o

channel_scan_atsc.o:
	gcc -c channel_scan_atsc.c $(INC)

hex_dump.o:
	gcc -c hex_dump.c

clean:
	rm -f *.o atsc_channel_scan hdtvrecorder
	rm -f atsc_scan.tar.gz
	rm -rf atsc_channel_scanner/

dist:	atsc_channel_scan
	mkdir -p atsc_channel_scanner
	cp *.c atsc_channel_scanner/
	cp *.h atsc_channel_scanner/
	cp Makefile atsc_channel_scanner/
	cp atsc_channel_scan atsc_channel_scanner/

package: dist
	tar -cvzf atsc_scan.tar.gz atsc_channel_scanner/
	
