#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include "hex_dump.h"

// -- This is 32 for Air2PC cards but lets be nice other might have different cards.
#if !defined(DMX_FILTER_SIZE)
#define DMX_FILTER_SIZE 16
#endif


#define MTU 1500
//#define CHANNEL_FILE "/.azap/channels.conf"
#define CHANNEL_FILE "channels.conf"
#define DEBUG_SUMMARY   		       1
#define ERROR_COMP                            -1
#define SYNC_BYTE                           0x47
#define BASE_PID                            0x1FFB
#define SERVICE_LOCATION_DESCRIPTOR         0xA1
#define TVCG_TABLE_ID                       0xC8
#define CVCG_TABLE_ID                       0xC9
#define MAX_SECTION_SIZE 		    8192
#define DVB_TIMEOUT                         9000 /* Previously 8360 */

#define VCT_MAX_SECTION_SIZE                1021
#define VCT_MAX_SECTION_NUMBER               256
#define VCT_HDR_OFFSET                        10
#define VCT_ITEM_SIZE                         18
#define DEBUG                                  0
#define SCANMODE_FIXED                         8
#define SCANMODE_NORMAL                        1

#define BAD_ARG                                1
#define INVALID_RANGE                          8
#define INVALID_VALUE                          16
	


#define ERROR(x...)                                                     \
        do {                                                            \
                fprintf(stderr, "ERROR: ");                             \
                fprintf(stderr, x);                                     \
                fprintf (stderr, "\n");                                 \
        } while (0)

#define PERROR(x...)                                                    \
        do {                                                            \
                fprintf(stderr, "ERROR: ");                             \
                fprintf(stderr, x);                                     \
                fprintf (stderr, " (%s)\n", strerror(errno));		\
        } while (0)


typedef struct {
	char *name;
	int value;
} Param;


typedef struct  
{	
	unsigned int  *vpid;
	unsigned int  *apid;
	char	      *lang_Code[256][3];
	int num_vpids;
	int num_apids;	
} DTV_PIDS;

struct DTVChannel {
	char name[256][8];
	unsigned int *major_channel_number;
	unsigned int *minor_channel_number;
	unsigned char number_of_channels;
	unsigned long freq;
	unsigned char *number_of_elements;
	unsigned char *modulation_type;	
	DTV_PIDS *dtv_pids;
};


static const Param modulation_list [] = {
	{ "8VSB", VSB_8 },
	{ "16VSB", VSB_16 },
	{ "QAM_64", QAM_64 },
	{ "QAM_256", QAM_256 },
};

#define LIST_SIZE(x) sizeof(x)/sizeof(Param)

static char FRONTEND_DEV [80];
static char DEMUX_DEV [80];
static char DVR_DEV [80];


/* Center frequencies for NTSC channels */
static int ntsc[ ] = { 
 0,  0,  57,  63,  69,  79,  85, 177, 183, 189 ,
 195, 201, 207, 213, 473, 479, 485, 491, 497, 503 ,
 509, 515, 521, 527, 533, 539, 545, 551, 557, 563 ,
 569, 575, 581, 587, 593, 599, 605, 611, 617, 623 ,
 629, 635, 641, 647, 653, 659, 665, 671, 677, 683 ,
 689, 695, 701, 707, 713, 719, 725, 731, 737, 743 ,
 749, 755, 761, 767, 773, 779, 785, 791, 797, 803 
};


/*
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
	
	
	
*/

/*
  ###############################################################
  #    Set card to filter on the following PID's                #
  ###############################################################
*/
static int setup_frontend (int fe_fd, struct dvb_frontend_parameters *frontend ) 
{
	struct dvb_frontend_info fe_info;

	if (ioctl(fe_fd, FE_GET_INFO, &fe_info) < 0) {
		PERROR("ioctl FE_GET_INFO failed");
		return -1;
	}

	if (fe_info.type != FE_ATSC) {
		ERROR ("frontend device is not an ATSC (VSB/QAM) device");
		return -1;
	}

	return 0;
}


