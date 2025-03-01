// chiaharvestgraph.c
// 
// (c)2021 by Abraham Stolk.
// XCH Donations: xch1zfgqfqfdse3e2x2z9lscm6dx9cvd5j2jjc7pdemxjqp0xp05xzps602592

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <termios.h>
#include <sys/stat.h>   // stat
#include <stdbool.h>    // bool type
#include <pwd.h>

#include "grapher.h"
#include "colourmaps.h"


#define MAXLOGSZ		(64*1024*1024)
#define MAXLINESZ		1024

#define WAIT_BETWEEN_SELECT_US	500000L

#define	MAXHIST			( 4 * 24 * 7 )	// A week's worth of quarter-hours.
#define MAXENTR			( 12 * 15 )	// We expect 6 per minute, worst-case: 12 per min, 180 per quarter-hr.

typedef struct quarterhr
{
	time_t	stamps[ MAXENTR ];
	int	eligib[ MAXENTR ];
	int	proofs[ MAXENTR ];
	float	durati[ MAXENTR ];
	int	sz;
	time_t	timelo;
	time_t	timehi;
} quarterhr_t;

const char* logfilenames[8] =
{
	"debug.log.7",
	"debug.log.6",
	"debug.log.5",
	"debug.log.4",
	"debug.log.3",
	"debug.log.2",
	"debug.log.1",
	"debug.log",
};


quarterhr_t quarters[ MAXHIST ];

static int entries_added=0;	// How many log entries have we added in total?

static time_t newest_stamp=0;	// The stamp of the latest entry.

static time_t refresh_stamp=0;	// When did we update the image, last?

static struct termios orig_termios;

static const rgb_t* ramp=0;


static void init_quarters( time_t now )
{
	time_t q = now / 900;
	time_t q_lo = (q+0) * 900;
	time_t q_hi = (q+1) * 900;
	for ( int i=MAXHIST-1; i>=0; --i )	// [0..MAXHIST)
	{
		const int ir = MAXHIST-1-i;	// [MAXHIST-1..0]
		quarters[i].sz = 0;
		quarters[i].timelo = q_lo - 900 * ir;
		quarters[i].timehi = q_hi - 900 * ir;
	}
}


static void shift_quarters( void )
{
	fprintf( stderr, "Shifting quarters...\n" );
	for ( int i=0; i<MAXHIST-1; ++i )
		quarters[i] = quarters[i+1];
	const int last = MAXHIST-1;
	memset( quarters+last, 0, sizeof(quarterhr_t) );
	quarters[ last ].timelo = quarters[ last-1 ].timelo + 900;
	quarters[ last ].timehi = quarters[ last-1 ].timehi + 900;
}


static int too_old( time_t t )
{
	return t < quarters[ 0 ].timelo;
}


static int too_new( time_t t )
{
	const int last = MAXHIST-1;
	return t >= quarters[last].timehi;
}


static int quarterslot( time_t tim )
{
	const int last = MAXHIST-1;
	const time_t d = tim - quarters[last].timehi;
	if ( d >= 0 )
		return INT_MAX;
	return MAXHIST - 1 + ( d / 900 );
}


static int add_entry( time_t t, int eligi, int proof, float durat )
{
	while ( too_new( t ) )
		shift_quarters();
	if ( too_old( t ) )
		return 0;
	int s = quarterslot( t );
	assert( s>=0 );
	assert( s<MAXHIST );
	const int i = quarters[s].sz;
	assert( i < MAXENTR );
	quarters[s].stamps[i] = t;
	quarters[s].eligib[i] = eligi;
	quarters[s].proofs[i] = proof;
	quarters[s].durati[i] = durat;
	quarters[s].sz += 1;
	return 1;
}


static FILE* f_log = 0;


