#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parseMork.h"

void usage() {
	fprintf( stderr, "usage: mork [-v] [-V vCardFileName] abook.mab\n" );
	fprintf( stderr, " -g               : Do not parse groups\n" );
	fprintf( stderr, " -v               : Verbose\n" );
	fprintf( stderr, " -V vCardFileName : write vCards to the file\n" );
}

int main( int argc, char **argv ) {
	char *vCardFile = (char *) 0;
	char *arg;
	int i;
	morkDb *mork;
	//morkLogfp = stdout;
	morkLogfp = 0;
	morkErrfp = stderr;
	for( i = 1; i < argc; ++i ) {
		arg = argv[i];
		switch( *arg ) {
		case '-':	// Options
			++arg;
			switch( *arg ) {
			case 'g':	// Group parsing off
				morkDoNotParseGroups = 1;
				break;
			case 'v':	// verbose
				morkLogfp = stdout;
				break;
			case 'V':	// vCard
				if( !*(++arg) ) arg = argv[++i];
				vCardFile = arg;
				break;
			default:
				usage();
				return -1;
				break;
			}
			break;
		default:	// File name
			mork = parseMorkFile( argv[i] );
			fprintf( stdout, "\nDump of Mork Data\n" );
			fprintf( stdout, "----- columns table -----\n" );
			dumpMorkColumns( stdout, mork );
			fprintf( stdout, "----- values table -----\n" );
			dumpMorkValues( stdout, mork );
			fprintf( stdout, "----- mork structure -----\n" );
			dumpTableScopeMap( stdout, mork );
			if( vCardFile ) {
				FILE *vCardfp = fopen( vCardFile, "w" );
				dumpVcards( vCardfp, mork );
				fclose( vCardfp );
			}
			freeMorkDb( mork );
			break;
		}
	}
	return 0;
}