/*
  ###############################################################
  #    Set card to filter on the following PID's                #
  ###############################################################
*/
static int set_filter(int fd, 
		      unsigned int pid,
		      const unsigned char* filter, 
		      const unsigned char* mask,
		      unsigned int timeout )
{
	struct dmx_sct_filter_params f;
	unsigned long bufsz;

	if (getenv("BUFFER")) 
	{
		bufsz=strtoul(getenv("BUFFER"), NULL, 0);
		if (bufsz > 0 && bufsz <= MAX_SECTION_SIZE) 
		{
			fprintf(stderr, "DMX_SET_BUFFER_SIZE %lu\n", bufsz);
			if (ioctl(fd, DMX_SET_BUFFER_SIZE, bufsz) == -1) 
			{
				perror("DMX_SET_BUFFER_SIZE");
				return 1;
			}
		}
	}
	
	memset(&f.filter, 0, sizeof(struct dmx_filter));	
	memcpy(f.filter.filter, filter, DMX_FILTER_SIZE);
	memcpy(f.filter.mask, mask, DMX_FILTER_SIZE);
	
	f.pid = (uint16_t) pid;
	f.timeout = timeout;
	f.flags = DMX_IMMEDIATE_START | DMX_CHECK_CRC;

	if (ioctl(fd, DMX_SET_FILTER, &f) == -1) {
		perror("DMX_SET_FILTER");
		return 1;
	}
	
	return 0;
}


