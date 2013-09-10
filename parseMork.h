/*-----------------------------------------------------------------------------
 *    ParseMork.h - Parser for Thunderbird address books (abook.mab) files
 *
 *    Will load an abook.mab file and report any errors it encounters.
 *
 *    If the 'morkLogfp' file pointer is set it will log what it is doing
 *    so when an error is encountered it can be sorted out.
 *
 *    If the 'morkErrfp' file pointer is NULL no error information will
 *    be printed.
 *
 *    The Mork database can be written out using dumpTableScopeMap().
 *    Alternatively dumpMorkValues() or dumpMorkColumns() will write only
 *    the columns or values dictionaries.
 *
 *    The Mork database can be written as vCards using dumpVcards().
 *
 *
 *    Example usage to load the address book and print it as vCards:
 *       morkLogfp = NULL;
 *       morkErrfp = stderr;
 *       morkDb *mork = parseMorkFile( "abook.mab" );
 *       FILE *vCardFp = fopen( "abook.vcf", "r" );
 *       dumpVcards( vCardFp, mork );
 *	 fclose( vCardFp );
 *       freeMorkDb( mork );
 *       
 *
 *    Author: David W. Stockton
 *    September 9, 2013
 *
 ----------------------------------------------------------------------------*/
#ifndef __ParseMork_h__
#define __ParseMork_h__

// Set these to NULL or where you want logging and debug output to print
extern FILE	*morkLogfp;
extern FILE	*morkErrfp;

extern const char MorkMagicHeader[];
extern const char MorkDictColumnMeta[];

// Indicates intity is being parsed
typedef enum {
	NPColumns,
	NPValues,
	NPRows,
} nowParsingType;

// Mork dictionary entry records (integer key, string value)
typedef struct {
	int	key;
	char	*value;
} morkDictEntry;
// A Mork dictionary structure
typedef struct {
	int		cnt;
	morkDictEntry	**entries;
} morkDict;

// Mork cell entry records (integer tuples, key and value)
typedef struct {
	int	key;
	int	value;
} morkCellEntry;
// A Mork cells structure
typedef struct {
	int		cnt;
	morkCellEntry	**entries;
} morkCells;
// A Mork row map structure (integer keys and cells values)
typedef struct {
	int		cnt;
	int		*keys;
	morkCells	**entries;
} morkRowMap;
// A Mork scope map structure (integer keys and row map values)
typedef struct {
	int		cnt;
	int		*keys;
	morkRowMap	**entries;
} rowScopeMap;
// A Mork table map structure (integer keys and row scope map values)
typedef struct {
	int		cnt;
	int		*keys;
	rowScopeMap	**entries;
} morkTableMap;
// A Mork database structure.
// Includes the column and value dictionaries.
// Includes the table scope map (integer keys and table map values)
// Includes internal status parameters for parsing
typedef struct {
	int		cnt;		// The number of keys & entries
	int		*keys;		// Malloc'd array of integers
	morkTableMap	**entries;	// Malloc'd array of table map pointers
	morkDict	*columns;	// Malloc'd column dictionary
	morkDict	*values;	// Malloc'd value dictionary
	nowParsingType	nowParsing;	// Parsing state
	int		nextAddValueId;
	int		defaultScope;
	morkCells	*activeCells;
} morkDb;

morkDb *parseMorkFile( const char *filename );
morkDb *parseMorkStream( FILE *ifp );
void freeMorkDb( morkDb *mork );
void dumpTableScopeMap( FILE *ofp, morkDb *mork );
void dumpMorkValues( FILE *ofp, morkDb *mork );
void dumpMorkColumns( FILE *ofp, morkDb *mork );
void dumpVcards( FILE *ofp, morkDb *mork );

#endif // __ParseMork_h__
