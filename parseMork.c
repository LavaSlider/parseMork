/*-----------------------------------------------------------------------------
 *    ParseMork.c - Parser for Thunderbird address books (abook.mab) files
 *
 *    Developed after carful scrutiny of MorkParser.cpp written by
 *    Yuriy Soroka <ysoroka@scalingweb.com> and Annton Fedoruk
 *    <afedoruk@scalingweb.com> of ScalingWeb.com (that no longer seems
 *    to exist). That was downloaded from:
 *	   http://downloads.fyxm.net/Mork-Format-33313.html
 *
 *    Author: David W. Stockton
 *    September 9, 2013
 *
 *    After getting this generally working based on the referenced code,
 *    reverse engineering, etc., I found some documentation on the file
 *    format at: https://developer.mozilla.org/en-US/docs/Mork_Structure
 ----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parseMork.h"
#include "vCard.h"
//
// OK, in table scope 128 there seems to be two tables, 0 and 1
// in the case where I have an address book entry that I have
// entered then modified. Table 0 has the updated version, Table 1
// has the pre-edited version.
//
typedef int	bool;
#define	true	1
#define	false	0

#define	PARSE_GROUPS	1

const char MorkMagicHeader[] = "// <!-- <mdb:mork:z v=\"1.4\"/> -->";
const char MorkDictColumnMeta[] = "<(a=c)>";

FILE	*morkLogfp = NULL;
FILE	*morkErrfp = NULL;

#define	morkLog(...)	if( morkLogfp ) fprintf( morkLogfp, ##__VA_ARGS__ )
#define	morkErr(...)	if( morkErrfp ) fprintf( morkErrfp, ##__VA_ARGS__ )

// Internally used function declarations
int parseMorkDict( FILE *ifp, morkDb *mork );
  int parseMorkCell( FILE *ifp, morkDb *mork );
  void storeInMorkDict( morkDb *mork, morkDict *dict, int key, char *value );
int parseMorkTable( FILE *ifp, morkDb *mork );
int parseMorkRow( FILE *ifp, morkDb *mork, int a, int b );
void setCurrentRow( morkDb *mork, int TableScope, int TableId, int RowScope, int RowId );
morkCells *makeMorkCells();
void storeInMorkCell( morkCells *cells, int key, int value );
morkRowMap *makeMorkRowMap();
morkCells *getMorkCells( morkRowMap *morkRowMap, int rowId );
rowScopeMap *makeRowScopeMap();
morkRowMap *getMorkRowMap( rowScopeMap *rowScopeMap, int rowScope );
morkTableMap *makeMorkTableMap();
void freeMorkTableMap( morkTableMap *morkTableMap );
rowScopeMap *getRowScopeMapEntry( morkDb *m, morkTableMap *morkTableMap, int tableId );
void initializeTableScopeMap( morkDb *mork );
morkTableMap *getMorkTableMapEntry( morkDb *mork, int tableScope );
int parseMorkComment( FILE *ifp );
  void parseScopeId( const char *textId, int *Id, int *Scope );
  int parseMorkGroup( FILE *ifp );
  int parseMorkMeta( FILE *ifp, char c );
int parseMorkGroup( FILE *ifp );
// MorkDict interface functions
void initializeDict( morkDict *dict );
void dumpMorkDict( FILE *ofp, morkDict *dict );
char *getMorkDictValue( morkDict *dict, int key );
int getMorkDictKey( morkDict *dict, const char *value );
void freeMorkDict( morkDict *dict );
void freeMorkDictEntry( morkDictEntry *e );
char *getValue( morkDb *mork, int objectId );
char *getColumn( morkDb *morkDb, int objectId );
int getColumnId( morkDb *morkDb, const char *value );

// Read the characters through a function so that
// I can add position counting in the input stream
// and character buffering to use if groups are
// properly implemented (need to go back and read
// them again if not ~abort~'d).
int morkgetc( FILE *ifp ) {
	return fgetc( ifp );
}
int morkungetc( int c, FILE *ifp ) {
	return ungetc( c, ifp );
}

void freeMorkDb( morkDb *mork ) {
	freeMorkDict( mork->columns );
	mork->columns = NULL;
	freeMorkDict( mork->values );
	mork->values = NULL;
	mork->activeCells = NULL;
	if( mork->entries ) {
		int i;
		for( i = 0; i < mork->cnt; ++i ) {
			freeMorkTableMap( mork->entries[i] );
		}
	}
	free( mork->entries );
	mork->entries = NULL;
	free( mork->keys );
	mork->keys = NULL;
	mork->cnt = 0;
}

morkDb *parseMorkFile( const char *filename ) {
	morkDb	*mork;
	FILE	*ifp = fopen( filename, "r" );
	if( !ifp ) {
		morkErr( "error: unable to read file \"%s\"\n", filename );
		return 0;
	}
	mork = parseMorkStream( ifp );
	fclose( ifp );

	// Print some info about what we loaded
	//fprintf( morkLogfp, "\nDump of Mork Data\n" );
	//morkLog( "\nDump of Mork Data\n" );
	//morkLog( "----- columns table -----\n" );
	//if( morkLogfp ) dumpMorkDict( morkLogfp, x->columns );
	//morkLog( "----- values table -----\n" );
	//if( morkLogfp ) dumpMorkDict( morkLogfp, x->values );
	//morkLog( "----- mork structure -----\n" );
	//if( morkLogfp ) dumpTableScopeMap( morkLogfp, x );
	return mork;
}

morkDb *parseMorkStream( FILE *ifp ) {
	morkDb	*mork;
	bool	result	= true;	// Boolean result flag
	int	cur	= 0;	// The current character

	// Create and initialize the mork database object
	mork = (morkDb *) calloc( 1, sizeof(*mork) );
	if( !mork ) {
		morkErr( "***** error: unable to allocate mork database structure\n" );
		return (morkDb *) 0;
	}
	initializeTableScopeMap( mork );

	// It should start with the MorkMagicHeader
	char	magicHeaderBuffer[512];
	int	bufferPos = 0;
	do {
		magicHeaderBuffer[bufferPos++] = morkgetc( ifp );
	} while( bufferPos < strlen( MorkMagicHeader ) && !feof( ifp ) );
	magicHeaderBuffer[bufferPos] = '\0';
	if( strcmp( magicHeaderBuffer, MorkMagicHeader ) != 0 ) {
		morkErr( "***** error: Mork does not start with \"%s\"\n", magicHeaderBuffer );
		morkLog( "***** error: magic head mismatch \"%s\"\n",
			magicHeaderBuffer );
		free( mork );
		return (morkDb *) 0;
	}
	morkLog( "Correct \"%s\" header found\n", magicHeaderBuffer );

	cur = morkgetc( ifp );
	while( result && cur && !feof( ifp ) ) {
		if( !isspace( cur ) ) {
			switch( cur ) {
			case '<':	// Dict
				result = parseMorkDict( ifp, mork );
				if( !result ) morkErr( "***** error: parsing Mork dictionary\n" );
				break;
			case '/':	// Comment
				result = parseMorkComment( ifp );
				if( !result ) morkErr( "***** error: parsing Mork comment\n" );
				break;
			case '{':	// Table
				result = parseMorkTable( ifp, mork );
				if( !result ) morkErr( "***** error: parsing Mork table\n" );
				break;
			case '[':	// Row
				result = parseMorkRow( ifp, mork, 0, 0 );
				if( !result ) morkErr( "***** error: parsing Mork row\n" );
				break;
			case '@':	// Group
				result = parseMorkGroup( ifp );
				if( !result ) morkErr( "***** error: parsing Mork group\n" );
				break;
			default:
				morkErr( "format error: with '%c', looking for '<', '/', '{', '[', or '@'\n", cur );
				result = false;
				break;
			}
		}
		cur = morkgetc( ifp );
	}
	return mork;
}
// A Mork dictionary starts with '<'
int parseMorkDict( FILE *ifp, morkDb *m ) {
	char buf[10];
	int i;
	bool result = true;
	m->nowParsing = NPValues;

	morkLog( "Entering parseMorkDict()\n" );
	int cur = morkgetc( ifp );

	while( result && cur != '>' && cur && !feof(ifp) ) {
		if( !isspace( cur ) ) {
			switch( cur ) {
			case '<':
				buf[0] = cur;
				for( i = 1; i < strlen( MorkDictColumnMeta ); ++i ) {
					cur = morkgetc( ifp );
					buf[i] = cur;
				}
				buf[i] = '\0';
				if( strncmp( buf, MorkDictColumnMeta, strlen(MorkDictColumnMeta) ) == 0 ) {
					m->nowParsing = NPColumns;
				} else {
					// What do I do about the input
					// position? It has been advanced...
					morkErr( "error: thought we were getting a dictionary but found \"%s\" instead of \"%s\"\n", buf, MorkDictColumnMeta );
				}
				break;
			case '(':	// Cells
				result = parseMorkCell( ifp, m );
				break;
			case '/':	// Comment
				result = parseMorkComment( ifp );
				break;
			default:	// ???
				morkLog( "---- Ignored '%c' in parseMorkDict()\n", cur );
				break;
			}
		}
		cur = morkgetc( ifp );
	}
	morkLog( "-- Leaving parseMorkDict()\n" );
	return result;
}
// A Mork Cell starts with '('
int parseMorkCell( FILE *ifp, morkDb *m ) {
	bool result = true;
	bool columnIsObjectId = false;
	bool valueIsObjectId = false;
	bool bColumn = true;
	int corners = 0;

	morkLog( "  .  Entering parseMorkCell()" );

	// Column = Value
	char	column[512];
	int	colPos = 0;
	char	text[512];
	int	textPos = 0;

	// Process cell, start with column (bColumn == true)
	char cur = morkgetc( ifp );
	while( result && cur != ')' && cur && !feof(ifp) ) {
		switch( cur ) {
		case '^':	// Oids
			if( bColumn ) {
				corners++;
				if( 1 == corners ) {
					columnIsObjectId = true;
				} else if( 2 == corners ) {
					bColumn = false;
					valueIsObjectId = true;
				}
			} else {
				text[textPos++] = cur;
			}
			break;
		case '=':	// Transitioning from column to value
			if( bColumn ) {
				bColumn = false;
			} else {
				text[textPos++] = cur;
			}
			break;
		case '\\': {	// Skip the newline if there is one
				// otherwise it is an escaped character
			char nextChar = morkgetc( ifp );
			if( '\r' != nextChar && '\n' != nextChar ) {
				text[textPos++] = nextChar;
			} //else morkgetc( ifp );
			}
			break;
		case '$': {	// Hex escape, get next two chars
			char	hexChar[3];
			hexChar[0] = morkgetc( ifp );
			hexChar[1] = morkgetc( ifp );
			hexChar[2] = '\0';
			text[textPos++] = (char) strtol( hexChar, (char **) NULL, 16 );
			}
			break;
		default: // Just a char
			if( bColumn ) {
				if( !isspace( cur ) )
					column[colPos++] = cur;
			} else {
				text[textPos++] = cur;
			}
			break;
		}
		cur = morkgetc( ifp );
	}
	column[colPos] = '\0';
	text[textPos] = '\0';
	morkLog( " => %s%s%s%s\n", columnIsObjectId ? "^" : "", column, valueIsObjectId ? "^" : "=", text );

	// Apply column and text
	int columnId = strtol( column, (char **) NULL, 16 );

	// If the text field is not empty
	if( '\0' != text[0] ) {
		if( NPRows == m->nowParsing ) {
			// Rows
			if( valueIsObjectId  ) {
				int valueId = strtol( text, (char **) NULL, 16 );
				storeInMorkCell( m->activeCells, columnId,
						valueId );
			} else {
				m->nextAddValueId--;
				storeInMorkDict( m, m->values,
					m->nextAddValueId, text );
				storeInMorkCell( m->activeCells,
					columnId, m->nextAddValueId );
			}
		} else {
			// Dicts
			if( NPColumns == m->nowParsing ) {
				storeInMorkDict( m, m->columns, columnId, text);
			} else {
				storeInMorkDict( m, m->values, columnId, text );
			}
		}
	//} else {
	//	// If the text is empty I should probably be removing
	//	// any previously set cell for the column...
	//	// If nothing previously set, then just doing nothing
	//	// is fine.
	//	if( NPRows == m->nowParsing ) {
	//		// Rows
	//		int i;
	//		for( i = 0; i < m->activeCells->cnt; ++i ) {
	//			if( columnId <= m->activeCells->entries[i]->key ) {
	//				break;
	//			}
	//		}
	//		if( i < m->activeCells->cnt &&
	//		    m->activeCells->entries[i]->key == columnId ) {
	//			morkErr( "Changing %X from %X to empty\n",
	//				columnId, m->activeCells->entries[i]->value );
	//			morkLog( "Changing %X from %X to empty\n",
	//				columnId, m->activeCells->entries[i]->value );
	//		}
	//	} else {
	//		// Dicts
	//		if( NPColumns == m->nowParsing ) {
	//			morkErr( "Empty value for column dictionary entry %X? Was \"%s\"?\n",
	//				columnId, getColumn( m, columnId ) );
	//			morkLog( "Empty value for column dictionary entry %X? Was \"%s\"?\n",
	//				columnId, getColumn( m, columnId ) );
	//		} else {
	//			morkErr( "Empty value for values dictionary entry %X? Was \"%s\"?\n",
	//				columnId, getValue( m, columnId ) );
	//			morkLog( "Empty value for values dictionary entry %X? Was \"%s\"?\n",
	//				columnId, getValue( m, columnId ) );
	//		}
	//	}
	}
	return result;
}
int parseMorkComment( FILE *ifp ) {
	morkLog( "  Entering parseMorkComment()" );
	char	cmntBuf[512];
	int	cmntPos = 0;
	int cur = morkgetc( ifp );
	if( '/' != cur ) return false;

	while( cur && cur != '\r' && cur != '\n' ) {
		cmntBuf[cmntPos++] = cur;
		cur = morkgetc( ifp );
	}
	cmntBuf[cmntPos] = '\0';
	morkLog( " => \"%s\"\n", cmntBuf );
	return true;
}
// A Mork table starts with '{'
int parseMorkTable( FILE *ifp, morkDb *m ) {
	bool result = true;
	char	textId[512];
	int	textPos = 0;
	int id = 0, scope = 0;

	morkLog( "Entering parseMorkTable()\n" );

	char cur = morkgetc( ifp );

	// Get id
	while( cur && cur != '{' && cur != '[' && cur != '}' ) {
		if( !isspace( cur ) ) {
			textId[textPos++] = cur;
		}
		cur = morkgetc( ifp );
	}
	textId[textPos] = '\0';

	parseScopeId( textId, &id, &scope );

	// Parse the table
	while( result && cur && cur != '}' ) {
		if( !isspace( cur ) ) {
			switch( cur ) {
			case '{':
				result = parseMorkMeta( ifp, '}' );
				break;
			case '[':
				result = parseMorkRow( ifp, m, id, scope );
				break;
			case '-':
			case '+':
				break;
			default: {
				char	justId[512];
				int	justPos = 0;
				while( cur && !isspace( cur ) ) {
					justId[justPos++] = cur;
					cur = morkgetc( ifp );

					if( cur == '}' ) {
						morkLog( "-- Leaving parseMorkTable()\n" );
						return result;
					}
				}
				justId[justPos] = '\0';

				int justIdNum = 0, justScopeNum = 0;
				parseScopeId( justId, &justIdNum, &justScopeNum );

				setCurrentRow( m, scope, id, justScopeNum, justIdNum );
				}
				break;
			}
		}
		cur = morkgetc( ifp );
	}
	morkLog( "-- Leaving parseMorkTable()\n" );
	return result;
}
void setCurrentRow( morkDb *m, int TableScope, int TableId, int RowScope, int RowId ) {
	if( !RowScope )	  RowScope = m->defaultScope;
	if( !TableScope ) TableScope = m->defaultScope;

	morkLog( "  Setting active cells to Table ID %d in TableScope "
		 "%d and Row ID %d in Row Scope %d\n",
		 TableId, TableScope, RowId, RowScope );
	morkTableMap *tableMap = getMorkTableMapEntry( m, TableScope );
	rowScopeMap *rowScopeMap = getRowScopeMapEntry( m, tableMap, TableId );
	morkRowMap *rowMap = getMorkRowMap( rowScopeMap, RowScope );
	m->activeCells = getMorkCells( rowMap, RowId );
}
void parseScopeId( const char *textId, int *id, int *scope ) {
	morkLog( "  Entering parseScopeId( \"%s\" ) => ", textId );

	char *colonPos = strchr( textId, ':' );
	if( colonPos ) {
		// Terminate the string at the colon and move past
		*colonPos++ = '\0';

		if( *colonPos && '^' == *colonPos ) {
			// Delete '^'
			// --- but what does the '^' mean?
			++colonPos;
		}
		*scope = strtol( colonPos, (char **) NULL, 16 );
		morkLog( "scope %d for ", *scope );
	}
	*id = strtol( textId, (char **) NULL, 16 );
	morkLog( "id %d\n", *id );
}
//
// Groups should be processed as a block that can be ignored
// or included. Since including ignored blocks in the file seems
// silly we are ignoring groups themselves and just incorporating
// all the elements found in groups, outside of groups, etc.
//
// The syntax is:
//   @$${n{@		<-- to start the group (the 'n' is a group number)
//   @$$}n}@		<-- to end an accepted or included group (the 'n'
//			    matches the one given in the start.
//   @$$}~abort~n}@	<-- to end and throw away the group content
int parseMorkGroup( FILE *ifp ) {
#if	PARSE_GROUPS
	static	const char *startString = "$${.{";
	static	const char *endString = "$$}.}";
	static	const char *abortString = "$$}~abort~.}";
	char	headerBuf[64];
	int	headerBufPos = 0;
	char	*contentBuf = (char *) 0;
	int	contentBufSize = 0;
	int	contentBufPos = 0;
	char	footerBuf[64];
	int	footerBufPos = 0;
	int	cur;
	int	startGroupId = 0;
	int	endGroupId = -1;
	bool	isCorrupt;
	bool	groupAborted = false;

	morkLog( "Entering parseMorkGroup()\n" );

	// Load the group header
	morkLog( "  . Loading the group header: @" );
	cur = morkgetc( ifp );
	while( cur != '@' && cur && !feof(ifp) ) {
		if( headerBufPos < 63 )
			headerBuf[headerBufPos++] = cur;
		cur = morkgetc( ifp );
	}
	headerBuf[headerBufPos] = '\0';
	morkLog( "%s", headerBuf );
	if( headerBufPos > 4 && headerBuf[headerBufPos-1] == '{' &&
	    strncmp( headerBuf, startString, 3 ) == 0 &&
	    isxdigit( headerBuf[3] ) ) {
		startGroupId = strtol( &headerBuf[3], (char **) NULL, 16 );
		endGroupId = -startGroupId - 1;
		morkLog( "@\n    + Got the group header with group id of %d\n",
			startGroupId );
	} else {
		morkLog( "@\n    - Failed to recognize a group header\n" );
		// If it was not a valid header, then we should not be
		// considered as being in a group. If I just return
		// it will be the same as skipping it like a meta sequence!
		return true;	// Not really true but...
	}

	// Load the group contents
	bool notAtTheEnd = true;
	cur = morkgetc( ifp );
	while( notAtTheEnd && cur && !feof(ifp) ) {
		// Make sure there is buffer space for another character
		if( contentBufPos >= contentBufSize ) {
			contentBufSize += 512;
			contentBuf = realloc( contentBuf,
				contentBufSize * sizeof(*contentBuf) );
		}
		switch( cur ) {
		case '\\':	// Just blindly load the next character...
			contentBuf[contentBufPos++] = cur;
			contentBuf[contentBufPos++] = morkgetc( ifp );
			break;
		case '@':	// Could be the end...
			contentBuf[contentBufPos++] = cur;
			cur = morkgetc( ifp );
			contentBuf[contentBufPos++] = cur;
			if( cur == '$' ) {
				cur = morkgetc( ifp );
				contentBuf[contentBufPos++] = cur;
				if( cur == '$' ) {
					// I believe it is the end!
					contentBufPos -= 3; // Remove "@$$"
					if( morkungetc( '$', ifp ) == EOF )
						morkErr( "Failed to unget "
							 "the first '$'\n" );
					if( morkungetc( '$', ifp ) == EOF )
						morkErr( "Failed to unget "
							 "the second '$'\n" );
					if( morkungetc( ' ', ifp ) == EOF )
						morkErr( "Failed to unget "
							 "the staring '@'\n" );
					notAtTheEnd = false;
				}
			}
			break;
		default:
			contentBuf[contentBufPos++] = cur;
			break;
		}
		cur = morkgetc( ifp );
	}
	contentBuf[contentBufPos] = '\0';
	morkLog( "  . Loaded group contents:\n%s\n", contentBuf );

	// I could look for a group header here and recurse if nested
	// groups are allowed...

	// Load the group footer
	morkLog( "  . Loading the group footer: @" );
	isCorrupt = false;
	cur = morkgetc( ifp );
	while( cur != '@' && cur && !feof(ifp) ) {
		if( footerBufPos < 63 )
			footerBuf[footerBufPos++] = cur;
		cur = morkgetc( ifp );
	}
	footerBuf[footerBufPos] = '\0';
	morkLog( "%s", footerBuf );
	if( footerBufPos > 4 && footerBuf[footerBufPos-1] == '}' &&
	    strncmp( footerBuf, endString, 3 ) == 0 &&
	    isxdigit( footerBuf[3] ) ) {
		endGroupId = strtol( &footerBuf[3], (char **) NULL, 16 );
		morkLog( "@\n    + Got the group footer with group id of %d\n",
			endGroupId );
	} else if( footerBufPos > 10 && footerBuf[footerBufPos-1] == '}' &&
	    strncmp( footerBuf, abortString, 10 ) == 0 &&
	    isxdigit( footerBuf[10] ) ) {
		// This a strict abort match but really anything that
		// puts the group index anyplace other than at character
		// position 3 will result in an abort!
		groupAborted = true;
		endGroupId = strtol( &footerBuf[10], (char **) NULL, 16 );
		morkLog( "@\n    + Got the abort group footer with group id of %d\n",
			endGroupId );
	} else {
		isCorrupt = true;
		morkLog( "@\n    - Failed to recognize a group footer\n" );
	}

	// If the group was not aborted then push the content back on the
	// input to be read by the normal processing
	if( isCorrupt ) {
		morkErr( "Something was corrupt in the group footer?\n" );
		morkLog( "  . Something was wrong... trashing contents\n" );
	} else if( startGroupId != endGroupId ) {
		morkErr( "Something's corrupt because the start group ID "
			 "is %d and the end group ID is %d\n",
			 startGroupId, endGroupId );
		morkLog( "  . Start  and end Id's don't match... "
			 "trashing the contents\n" );
	} else if( !groupAborted ) {
		morkLog( "  . Found a good unaborted group... "
			 "pushing contents to be loaded\n" );
		while( --contentBufPos >= 0 ) {
			if( morkungetc( contentBuf[contentBufPos], ifp ) == EOF ) {
				morkErr( "***** error: failed ungetting "
					 "group %d content!\n", startGroupId );
				// Try and fail gracefully...
				//   I should try and read in all the
				//   ungotton characters so it will be just
				//   like an aborted group!
				contentBufPos = 0;
			}
		}
	} else {
		morkLog( "  . Found a good group but it was aborted... "
			 "trashing contents\n" );
	}
	// Free that allocated memory...
	if( contentBuf )	free( contentBuf );
	return true;
#else
	return parseMorkMeta( ifp, '@' );
#endif
}
int parseMorkMeta( FILE *ifp, char c ) {
	int cur = morkgetc( ifp );
	morkLog( "    - Ignoring meta \"" );
	while( cur != c && cur && !feof(ifp) ) {
		if( morkLogfp ) fputc( cur, morkLogfp );
		cur = morkgetc( ifp );
	}
	if( morkLogfp ) fputs( "\"\n", morkLogfp );
	return true;
}
int parseMorkRow( FILE *ifp, morkDb *m, int tableId, int tableScope ) {
	bool result = true;
	char	rowIdText[512];
	int	textPos = 0;
	int rowId = 0, rowScope = 0;

	morkLog( "  Entering parseMorkRow()\n" );
	m->nowParsing = NPRows;

	int cur = morkgetc( ifp );

	// Get the id text description
	while( cur != '(' && cur != '[' && cur != ']' && cur && !feof(ifp) ) {
		if( !isspace( cur ) ) {
			rowIdText[textPos++] = cur;
		}
		cur = morkgetc( ifp );
	}
	rowIdText[textPos] = '\0';

	// Figure out eh row scope and row ID and set it
	parseScopeId( rowIdText, &rowId, &rowScope );
	setCurrentRow( m, tableScope, tableId, rowScope, rowId );

	// Now parse the row itself
	while( result && cur != ']' && cur ) {
		if( !isspace( cur ) ) {
			switch( cur ) {
			case '(':
				result = parseMorkCell( ifp, m );
				if( !result ) {
					morkErr( "***** error: parsing Mork cell in parseMorkRow()\n" );
					morkLog( "***** error: parsing Mork cell in parseMorkRow()\n" );
				}
				break;
			case '[':
				result = parseMorkMeta( ifp, ']' );
				if( !result ) {
					morkErr( "***** error: parsing Mork meta in parseMorkRow()\n" );
					morkLog( "***** error: parsing Mork meta in parseMorkRow()\n" );
				}
				break;
			default:
				morkErr( "***** error: expected '(' or '[' not '%c' in parseMorkRow\n", cur );
				morkLog( "***** error: expected '(' or '[' not '%c' in parseMorkRow\n", cur );
				result = false;
				break;
			}
		}
		cur = morkgetc( ifp );
	}
	return result;
}

char *getValue( morkDb *mork, int objectId ) {
	return getMorkDictValue( mork->values, objectId );
}
char *getColumn( morkDb *morkDb, int objectId ) {
	return getMorkDictValue( morkDb->columns, objectId );
}
int getColumnId( morkDb *morkDb, const char *value ) {
	return getMorkDictKey( morkDb->columns, value );
}

// morkDictEntry procedures
morkDictEntry *makeMorkDictEntry( int key, char *value ) {
	morkDictEntry *e = malloc( sizeof(morkDictEntry) );
	e->key = key;
	e->value = strdup( value );
	return e;
}
void freeMorkDictEntry( morkDictEntry *e ) {
	if( e->value )	free( e->value );
	e->value = NULL;
	free( e );
}
void dumpMorkDictEntry( FILE *ofp, morkDictEntry *dictEntry ) {
	fprintf( ofp, "  %3d/%2X: \"%s\"\n", dictEntry->key, dictEntry->key, dictEntry->value );
}
// morkDict procedures
void dumpMorkValues( FILE *ofp, morkDb *mork ) {
	if( !mork ) {
		morkErr( "***** error: request to dump values from NULL Mork database\n" );
	} else	dumpMorkDict( ofp, mork->values );
}
void dumpMorkColumns( FILE *ofp, morkDb *mork ) {
	if( !mork ) {
		morkErr( "***** error: request to dump columns from NULL Mork database\n" );
	} else	dumpMorkDict( ofp, mork->columns );
}
void dumpMorkDict( FILE *ofp, morkDict *dict ) {
	int i;
	for( i = 0; i < dict->cnt; ++i ) {
		dumpMorkDictEntry( ofp, dict->entries[i] );
	}
}
void initializeDict( morkDict *dict ) {
	dict->cnt = 0;
	dict->entries = (morkDictEntry **) 0;
}
char *getMorkDictValue( morkDict *dict, int key ) {
	int i;
	for( i = 0; i < dict->cnt; ++i ) {
		if( dict->entries[i]->key == key )
			return dict->entries[i]->value;
	}
	return "";
}
int getMorkDictKey( morkDict *dict, const char *value ) {
	int i;
	for( i = 0; i < dict->cnt; ++i ) {
		if( strcmp( value, dict->entries[i]->value ) == 0 )
			return dict->entries[i]->key;
	}
	return 0;
}
void freeMorkDict( morkDict *dict ) {
	if( dict->entries ) {
		int i;
		for( i = 0; i < dict->cnt; ++i ) {
			freeMorkDictEntry( dict->entries[i] );
			dict->entries[i] = NULL;
		}
		free( dict->entries );
	}
	dict->entries = NULL;
	dict->cnt = 0;
}
void storeInMorkDict( morkDb *m, morkDict *dict, int key, char *value ) {
	int i, j;
	char *dictName = "unknown";
	if( dict == m->columns ) {
		dictName = "columns";
	} else if( dict == m->values ) {
		dictName = "values";
	}
	morkLog( "     Setting %s dictionary key %3d/%2X to \"%s\"\n", dictName, key, key, value );
	for( i = 0; i < dict->cnt; ++i ) {
		if( key <= dict->entries[i]->key ) {
			break;
		}
	}
	//morkLog( "   This will be at position %d of %d in the dictionary\n", i, dict->cnt );
	if( i >= dict->cnt || key != dict->entries[i]->key ) {
		++dict->cnt;
		dict->entries = realloc( dict->entries, dict->cnt * sizeof(*(dict->entries)) );
		for( j = dict->cnt - 1; j > i; --j ) {
			dict->entries[j] = dict->entries[j-1];
		}
	} else {
		morkLog( "     - Changing %3d/%2X from \"%s\" to \"%s\"\n", key, key, dict->entries[i]->value, value );
		free( dict->entries[i]->value );
	}
	//morkLog( "   Putting the entry at %d with the size now %d\n", i, dict->cnt );
	dict->entries[i] = makeMorkDictEntry( key, value );
}

// morkCellEntry procedures
morkCellEntry *makeMorkCellEntry( int key, int value ) {
	morkCellEntry *e = malloc( sizeof(morkCellEntry) );
	e->key = key;
	e->value = value;
	return e;
}
void freeMorkCellEntry( morkCellEntry *cellEntry ) {
	free( cellEntry );
}
void dumpMorkCellEntry( FILE *ofp, morkDb *mork, morkCellEntry cellEntry ) {
	fprintf( ofp, "                 \"%s\" = \"%s\" (%d/%X = %d/%X)\n",
		getColumn( mork, cellEntry.key ),
		getValue( mork, cellEntry.value ),
		cellEntry.key, cellEntry.key,
		cellEntry.value, cellEntry.value );
}
char *valueForColumnId( int columnId, morkCells *cells, morkDb *morkDb ) {
	int i;
	for( i = 0; i < cells->cnt; ++i ) {
		if( cells->entries[i]->key == columnId ) {
			return getValue( morkDb, cells->entries[i]->value );
		}
	}
	return (char *) 0;
}
// morkCells procedures
//
// http://www.imc.org/pdi/vcard-21.txt
#define	vCardLine(colId,fmt)	\
	value = valueForColumnId( colId, cells, morkDb ); \
	if( value ) fprintf( ofp, fmt, vCardEscapeString( escBuf, value, ESCBUFSIZE ) )
void writeMorkCellsAsVcard3_0( FILE *ofp, morkDb *morkDb, morkCells *cells ) {
	int LastNameCol = getColumnId( morkDb, "LastName" );
	int FirstNameCol = getColumnId( morkDb, "FirstName" );
	int FNcol = getColumnId( morkDb, "DisplayName" );
	int EMAILcol = getColumnId( morkDb, "PrimaryEmail" );
	//int EMAIL2col = getColumnId( morkDb, "SecondEmail" );
	int WorkPhoneCol = getColumnId( morkDb, "WorkPhone" );
	int FaxNumberCol = getColumnId( morkDb, "FaxNumber" );
	int HomePhoneCol = getColumnId( morkDb, "HomePhone" );
	int PagerNumberCol = getColumnId( morkDb, "PagerNumber" );
	int CellularNumberCol = getColumnId( morkDb, "CellularNumber" );
	int HomeAddressCol = getColumnId( morkDb, "HomeAddress" );
	int HomeAddress2Col = getColumnId( morkDb, "HomeAddress2" );
	int HomeCityCol = getColumnId( morkDb, "HomeCity" );
	int HomeStateCol = getColumnId( morkDb, "HomeState" );
	int HomeZipCodeCol = getColumnId( morkDb, "HomeZipCode" );
	int HomeCountryCol = getColumnId( morkDb, "HomeCountry" );
	int WorkAddressCol = getColumnId( morkDb, "WorkAddress" );
	int WorkAddress2Col = getColumnId( morkDb, "WorkAddress2" );
	int WorkCityCol = getColumnId( morkDb, "WorkCity" );
	int WorkStateCol = getColumnId( morkDb, "WorkState" );
	int WorkZipCodeCol = getColumnId( morkDb, "WorkZipCode" );
	int WorkCountryCol = getColumnId( morkDb, "WorkCountry" );
	int JobTitleCol = getColumnId( morkDb, "JobTitle" );
	//int DepartmentCol = getColumnId( morkDb, "Department" );
	int CompanyCol = getColumnId( morkDb, "Company" );
	int NotesCol = getColumnId( morkDb, "Notes" );
#define	ESCBUFSIZE	1024
	char	escBuf[ESCBUFSIZE];
	char	*email;
	char	*formattedName;
	char	*firstName;
	char	*lastName;
	char	*value;
	char	*value1, *value2, *value3, *value4, *value5;

	// If there is only one entry then don't write anything
	if( cells->cnt <= 1 )	return;

	// What is the minimum content to generate a vCard?
	// I have to do something because I am generating empty vCards!
	email = valueForColumnId( EMAILcol, cells, morkDb );
	formattedName = valueForColumnId( FNcol, cells, morkDb );
	firstName = valueForColumnId( FirstNameCol, cells, morkDb );
	lastName = valueForColumnId( LastNameCol, cells, morkDb );
	if( !email && !formattedName && !firstName && !lastName )
		return;

	fprintf( ofp, "BEGIN:VCARD\n" );
	fprintf( ofp, "VERSION:3.0\n" );
	//N:Gump;Forrest
	// name parts:
	//	; Family, Given, Middle, Prefix, Suffix.
	//	; Example:Public;John;Q.;Reverend Dr.;III, Esq.
	if( firstName || lastName ) {
		fprintf( ofp, "N:" );
		if( firstName ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				firstName, ESCBUFSIZE ) );
		if( lastName ) fprintf( ofp, ";%s", vCardEscapeString( escBuf,
				lastName, ESCBUFSIZE ) );
		fprintf( ofp, ";;;\n" );
	}
	//FN:Forrest Gump
	vCardLine( FNcol, "FN:%s\n" );
	vCardLine( EMAILcol, "EMAIL;type=INTERNET;type=PREF:%s\n" );
	//ORG:Bubba Gump Shrimp Co.
	vCardLine( CompanyCol, "ORG:%s" );
	//TITLE:Shrimp Man
	vCardLine( JobTitleCol, "TITLE:%s\n" );
	//PHOTO;GIF:http://www.example.com/dir_photos/my_photo.gif
	//TEL;WORK;VOICE:(111) 555-1212
	vCardLine( WorkPhoneCol, "TEL;type=WORK;type=VOICE:%s\n" );
	vCardLine( FaxNumberCol, "TEL;type=WORK;type=FAX:%s\n" );
	vCardLine( PagerNumberCol, "TEL;type=PAGER:%s\n" );
	vCardLine( CellularNumberCol, "TEL;type=CELL;type=VOICE:%s\n" );
	//TEL;HOME;VOICE:(404) 555-1212
	vCardLine( HomePhoneCol, "TEL;type=HOME;type=VOICE:%s\n" );
	//ADR;WORK:;;100 Waters Edge;Baytown;LA;30314;United States of America
	// addressparts	= 0*6(strnosemi ";") strnosemi
	//	; PO Box, Extended Addr, Street, Locality, Region, Postal Code, Country Name
	value1 = valueForColumnId( WorkAddressCol, cells, morkDb );
	value2 = valueForColumnId( WorkCityCol, cells, morkDb );
	value3 = valueForColumnId( WorkStateCol, cells, morkDb );
	value4 = valueForColumnId( WorkZipCodeCol, cells, morkDb );
	value5 = valueForColumnId( WorkCountryCol, cells, morkDb );
	if( value1 || value2 || value3 || value4 || value5 ) {
		fprintf( ofp, "ADR:type=WORK:;" );
		char *addr2 = valueForColumnId( WorkAddress2Col, cells, morkDb);
		if( addr2 && value1 ) {
			fprintf( ofp, "%s;%s;", value1, addr2 );
		} else if( addr2 ) {
			fprintf( ofp, ";%s;", addr2 );
		} else if( value1 ) {
			fprintf( ofp, ";%s;", value1 );
		} else {
			fprintf( ofp, ";;" );
		}
		if( value2 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value2, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value3 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value3, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value4 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value4, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value5 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value5, ESCBUFSIZE ) );
		fprintf( ofp, "\n" );
	}
	//LABEL;WORK;ENCODING=QUOTED-PRINTABLE:100 Waters Edge=0D=0ABaytown, LA 30314=0D=0AUnited States of America
	//ADR;HOME:;;42 Plantation St.;Baytown;LA;30314;United States of America
	value1 = valueForColumnId( HomeAddressCol, cells, morkDb );
	value2 = valueForColumnId( HomeCityCol, cells, morkDb );
	value3 = valueForColumnId( HomeStateCol, cells, morkDb );
	value4 = valueForColumnId( HomeZipCodeCol, cells, morkDb );
	value5 = valueForColumnId( HomeCountryCol, cells, morkDb );
	if( value1 || value2 || value3 || value4 || value5 ) {
		fprintf( ofp, "ADR:type=HOME:;" );
		char *addr2 = valueForColumnId( HomeAddress2Col, cells, morkDb);
		if( addr2 && value1 ) {
			fprintf( ofp, "%s;%s;", value1, addr2 );
		} else if( addr2 ) {
			fprintf( ofp, ";%s;", addr2 );
		} else if( value1 ) {
			fprintf( ofp, ";%s;", value1 );
		} else {
			fprintf( ofp, ";;" );
		}
		if( value2 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value2, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value3 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value3, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value4 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value4, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value5 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value5, ESCBUFSIZE ) );
		fprintf( ofp, "\n" );
	}
	//LABEL;HOME;ENCODING=QUOTED-PRINTABLE:42 Plantation St.=0D=0ABaytown, LA 30314=0D=0AUnited States of America
	vCardLine( NotesCol, "NOTE:%s\n" );
	//REV:20080424T195243Z
	fprintf( ofp, "END:VCARD\n" );
}
void writeMorkCellsAsVcard2_1( FILE *ofp, morkDb *morkDb, morkCells *cells ) {
	int LastNameCol = getColumnId( morkDb, "LastName" );
	int FirstNameCol = getColumnId( morkDb, "FirstName" );
	int FNcol = getColumnId( morkDb, "DisplayName" );
	int EMAILcol = getColumnId( morkDb, "PrimaryEmail" );
	//int EMAIL2col = getColumnId( morkDb, "SecondEmail" );
	int WorkPhoneCol = getColumnId( morkDb, "WorkPhone" );
	int FaxNumberCol = getColumnId( morkDb, "FaxNumber" );
	int HomePhoneCol = getColumnId( morkDb, "HomePhone" );
	int PagerNumberCol = getColumnId( morkDb, "PagerNumber" );
	int CellularNumberCol = getColumnId( morkDb, "CellularNumber" );
	int HomeAddressCol = getColumnId( morkDb, "HomeAddress" );
	int HomeAddress2Col = getColumnId( morkDb, "HomeAddress2" );
	int HomeCityCol = getColumnId( morkDb, "HomeCity" );
	int HomeStateCol = getColumnId( morkDb, "HomeState" );
	int HomeZipCodeCol = getColumnId( morkDb, "HomeZipCode" );
	int HomeCountryCol = getColumnId( morkDb, "HomeCountry" );
	int WorkAddressCol = getColumnId( morkDb, "WorkAddress" );
	int WorkAddress2Col = getColumnId( morkDb, "WorkAddress2" );
	int WorkCityCol = getColumnId( morkDb, "WorkCity" );
	int WorkStateCol = getColumnId( morkDb, "WorkState" );
	int WorkZipCodeCol = getColumnId( morkDb, "WorkZipCode" );
	int WorkCountryCol = getColumnId( morkDb, "WorkCountry" );
	int JobTitleCol = getColumnId( morkDb, "JobTitle" );
	//int DepartmentCol = getColumnId( morkDb, "Department" );
	int CompanyCol = getColumnId( morkDb, "Company" );
	int NotesCol = getColumnId( morkDb, "Notes" );
#define	ESCBUFSIZE	1024
	char	escBuf[ESCBUFSIZE];
	char	*email;
	char	*formattedName;
	char	*firstName;
	char	*lastName;
	char	*value;
	char	*value1, *value2, *value3, *value4, *value5;

	// If there is only one entry then don't write anything
	if( cells->cnt <= 1 )	return;

	// What is the minimum content to generate a vCard?
	// I have to do something because I am generating empty vCards!
	email = valueForColumnId( EMAILcol, cells, morkDb );
	formattedName = valueForColumnId( FNcol, cells, morkDb );
	firstName = valueForColumnId( FirstNameCol, cells, morkDb );
	lastName = valueForColumnId( LastNameCol, cells, morkDb );
	if( !email && !formattedName && !firstName && !lastName )
		return;

	fprintf( ofp, "BEGIN:VCARD\n" );
	fprintf( ofp, "VERSION:2.1\n" );
	//N:Gump;Forrest
	// name parts:
	//	; Family, Given, Middle, Prefix, Suffix.
	//	; Example:Public;John;Q.;Reverend Dr.;III, Esq.
	if( firstName || lastName ) {
		fprintf( ofp, "N:" );
		if( firstName ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				firstName, ESCBUFSIZE ) );
		if( lastName ) fprintf( ofp, ";%s", vCardEscapeString( escBuf,
				lastName, ESCBUFSIZE ) );
		fprintf( ofp, ";;;\n" );
	}
	//FN:Forrest Gump
	vCardLine( FNcol, "FN:%s\n" );
	//ORG:Bubba Gump Shrimp Co.
	vCardLine( CompanyCol, "ORG:%s" );
	//TITLE:Shrimp Man
	vCardLine( JobTitleCol, "TITLE:%s\n" );
	//PHOTO;GIF:http://www.example.com/dir_photos/my_photo.gif
	//TEL;WORK;VOICE:(111) 555-1212
	vCardLine( WorkPhoneCol, "TEL;WORK;VOICE:%s\n" );
	vCardLine( FaxNumberCol, "TEL;WORK;FAX:%s\n" );
	vCardLine( PagerNumberCol, "TEL;PAGER:%s\n" );
	vCardLine( CellularNumberCol, "TEL;CELL;VOICE:%s\n" );
	//TEL;HOME;VOICE:(404) 555-1212
	vCardLine( HomePhoneCol, "TEL;HOME;VOICE:%s\n" );
	//ADR;WORK:;;100 Waters Edge;Baytown;LA;30314;United States of America
	// addressparts	= 0*6(strnosemi ";") strnosemi
	//	; PO Box, Extended Addr, Street, Locality, Region, Postal Code, Country Name
	value1 = valueForColumnId( WorkAddressCol, cells, morkDb );
	value2 = valueForColumnId( WorkCityCol, cells, morkDb );
	value3 = valueForColumnId( WorkStateCol, cells, morkDb );
	value4 = valueForColumnId( WorkZipCodeCol, cells, morkDb );
	value5 = valueForColumnId( WorkCountryCol, cells, morkDb );
	if( value1 || value2 || value3 || value4 || value5 ) {
		fprintf( ofp, "ADR:WORK:;" );
		vCardLine( WorkAddress2Col, "%s" );
		fprintf( ofp, ";" );
		if( value1 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value1, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value2 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value2, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value3 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value3, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value4 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value4, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value5 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value5, ESCBUFSIZE ) );
		fprintf( ofp, "\n" );
	}
	//LABEL;WORK;ENCODING=QUOTED-PRINTABLE:100 Waters Edge=0D=0ABaytown, LA 30314=0D=0AUnited States of America
	//ADR;HOME:;;42 Plantation St.;Baytown;LA;30314;United States of America
	value1 = valueForColumnId( HomeAddressCol, cells, morkDb );
	value2 = valueForColumnId( HomeCityCol, cells, morkDb );
	value3 = valueForColumnId( HomeStateCol, cells, morkDb );
	value4 = valueForColumnId( HomeZipCodeCol, cells, morkDb );
	value5 = valueForColumnId( HomeCountryCol, cells, morkDb );
	if( value1 || value2 || value3 || value4 || value5 ) {
		fprintf( ofp, "ADR:HOME:;" );
		vCardLine( HomeAddress2Col, "%s" );
		fprintf( ofp, ";" );
		if( value1 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value1, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value2 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value2, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value3 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value3, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value4 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value4, ESCBUFSIZE ) );
		fprintf( ofp, ";" );
		if( value5 ) fprintf( ofp, "%s", vCardEscapeString( escBuf,
				value5, ESCBUFSIZE ) );
		fprintf( ofp, "\n" );
	}
	//LABEL;HOME;ENCODING=QUOTED-PRINTABLE:42 Plantation St.=0D=0ABaytown, LA 30314=0D=0AUnited States of America
	//EMAIL;PREF;INTERNET:forrestgump@example.com
	vCardLine( EMAILcol, "EMAIL;PREF;INTERNET:%s\n" );
	vCardLine( NotesCol, "NOTE:%s\n" );
	//REV:20080424T195243Z
	fprintf( ofp, "END:VCARD\n" );
}
void dumpMorkCells( FILE *ofp, morkDb *morkDb, morkCells *cells ) {
	int i;
	fprintf( ofp, "               Mork cells with %d entries\n",
		cells->cnt );
	for( i = 0; i < cells->cnt; ++i ) {
		dumpMorkCellEntry( ofp, morkDb, *(cells->entries[i]) );
	}
}
morkCells *makeMorkCells() {
	morkCells *mc = calloc( 1, sizeof(morkCells) );
	return mc;
}
void freeMorkCells( morkCells *cells ) {
	if( cells->entries ) {
		int i;
		for( i = 0; i < cells->cnt; ++i ) {
			freeMorkCellEntry( cells->entries[i] );
			cells->entries[i] = NULL;
		}
		free( cells->entries );
	}
	cells->entries = NULL;
	cells->cnt = 0;
}
void storeInMorkCell( morkCells *cells, int key, int value ) {
	int i, j;
	morkLog( "     Setting cell with key %3d/%2X to %d/%X\n", key, key, value, value );
	for( i = 0; i < cells->cnt; ++i ) {
		if( key <= cells->entries[i]->key ) {
			break;
		}
	}
	//morkLog( "   This will be at position %d of %d in the dictionary\n",
	//	i, cells->cnt );
	if( i >= cells->cnt || key != cells->entries[i]->key ) {
		++cells->cnt;
		cells->entries = realloc( cells->entries, cells->cnt * sizeof(*(cells->entries)) );
		for( j = cells->cnt - 1; j > i; --j ) {
			cells->entries[j] = cells->entries[j-1];
		}
	} else if( cells->entries[i]->value != value ) {
		morkLog( "     - Changing cell %3d/%2X from %d/%X to %d/%X\n",
			key, key, cells->entries[i]->value,
			cells->entries[i]->value, value, value );
	}
	//morkLog( "   Putting the entry at %d with the size now %d\n",
	//	i, cells->cnt );
	cells->entries[i] = makeMorkCellEntry( key, value );
}
// morkRowMap functions
void dumpMorkRowMap( FILE *ofp, morkDb *mork, morkRowMap *morkRowMap ) {
	int i;
	fprintf( ofp, "               Mork row map with %d entries\n",
		morkRowMap->cnt );
	for( i = 0; i < morkRowMap->cnt; ++i ) {
		fprintf( ofp, "               Row %3d:\n", morkRowMap->keys[i]);
		dumpMorkCells( ofp, mork, morkRowMap->entries[i] );
		fflush( ofp );
		writeMorkCellsAsVcard2_1( ofp, mork, morkRowMap->entries[i] );
	}
}
void freeMorkRowMap( morkRowMap *morkRowMap ) {
	if( morkRowMap->entries ) {
		int i;
		for( i = 0; i < morkRowMap->cnt; ++i ) {
			freeMorkCells( morkRowMap->entries[i] );
			morkRowMap->entries[i] = NULL;
		}
		free( morkRowMap->entries );
		free( morkRowMap->keys );
	}
	morkRowMap->entries = NULL;
	morkRowMap->keys = NULL;
	morkRowMap->cnt = 0;
}
void dumpMorkRowMapVcards( FILE *ofp, morkDb *mork, morkRowMap *morkRowMap ) {
	int i;
	for( i = 0; i < morkRowMap->cnt; ++i ) {
		writeMorkCellsAsVcard3_0( ofp, mork, morkRowMap->entries[i] );
	}
}
morkRowMap *makeMorkRowMap() {
	morkRowMap *mrm = calloc( 1, sizeof(morkRowMap) );
	return mrm;
}
// Gets the Mork Cells Entry for the rowId from the morkRowMap
// (will create an empty one if it does not exist).
morkCells *getMorkCells( morkRowMap *morkRowMap, int rowId ) {
	int	i, j;
	for( i = 0; i < morkRowMap->cnt; ++i ) {
		if( rowId <= morkRowMap->keys[i] ) {
			break;
		}
	}
	if( i >= morkRowMap->cnt || rowId != morkRowMap->keys[i] ) {
		++morkRowMap->cnt;
		morkRowMap->entries = realloc( morkRowMap->entries,
			morkRowMap->cnt * sizeof(*(morkRowMap->entries)) );
		morkRowMap->keys = realloc( morkRowMap->keys,
			morkRowMap->cnt * sizeof(*(morkRowMap->keys)) );
		for( j = morkRowMap->cnt - 1; j > i; --j ) {
			morkRowMap->entries[j] = morkRowMap->entries[j-1];
			morkRowMap->keys[j] = morkRowMap->keys[j-1];
		}
		morkRowMap->entries[i] = makeMorkCells();
		morkRowMap->keys[i] = rowId;
	}
	return morkRowMap->entries[i];
}
// rowScopeMap functions
void dumpRowScopeMap( FILE *ofp, morkDb *mork, rowScopeMap *rowScopeMap ) {
	int i;
	fprintf( ofp, "          Row scope map with %d entries\n",
		rowScopeMap->cnt );
	for( i = 0; i < rowScopeMap->cnt; ++i ) {
		fprintf( ofp, "          Row scope %3d:\n",
			rowScopeMap->keys[i] );
		dumpMorkRowMap( ofp, mork, rowScopeMap->entries[i] );
	}
}
void dumpRowScopeMapVcards( FILE *ofp, morkDb *mork, rowScopeMap *rowScopeMap ) {
	int i;
	for( i = 0; i < rowScopeMap->cnt; ++i ) {
		dumpMorkRowMapVcards( ofp, mork, rowScopeMap->entries[i] );
	}
}
rowScopeMap *makeRowScopeMap() {
	rowScopeMap *rsm = calloc( 1, sizeof(rowScopeMap) );
	return rsm;
}
void freeRowScopeMap( rowScopeMap *rowScopeMap ) {
	if( rowScopeMap->entries ) {
		int i;
		for( i = 0; i < rowScopeMap->cnt; ++i ) {
			freeMorkRowMap( rowScopeMap->entries[i] );
			rowScopeMap->entries[i] = NULL;
		}
		free( rowScopeMap->entries );
		free( rowScopeMap->keys );
	}
	rowScopeMap->entries = NULL;
	rowScopeMap->keys = NULL;
	rowScopeMap->cnt = 0;
}
// Gets the Mork Row Map Entry for the rowScope from the rowScopeMap
// (will create an empty one if it does not exist).
morkRowMap *getMorkRowMap( rowScopeMap *rowScopeMap, int rowScope ) {
	int	i, j;
	for( i = 0; i < rowScopeMap->cnt; ++i ) {
		if( rowScope <= rowScopeMap->keys[i] ) {
			break;
		}
	}
	if( i >= rowScopeMap->cnt || rowScope != rowScopeMap->keys[i] ) {
		++rowScopeMap->cnt;
		rowScopeMap->entries = realloc( rowScopeMap->entries,
			rowScopeMap->cnt * sizeof(*(rowScopeMap->entries)) );
		rowScopeMap->keys = realloc( rowScopeMap->keys,
			rowScopeMap->cnt * sizeof(*(rowScopeMap->keys)) );
		for( j = rowScopeMap->cnt - 1; j > i; --j ) {
			rowScopeMap->entries[j] = rowScopeMap->entries[j-1];
			rowScopeMap->keys[j] = rowScopeMap->keys[j-1];
		}
		rowScopeMap->entries[i] = makeMorkRowMap();
		rowScopeMap->keys[i] = rowScope;
	}
	return rowScopeMap->entries[i];
}
// morkTableMap functions
void dumpMorkTableMap( FILE *ofp, morkDb *mork, morkTableMap *morkTableMap ) {
	int	i;
	fprintf( ofp, "     Mork table map with %d entries\n",
		morkTableMap->cnt );
	for( i = 0; i < morkTableMap->cnt; ++i ) {
		fprintf( ofp, "     Table %3d:\n", morkTableMap->keys[i] );
		dumpRowScopeMap( ofp, mork, morkTableMap->entries[i] );
	}
}
void dumpMorkTableMapVcards( FILE *ofp, morkDb *mork, morkTableMap *morkTableMap ) {
	int	i;
	for( i = 0; i < morkTableMap->cnt; ++i ) {
		dumpRowScopeMapVcards( ofp, mork, morkTableMap->entries[i] );
	}
}
morkTableMap *makeMorkTableMap() {
	morkTableMap *mtm = calloc( 1, sizeof(morkTableMap) );
	return mtm;
}
void freeMorkTableMap( morkTableMap *morkTableMap ) {
	int i;
	if( morkTableMap && morkTableMap->entries ) {
		for( i = 0; i < morkTableMap->cnt; ++i ) {
			freeRowScopeMap( morkTableMap->entries[i] );
			morkTableMap->entries[i] = NULL;
		}
		free( morkTableMap->entries );
		free( morkTableMap->keys );
	}
	morkTableMap->entries = NULL;
	morkTableMap->keys = NULL;
	morkTableMap->cnt = 0;
}
// Gets the Row Scope Map Entry for the tableID from the morkTableMap
// (will create an empty one if it does not exist).
rowScopeMap *getRowScopeMapEntry( morkDb *m, morkTableMap *morkTableMap, int tableId ) {
	int	i, j;
	for( i = 0; i < morkTableMap->cnt; ++i ) {
		if( tableId <= morkTableMap->keys[i] ) {
			break;
		}
	}
	if( i >= morkTableMap->cnt || tableId != morkTableMap->keys[i] ) {
		++morkTableMap->cnt;
		morkTableMap->entries = realloc( morkTableMap->entries,
			morkTableMap->cnt * sizeof(*(morkTableMap->entries)) );
		morkTableMap->keys = realloc( morkTableMap->keys,
			morkTableMap->cnt * sizeof(*(morkTableMap->keys)) );
		for( j = morkTableMap->cnt - 1; j > i; --j ) {
			morkTableMap->entries[j] = morkTableMap->entries[j-1];
			morkTableMap->keys[j] = morkTableMap->keys[j-1];
		}
		morkTableMap->entries[i] = makeRowScopeMap();
		morkTableMap->keys[i] = tableId;
	}
	return morkTableMap->entries[i];
}
// morkDb procedures
void dumpTableScopeMap( FILE *ofp, morkDb *mork ) {
	int i;
	fprintf( ofp, "Table scope map with %d entries\n", mork->cnt );
	for( i = 0; i < mork->cnt; ++i ) {
		fprintf( ofp, "Table scope %3d:\n", mork->keys[i] );
		dumpMorkTableMap( ofp, mork, mork->entries[i] );
	}
}
void dumpVcards( FILE *ofp, morkDb *mork ) {
	int i;
	for( i = 0; i < mork->cnt; ++i ) {
		dumpMorkTableMapVcards( ofp, mork, mork->entries[i] );
	}
}
void initializeTableScopeMap( morkDb *mork ) {
	mork->cnt = 0;
	mork->keys = (int *) 0;
	mork->nowParsing = NPValues;
	mork->nextAddValueId = 0x7fffffff;
	mork->defaultScope = 0x80;
	mork->entries = (morkTableMap **) 0;
	mork->columns = (morkDict *) calloc( 1, sizeof(*mork->columns) );
	mork->values = (morkDict *) calloc( 1, sizeof(*mork->values) );
}
// Gets the Mork Table Map Entry for the tableScope from the morkDb
// (will create an empty one if it does not exist).
morkTableMap *getMorkTableMapEntry( morkDb *mork, int tableScope ) {
	int	i, j;
	for( i = 0; i < mork->cnt; ++i ) {
		if( tableScope <= mork->keys[i] ) {
			break;
		}
	}
	if( i >= mork->cnt || tableScope != mork->keys[i] ) {
		++mork->cnt;
		mork->entries = realloc( mork->entries, mork->cnt * sizeof(*(mork->entries)) );
		mork->keys = realloc( mork->keys, mork->cnt * sizeof(*(mork->keys)) );
		for( j = mork->cnt - 1; j > i; --j ) {
			mork->entries[j] = mork->entries[j-1];
			mork->keys[j] = mork->keys[j-1];
		}
		mork->entries[i] = makeMorkTableMap();
		mork->keys[i] = tableScope;
	}
	return mork->entries[i];
}