/*
  ########################################################################
  # Process only the VCT section of the PSIP to find Audio/Video PID's   #
  ########################################################################
*/
int process_vct_section(int fd, struct DTVChannel *foundDTVChannel )
{
	uint8_t  buf[MAX_SECTION_SIZE];
	uint8_t  number_of_elements = 0;
	uint8_t  *buf_ptr;
	uint8_t  *desc_ptr;
	uint8_t  desc_len;
	uint8_t  stream_type;
	uint8_t  vidcnt   = 0;
	uint8_t  audiocnt = 0;
	
	uint16_t elementary_pid;
	uint16_t descr_length;
		
	int bytes = 0;
	int i =0, j = 0, h = 0, k =0;

	// -- Try to read	
	bytes = read( fd, &buf, sizeof( buf ) );
	
	if( DEBUG ) printf("read %d bytes\n", bytes);
		
	if( bytes < 0 ) 
	{
		//perror("read");
				
		if( errno == ETIMEDOUT ) 
		{
			printf("Timeout waiting for valid data to arrive!\n");
			return -1;
		}

		if( errno == EOVERFLOW )
			return -1;
		
		return -1;
	}
		
	buf_ptr = &buf[10];
		
	foundDTVChannel->number_of_channels = (buf[9] & 0xFF);
	foundDTVChannel->major_channel_number = malloc( sizeof(int) * foundDTVChannel->number_of_channels  );
	foundDTVChannel->minor_channel_number = malloc( sizeof(int) * foundDTVChannel->number_of_channels  );	
	foundDTVChannel->dtv_pids             = malloc( sizeof(DTV_PIDS) * foundDTVChannel->number_of_channels  );			
	foundDTVChannel->modulation_type      = malloc( sizeof(char) * foundDTVChannel->number_of_channels  );	
	foundDTVChannel->number_of_elements   = NULL;

	for( i=0; i < foundDTVChannel->number_of_channels; i++ )
	{
			memset( foundDTVChannel->name[i], '\0',  sizeof(char) * 8  );
			foundDTVChannel->dtv_pids[i].vpid = NULL;
			foundDTVChannel->dtv_pids[i].apid = NULL;
			
			for(j=0;j<7;j++) 
			{
				foundDTVChannel->name[i][j] = ((buf_ptr[0]&0xFF)<<8) | (buf_ptr[1]&0xFF);
				buf_ptr+=2;
			}

			foundDTVChannel->major_channel_number[i] = ((buf_ptr[0]&0xF  )<<6) | ((buf_ptr[1]>>2)&0x3F);
			foundDTVChannel->minor_channel_number[i] = ((buf_ptr[1]&0x03 )<<8) |  (buf_ptr[2]&0xFF);
			foundDTVChannel->modulation_type[i]      = ( buf_ptr[3]&0xFF );

			descr_length = ((buf_ptr[16]&0x03)<<8) | (buf_ptr[17]&0xFF);
			
			buf_ptr+=18;

			desc_ptr = buf_ptr; // Assign our Desc pointer to its proper location in out buffer
			
			desc_len = 0;
			
			desc_len = desc_ptr[1] & 0xFF;
			
			for( k=0;k<desc_len;k++)
			{
				if( DEBUG ) printf("Desc Type: 0x%x \n", (*desc_ptr & 0xFF) );
				
				switch( (*desc_ptr & 0xFF)  )									
				{					
						case SERVICE_LOCATION_DESCRIPTOR:
						vidcnt   = 0;
						audiocnt = 0;
                        
						/*  This is IMPORTANT this stops us from trying to read the service information in a NTSC broadcast information in the VCT */
						if( foundDTVChannel->modulation_type[i] == 0x01 )
	  					    number_of_elements = 0;
						else
						    number_of_elements = (desc_ptr[4] & 0xFF);
						
						if( foundDTVChannel->number_of_elements == NULL ) 
						{
							foundDTVChannel->number_of_elements = malloc( sizeof(int) * foundDTVChannel->number_of_channels );											
						}
		
						foundDTVChannel->number_of_elements[i] = number_of_elements;
										
						if( foundDTVChannel->dtv_pids[i].vpid == NULL ) 
						{
							foundDTVChannel->dtv_pids[i].vpid = malloc( sizeof(int) * number_of_elements );		
							//memset( foundDTVChannel->dtv_pids[i].vpid, 0, sizeof(int) * foundDTVChannel->number_of_channels * number_of_elements );
						}
						
						if( foundDTVChannel->dtv_pids[i].apid == NULL ) 
						{
							foundDTVChannel->dtv_pids[i].apid = malloc( sizeof(int) * number_of_elements );	
							//memset( foundDTVChannel->dtv_pids[i].apid, 0, sizeof(int) * foundDTVChannel->number_of_channels * number_of_elements );					
						}
						
						for( h=0; h<number_of_elements; h++)
						{
							stream_type    = (desc_ptr[5]  & 0xFF);
							elementary_pid = ((desc_ptr[6] & 0x1F) << 8 ) | (desc_ptr[7] & 0xFF);
							
							if( stream_type == 0x02 ) 
							{						
								foundDTVChannel->dtv_pids[i].vpid[vidcnt] = elementary_pid;						
								vidcnt++;
							}
							else if( stream_type == 0x81 ) 
							{
								foundDTVChannel->dtv_pids[i].apid[audiocnt] = elementary_pid;
								audiocnt++;
							}
							
							//memcpy( foundDTVChannel->dtv_pids[i].lang_Code[i],&desc_ptr[8], sizeof(char) * 3);
							
							/*
							ISO_639_lang_Code[0] = desc_ptr[8];
							ISO_639_lang_Code[1] = desc_ptr[9];
							ISO_639_lang_Code[2] = desc_ptr[10];					
							*/
							
							desc_ptr+=6; 
						}
						
						foundDTVChannel->dtv_pids[i].num_apids = audiocnt;
						foundDTVChannel->dtv_pids[i].num_vpids = vidcnt;
					break;
				}			
			}

		buf_ptr+=descr_length;
	 }

	if( DEBUG ) hex_dump(buf, bytes);

        printf( "found %d Digital Channels \n", foundDTVChannel->number_of_channels );
	printf( "\n");
	
	for(i=0;i<foundDTVChannel->number_of_channels;i++)
	{
		printf ("Name = %s \n",              foundDTVChannel->name[i] );	
		printf ("Channel %d-%d \n",          foundDTVChannel->major_channel_number[i], foundDTVChannel->minor_channel_number[i] );
		printf ("Modulation Type  0x%x\n",   foundDTVChannel->modulation_type[i] );
		
		if( foundDTVChannel->number_of_elements != NULL && foundDTVChannel->modulation_type[i] != 0x01 ) 
		{
			printf ("Number_of_element = %d \n", foundDTVChannel->number_of_elements[i] );					
			
			for ( h = 0;h < foundDTVChannel->number_of_elements[i]; h++)
			{		
				if( h < foundDTVChannel->dtv_pids[i].num_vpids ) 
					printf ("Video PID = DEC: %d HEX: 0x%x \n",foundDTVChannel->dtv_pids[i].vpid[h], foundDTVChannel->dtv_pids[i].vpid[h] );					
				
				if( h < foundDTVChannel->dtv_pids[i].num_apids )
					printf ("Audio PID = DEC: %d HEX: 0x%x\n",foundDTVChannel->dtv_pids[i].apid[h], foundDTVChannel->dtv_pids[i].apid[h] );				
				
				foundDTVChannel->dtv_pids[i].vpid[audiocnt] = elementary_pid;
			}	
		}
		else
		    printf(" NO SERVICE LOCATION DESCRIPTOR AVAILABLE!\n");
		
		printf("\n");		
	}

	return 0;	
}



