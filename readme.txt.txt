Kevin Fowlks <fowlks(at)msu.edu> Copyright Feb 25th, 2005
	Version 1.0.0 
	Blog: http://core.resonanceone.com/
	Purpose: This tools will scan for available channels for any ATSC device using 
	the Linux DVB API Version 4 interface. It currently scans valid VHF/UHF freq
	for Over the Air(OTA) signals.
	
	Loop Thourgh US UHF Freq Table 
	HZ = UHFFreqTable[1] * 1000000
	IF Found LOCK on HZ
		IF WE HAVE STABLE LOCK 	
		   SCAN TS STREAM FOR (T)/(C)VCT SECTIONS
			DISPLAY CHANNEL NAME
			DISPLAY Audio / Video PIDS
	
	ATSC Standard Revision B (A65/B)
	NIST/DASE API Reference Implementation
	
	
	$History: Version 1.0.2
	Added channels.conf file creations 
	Added some useful command line arguments outlined below
		Fixed mode: useful for continues scanning a channel to determining its call sign
		-c to start at a specifed channel
	Added command switches -vsb and -qam to 
	