static FILE* open_log_file(const char* dirname, const char* logname)
{
	if ( f_log )
		fclose( f_log );
	if ( !logname )
		logname = logfilenames[7];

	char fname[PATH_MAX+1];
	snprintf( fname, sizeof(fname), "%s/%s", dirname, logname );
	f_log = fopen( fname, "rb" );
	if ( !f_log )
	{
		fprintf( stderr, "Failed to open log file '%s'\n", fname );
		return 0;
	}

#if 0	// No need for non blocking IO.
	const int fd = fileno( f_log );
	assert( fd );
	int flags = fcntl( fd, F_GETFL, 0 );
	fcntl( fd, F_SETFL, flags | O_NONBLOCK );
#endif

	return f_log;
}

// Parses log entries that look like this:
// 2021-05-13T09:14:35.538 harvester chia.harvester.harvester: INFO     0 plots were eligible for farming c1c8456f7a... Found 0 proofs. Time: 0.00201 s. Total 36 plots

static void analyze_line(const char* line, ssize_t length)
{
	if ( length > 60 )
	{
		if ( !strncmp( line+24, "harvester ", 10 ) )
		{
			int year=-1;
			int month=-1;	
			int day=-1;
			int hours=-1;
			int minut=-1;
			float secon=-1;
			int eligi = -1;
			int proof = -1;
			float durat = -1.0f;
			int plots = -1;
			char key[128];
			const int num = sscanf
			(
				line,
				"%04d-%02d-%02dT%02d:%02d:%f harvester chia.harvester.harvester: INFO "
				"%d plots were eligible for farming %s Found %d proofs. Time: %f s. Total %d plots",
				&year,
				&month,
				&day,
				&hours,
				&minut,
				&secon,
				&eligi,
				key,
				&proof,
				&durat,
				&plots
			);
			if ( num == 11 )
			{
				struct tm tim =
				{
					(int)secon,	// seconds 0..60
					minut,		// minutes 0..59
					hours,		// hours 0..23
					day,		// day 1..31
					month-1,	// month 0..11
					year-1900,	// year - 1900
					-1,
					-1,
					-1
				};
				const time_t logtim = mktime( &tim );
				assert( logtim != (time_t) -1 );

				if ( logtim > newest_stamp )
				{
					const int added = add_entry( logtim, eligi, proof, durat );
					if ( added )
						newest_stamp = logtim;
					entries_added += added;
				}
				else
				{
					// Sometimes a whole bunch of harvester runs are done in the very same second. Why?
					//fprintf(stderr, "Spurious entry: %s", line);
				}
			}
		}
	}
}


static int read_log_file(void)
{
	assert( f_log );
	static char* line = 0;
	static size_t linesz=MAXLINESZ;
	if ( !line )
		line = (char*)malloc(MAXLINESZ);

	int linesread = 0;

	do
	{
		struct timeval tv = { 0L, WAIT_BETWEEN_SELECT_US };
		fd_set rdset;
		FD_ZERO(&rdset);
		int log_fds = fileno( f_log );
		FD_SET( log_fds, &rdset );
		const int ready = select( log_fds+1, &rdset, NULL, NULL, &tv);

		if ( ready < 0 )
			error( EXIT_FAILURE, errno, "select() failed" );

		if ( ready == 0 )
		{
			//fprintf( stderr, "No descriptors ready for reading.\n" );
			return linesread;
		}

		const ssize_t ll = getline( &line, &linesz, f_log );
		if ( ll <= 0 )
		{
			//fprintf( stderr, "getline() returned %zd\n", ll );
			clearerr( f_log );
			return linesread;
		}

		linesread++;
		analyze_line( line, ll );
	} while(1);
}