/*###############################################################
  #    Write out valid Digital TV channels in a azap format     #
  ###############################################################*/
int write_channels(  struct DTVChannel foundDTVChannel, FILE *fp )
{
	int h, i;
	
	for(i=0;i<foundDTVChannel.number_of_channels;i++)
	{
		if( foundDTVChannel.number_of_elements != NULL && foundDTVChannel.modulation_type[i] != 0x01 ) 
		{			
			if( foundDTVChannel.number_of_elements[i] > 1  )
			{
			    if( foundDTVChannel.dtv_pids[i].num_vpids > 0 && foundDTVChannel.dtv_pids[i].num_apids > 0 )
				fprintf( fp, "%s:%i:8VSB:%d:%d\n", foundDTVChannel.name[i] , foundDTVChannel.freq, foundDTVChannel.dtv_pids[i].vpid[0] , foundDTVChannel.dtv_pids[i].apid[0] );
			    else
				fprintf( fp, "%s:%i:8VSB:0:0\n", foundDTVChannel.name[i] , foundDTVChannel.freq  );
			}
			else 
			{
				fprintf( fp, "%s:%i:8VSB:0:0\n", foundDTVChannel.name[i] , foundDTVChannel.freq  );
			}
		}
	}

	return 0;
}

/*###############################################################
  #    Scan for valid channels in UHF band                      #
  ###############################################################*/
