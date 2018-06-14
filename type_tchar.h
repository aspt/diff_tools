/** 04.12.2014 @file
*  
*/

#ifndef type_tchar_H_INCLUDED
#define type_tchar_H_INCLUDED

#if defined (_WIN32)
#   define HAVE_TCHAR 1
#   include <tchar.h>
#elif !defined (HAVE_TCHAR)
#   define HAVE_TCHAR 0

typedef char TCHAR;
#define _T(x) x

#define _fputts     fputs
#define _ftprintf   fprintf
#define _sntprintf  _snprintf
#define _stprintf   sprintf
#define _tasctime   asctime
#define _tcscat     strcat
#define _tcsclen    strlen
#define _tcscmp     strcmp
#define _tcscpy     strcpy
#define _tcsdup     _strdup
#define _tcsicmp    _stricmp
#define _tcslen     strlen
#define _tcsnccmp   strncmp
#define _tcsncpy    strncpy
#define _tcsrchr    strrchr
#define _tcsstr     strstr
#define _tcstod     strtod
#define _tcstoul    strtoul
#define _tfopen     fopen
#define _tmain      main
#define _totupper   toupper
#define _tprintf    printf
#define _tremove    remove
#define _ttoi       atoi
#define _vsntprintf _vsnprintf

#endif


#endif //type_tchar_H_INCLUDED