static void draw_column( int nr, uint32_t* img, int h )
{
	const int q = MAXHIST-1-nr;
	if ( q<0 )
		return;
	const time_t qlo = quarters[q].timelo;
	const int sz = quarters[q].sz;
	const int band = ( ( qlo / 900 / 4 ) & 1 );
	for ( int y=0; y<h; ++y )
	{
		const int y0 = y>0   ?  y-1 : y+0;
		const int y1 = y<h-1 ?  y+2 : y+1;
		const time_t r0 = qlo + 900 * (y0 ) / h;
		const time_t r1 = qlo + 900 * (y1 ) / h;
		const time_t s0 = qlo + 900 * (y+0) / h;
		const time_t s1 = qlo + 900 * (y+1) / h;

		int checks=0;
		int eligib=0;
		int proofs=0;
		for ( int i=0; i<sz; ++i )
		{
			const time_t t = quarters[q].stamps[i];
			if ( t >= r0 && t < r1 )
				checks++;
			if ( t >= s0 && t < s1 )
			{
				eligib += quarters[q].eligib[i];
				proofs += quarters[q].proofs[i];
			}
		}
		const time_t span = r1-r0;
#if 0
fprintf(stderr,"span is %zd\n", span);
sleep(10);
exit(1);
#endif
		const float expected = span * (0.1f);
		float achieved = 0.7f * checks / expected;
		achieved = achieved > 1.0f ? 1.0f : achieved;
		const uint8_t idx = (uint8_t) ( achieved * 255 );
		uint32_t red = ramp[idx][0];
		uint32_t grn = ramp[idx][1];
		uint32_t blu = ramp[idx][2];
		if ( band )
		{
			red = red * 200 / 255;
			grn = grn * 200 / 255;
			blu = blu * 200 / 255;
		}
		if ( proofs )
		{
			// Eureka! We found a proof, and will probably get paid sweet XCH!
			red=0x40; grn=0x40; blu=0xff;
		}
		const uint32_t c = (0xff<<24) | (blu<<16) | (grn<<8) | (red<<0);
		img[ y*imw ] = c;
	}
}


static void setup_postscript(void)
{
	snprintf
	(
		postscript,
		sizeof(postscript),

		SETFG "%d;%d;%dm" SETBG "%d;%d;%dm%s"
		SETFG "%d;%d;%dm" SETBG "%d;%d;%dm%s"
		SETFG "%d;%d;%dm" SETBG "%d;%d;%dm%s"
		SETFG "%d;%d;%dm" SETBG "%d;%d;%dm%s"
		SETFG "255;255;255m",

		0xf0,0x00,0x00, 0,0,0, "RED: NO-HARVEST ",
		0xf0,0xa0,0x00, 0,0,0, "ORA: UNDER-HARVEST ",
		0xf0,0xf0,0x00, 0,0,0, "YLW: NOMINAL ",
		0x40,0x40,0xff, 0,0,0, "BLU: PROOF "
	);
}


static void setup_overlay(void)
{
	strncpy( overlay + imw - 4, "NOW", 4 );

	int x = imw - 8;
	int h = 0;
	while( x >= 0 )
	{
		char lab[8] = {0,0,0,0, 0,0,0,0};

		if ( h<12 )
			snprintf( overlay+x, sizeof(lab), "%2dh",  h+1);
		else if ( h%24==0 )
			snprintf( overlay+x, sizeof(lab), "%dDAY", h/24);

		x -= 4;
		h += 1;
	}
}


static int update_image(void)
{
	int redraw=0;

	if ( grapher_resized )
	{
		grapher_adapt_to_new_size();
		setup_overlay();
		redraw=1;
	}

	// Compose the image.
	if ( newest_stamp > refresh_stamp )
		redraw=1;

	if (redraw)
	{
		for ( int col=0; col<imw-2; ++col )
		{
			draw_column( col, im + (3*imw) + (imw-2-col), imh-4 );
		}
		grapher_update();
		refresh_stamp = newest_stamp;
	}
	return 0;
}


static void disableRawMode()
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}


static void enableRawMode()
{
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disableRawMode);
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO);				// Don't echo key presses.
	raw.c_lflag &= ~(ICANON);			// Read by char, not by line.
	raw.c_cc[VMIN] = 0;				// No minimum nr of chars.
	raw.c_cc[VTIME] = 0;				// No waiting time.
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