static int scanner( int fe_fd, struct dvb_frontend_parameters *frontend , enum fe_modulation modulation, int start_chan , int scanmode )
{
	fe_status_t status;
	struct DTVChannel myDTVChannel;
		
	uint16_t snr = 0, signal = 0;
	uint16_t avg_snr = 0, avg_signal = 0;
	uint32_t ber, uncorrected_blocks;
	uint32_t error_count  = 0;
			
	int x = 0, y = 0;
  	int dtvchannel = 0;
  	int hz = 0;  	
	int attempt = 0;
	int lockcount = 0;
	int num_attempts = 6;
	int success = 0;
	int i = 0;
	
	int dmxfd;
	unsigned char filter[DMX_FILTER_SIZE];
        unsigned char mask[DMX_FILTER_SIZE];
	
	FILE *fp = NULL;
	
	// -- Don't write scan file on fixed scan
	if( scanmode != SCANMODE_FIXED )
	    fp = fopen( CHANNEL_FILE, "w" );
	
	memset( filter, '\0', sizeof(filter));	
	memset( mask, '\0', sizeof(mask));
	
	if( modulation)	    	
	    filter[0] = TVCG_TABLE_ID; // Look for the TVCG Table in Stream for OTA   DTV
	else
	    filter[0] = CVCG_TABLE_ID; // Look for the CVCG Table in Stream for Cable DTV
    
	mask[0]   = 0xFF; // Default Mask
	
	// -- Assign Start RF channel number	
	dtvchannel=start_chan;
	
	do	 
	{
		
		//if( scanmode == SCANMODE_FIXED ) dtvchannel = start_chan;
			
                hz = ntsc[dtvchannel] * 1000000;

		frontend->u.vsb.modulation = modulation;
		frontend->frequency = hz;
		  
		lockcount   = 0; 
		attempt     = 0;
		error_count = 0;
		avg_snr     = 0;
		avg_signal  = 0;

		printf ("Attempting to tuning to %i Hz UHF channel %d \n", frontend->frequency, dtvchannel );

		if (ioctl(fe_fd, FE_SET_FRONTEND, frontend) < 0) {
			PERROR("ioctl FE_SET_FRONTEND failed");
			return -1;
		}
	
		do 
		{		
			success+=ioctl(fe_fd, FE_READ_STATUS, &status);
			success+=ioctl(fe_fd, FE_READ_SIGNAL_STRENGTH, &signal);
			success+=ioctl(fe_fd, FE_READ_SNR, &snr);
			success+=ioctl(fe_fd, FE_READ_BER, &ber);
			success+=ioctl(fe_fd, FE_READ_UNCORRECTED_BLOCKS, &uncorrected_blocks);			
			
			if( success != 0 )
			{
				PERROR("ioctl failed");
				return -1;
			}
			
			if( status & FE_HAS_LOCK )
			{
				printf ("status %02x | signal %04x | snr %04x | "
						"ber %08x | unc %08x | ", status, signal, 
						snr, ber, uncorrected_blocks);
				
				error_count+=uncorrected_blocks;				
				printf("FE_HAS_LOCK");			
				printf("\n");					
				lockcount++;						
				avg_snr+=snr;
				avg_signal+=signal;

			}
			
			attempt++;
				
			usleep( 1000000 );
			
			if( attempt > 2 && lockcount == 0 ) break;		
			
		} while (attempt<num_attempts);
		
		// -- If error count is too high then sometimes the program can stall at reading 
		// -- data from the device this is because were only getting TS packets that have a valid CRC.

		if( lockcount > 2 ) 
		{
			printf( "Num Locks %d \n", lockcount );
			printf( "Error Count %d \n", error_count );			
			printf( "Average SNR %d \n", ( ( avg_snr  ) / lockcount ));
	 	   	printf( "Average Signal %d \n", ( ( avg_signal ) / lockcount) );	
		}
		
		if( lockcount > 3  ) 
		{	
			printf("UHF Channel %d, is at HZ %d\n", dtvchannel, hz );
			printf( "\n");
			if ( (dmxfd = open(DEMUX_DEV, O_RDWR)) < 0) 
			{
			      PERROR("failed opening '%s'", DEMUX_DEV);
			      return -1;
			}
			
			// -- Set Hardware to filter to only receive tvct sections
			// -- Remember out Air2PC card can do 32 pids filter at the same time.
			set_filter( dmxfd, BASE_PID, filter, mask, DVB_TIMEOUT );			
			
			int isOk = process_vct_section( dmxfd, &myDTVChannel );
			
			myDTVChannel.freq = hz;
			
			if( isOk > -1 )
			{
				/* Write out all valid Digital Channels found */
				if( scanmode != SCANMODE_FIXED ) write_channels( myDTVChannel , fp );

				/* Free Memory */
				if( myDTVChannel.number_of_elements != NULL ) 
				{				
					for(i=0;i<myDTVChannel.number_of_channels; i++)
					{
						free( myDTVChannel.dtv_pids[i].apid );
						free( myDTVChannel.dtv_pids[i].vpid );
					}
				}
				
				free( myDTVChannel.major_channel_number );
				free( myDTVChannel.minor_channel_number );
	
				if( myDTVChannel.number_of_elements != NULL ) 
				{
					free( myDTVChannel.number_of_elements );
					free( myDTVChannel.dtv_pids );
				}
			}

			// -- close device otherwise buffers may have data from a past channel change			
			close( dmxfd );
		}
		else if( lockcount > 2 )
		{
			printf("Found Good Signal Lock But To Many Errors High! (try ajusting antenna)\n");
		}
		
		if( scanmode != SCANMODE_FIXED ) dtvchannel++;
			
	}while ( dtvchannel < 70  );	
	
	if( fp != NULL ) fclose(fp);
	
	return 0;
}

/*###############################################################
  #   Display usage and exit                                    #
  ###############################################################*/
void usage()
{
     fprintf( stdout, "[-c] channels [ 3 - 70]");
     fprintf( stdout, "[-qam] modulation 64 or 256 ");
     fprintf( stdout, "[-vsb] modulation 8 or 16 [Default: 8]");
     fprintf( stdout, "[--fixedscan] continue to scab a channel until ctrl-c");     
     exit( 0 );

}

/*###############################################################
  #  Main program Entry                                         #
  ###############################################################*/
