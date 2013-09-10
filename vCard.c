#include <string.h>
#include <stdio.h>
char *vCardEscapeString( char *dst, const char *src, size_t n ) {
	if( !dst ) return dst;
	if( !src ) {
		*dst = '\0';
		return dst;
	}
	size_t i = 0;
	char *dp = dst;
	while( i < n && *src ) {
		switch( *src ) {
		// Characters to encode: !"#$@[\]^`{|}~
		case '\r':
			//*dp++ = '='; *dp++ = '0'; *dp++ = 'D';
			//i += 3;
			*dp++ = '\\';
			*dp++ = 'r';
			i += 2;
			break;
		case '\n':
			//*dp++ = '='; *dp++ = '0'; *dp++ = 'A';
			//i += 3;
			*dp++ = '\\';
			*dp++ = 'n';
			i += 2;
			break;
		case ';':
		case ',':
			*dp++ = '\\';
			*dp++ = *src;
			i += 2;
			break;
		default:
			*dp++ = *src;
			++i;
			break;
		}
		++src;
	}
	while( i++ < n )	*dp++ = '\0';
	return dst;
}