bool file_exists (const char *filename) {
    struct stat buffer;
    int result = stat(filename, &buffer);
    return result == 0;
}
int main(int argc, char *argv[])
{
    const char *dirname;

    if ((dirname = getenv("HOME")) == NULL) {
        dirname = getpwuid(getuid())->pw_dir;
    }

    strcat(dirname,"/.chia/mainnet/log");

	if (argc != 2)
	{
            char *filePath = malloc(strlen(dirname) + strlen("/debug.log") + 1);
            strcpy(filePath, dirname);
            strcat(filePath, "/debug.log");
	    if(!file_exists(filePath)){
                 fprintf( stderr, "Usage: %s ~/.chia/mainnet/log\n", argv[0] );
                 exit( 1 );
	    }
	    else
	    {
        	 free(filePath);
            }
	}
	else
	{
              strcpy(dirname,argv[1]);
	}
	fprintf( stderr, "Monitoring directory %s\n", dirname );

	int viridis = ( getenv( "CMAP_VIRIDIS" ) != 0 );
	ramp = viridis ? cmap_viridis : cmap_heat;

	init_quarters( time(0) );

	setup_postscript();

	for ( int i=0; i<8; ++i )
	{
		const char* logfilename = logfilenames[i];
		if ( open_log_file( dirname, logfilename ) )
		{
			// Log file exists, we should read what is in it, currently.
			const int numl = read_log_file();
			fprintf( stderr, "read %d lines from log.\n", numl );
		}
	}

	int fd;
	if ( (fd = inotify_init()) < 0 )
		error( EXIT_FAILURE, errno, "failed to initialize inotify instance" );

	int wd;
	if ( (wd = inotify_add_watch ( fd, dirname, IN_MODIFY | IN_CREATE | IN_DELETE ) ) < 0 )
		error( EXIT_FAILURE, errno, "failed to add inotify watch for '%s'", dirname );


	int result = grapher_init();
	if ( result < 0 )
	{
		fprintf( stderr, "Failed to intialize grapher(), maybe we are not running in a terminal?\n" );
		exit(2);
	}

	enableRawMode();
	update_image();

	// Read notifications.
	char buf[ sizeof(struct inotify_event) + PATH_MAX ];
	int done=0;

	do
	{
		int len = read( fd, buf, sizeof(buf) );
		if ( len <= 0 && errno != EINTR )
		{
			error( EXIT_FAILURE, len == 0 ? 0 : errno, "failed to read inotify event" );
		}
		int i=0;
		while (i < len)
		{
			struct inotify_event *ie = (struct inotify_event*) &buf[i];
			if ( ie->mask & IN_CREATE )
			{
				// A file got created. It could be our new log file!
				if ( !strcmp( ie->name, "debug.log" ) )
				{
					fprintf( stderr, "Reopening logfile.\n" );
					open_log_file( dirname, 0 );
					const int numl = read_log_file();
					fprintf( stderr, "read %d lines from log.\n", numl );
				}
			}
			else if ( ie->mask & IN_MODIFY )
			{
				if ( !strcmp( ie->name, "debug.log" ) )
				{
					//fprintf( stderr, "Modified.\n" );
					const int numl = read_log_file();
					(void) numl;
					//fprintf( stderr, "read %d lines from log.\n", numl );
				}
			}
			else if (ie->mask & IN_DELETE)
			{
				printf("%s was deleted\n",  ie->name);
			}

			i += sizeof(struct inotify_event) + ie->len;
		}

		update_image();

		char c=0;
		const int numr = read( STDIN_FILENO, &c, 1 );
		if ( numr == 1 && ( c == 27 || c == 'q' || c == 'Q' ) )
			done=1;
	} while (!done);

	grapher_exit();
	exit(0);
}