int main( int argc, char *argv[] )
{
	
	int adapter = 0;
	int frontend = 0, demux = 0, dvr = 0;	
	int frontend_fd, dvr_fd, dmxfd;
	int start_chan = 2; /* Start channel at first valid UHF channel */	
	int scan_mode = SCANMODE_NORMAL;	
	int mod_type = 0;   /* Default VSB8 */
	int verbose  = 0;
	int c        = 0;
	int temp     = 0;
	
	char *modtypes_name[ ] = { "8VSB", "16VSB" ,"QAM_64", "QAM_256" };
	struct dvb_frontend_parameters frontend_param;
	enum fe_modulation modulation_type[] = { VSB_8, VSB_16 ,QAM_64, QAM_256 }; 

	argv++;
	while( (argc--) > 0 ) 
	{	      
	      c = argc;
	      
      	      if( c > 1 && strcmp(*argv,"-c") == 0 ) 
	      {		 
		  argv++;
		  argc--;
		  temp = atoi(*argv);
		  if( temp > 1 && temp < 71)
		      start_chan = temp;		  
		  else 
		  {
		      fprintf( stdout, "Invalid channel value %d\n", temp );
		      exit( BAD_ARG );
		  }
	      }

	      
	      if( c > 1 && strcmp(*argv,"-vsb") == 0 ) 
	      {	
		  argc--;
		  argv++;
		  temp = atoi(*argv);
		  if( temp == 8 )
		      mod_type = 0;
		  else if( temp == 16 )
		      mod_type = 1;
		  else 
		  {
		      fprintf( stdout, "Invalid value for VSB modulation %d\n", temp );
		      exit( BAD_ARG );
		  }
	      }

	      if( c > 1 && strcmp(*argv,"-qam") == 0 ) 
	      {		  
		  argv++;
		  argc--;		  
		  temp = atoi(*argv);
		  
		  if( temp == 64 )
		      mod_type = 2;
		  else if ( temp == 256 )
		      mod_type = 3;
		  else 
		  {
		     fprintf( stdout, "Invalid value for QAM modulation %d\n", temp );
		     exit( BAD_ARG );
		  }
	      }
	      
	      if( c > 1 && strcmp(*argv,"--fixedscan") == 0) 
	      {
		  scan_mode = SCANMODE_FIXED;
	      }

	      if( c > 1 && strcmp(*argv,"-h") == 0) 
	      {
		  usage();
	      }
	      
	      argv++;
        }
	
	memset(&frontend_param, 0, sizeof(struct dvb_frontend_parameters));
	
	snprintf (FRONTEND_DEV, sizeof(FRONTEND_DEV),"/dev/dvb/adapter%i/frontend%i", adapter, frontend);
	snprintf (DEMUX_DEV, sizeof(DEMUX_DEV), "/dev/dvb/adapter%i/demux%i", adapter, demux);
	snprintf (DVR_DEV, sizeof(DVR_DEV), "/dev/dvb/adapter%i/dvr%i", adapter, dvr);
	
	printf ( "Using '%s'\n", FRONTEND_DEV );
	printf ( "Using '%s'\n", DEMUX_DEV    );
	printf ( "Using '%s'\n", DVR_DEV );
	printf ( "Using Modulation Type '%s'\n", modtypes_name[mod_type] );
	if( scan_mode == SCANMODE_FIXED) printf ( "[Fixed Scan Mode Enabled]\n Ctrl-C to stop \n" );
		
  	if ( (frontend_fd = open(FRONTEND_DEV, O_RDWR)) < 0) 
	{
	      PERROR ("failed opening '%s'", FRONTEND_DEV);
	      return -1;
	}

	if ( setup_frontend (frontend_fd, &frontend_param) < 0)
	{
	     PERROR ("failed setup '%s'", DVR_DEV);	
	     return -1;
	}
  	
	// -- Lets pre open or device so we can see if we can open the device in WRITE mode
	if ( (dmxfd = open(DEMUX_DEV, O_RDWR)) < 0) 
	{
	   PERROR("failed opening '%s'", DEMUX_DEV);
	   return -1;
	}
	
	close (dmxfd);
	
	// -- Start Scanner
	scanner( frontend_fd, &frontend_param, modulation_type[mod_type], start_chan , scan_mode);
		
	close (frontend_fd);				
	close (dmxfd);
	close (dvr_fd);
		  	  
    return 0;
}
