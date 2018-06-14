/** 08.08.2004 @file
*   Console-mode progress indicator
*   Example:
*
*   GAUGE_init(
*       10,     // Status field width
*       1000    // Gauge width: deliberately big value produces full-screen gauge
*       );  
*   GAUGE_set_status("128 kbps");
*   GAUGE_set_pos((double)pos/size);
*   GAUGE_set_message("Currently playing myfile.mp3", 2.0);
*  
*/

#ifndef sys_gauge_H_INCLUDED
#define sys_gauge_H_INCLUDED

#include <stdio.h>
#include "type_tchar.h"

#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus

/**
*   Initialize module.
*   Does not display gauge, just initialize.
*/
void GAUGE_init(
    unsigned int status_width,       //!< Status field width (0 - disabled)
    unsigned int gauge_width         //!< Gauge width (limited to screen width)
    );

/**
*   Release resources, allocated by GAUGE_Init()
*   Does not hide gauge
*/
void GAUGE_close( void );

/**
*   Remove gauge from screen
*/
void GAUGE_hide( void );

/**
*   Show gauge, if it was hidden with GAUGE_Hide()
*/
void GAUGE_show( void );

/**
*   Set gauge status field text (clipped to uiStatusWidth)
*/
void GAUGE_set_status(
    const TCHAR * status_text        //!< [IN] Text to display in the status field
    );

/**
*   Set pop-up text above the gauge for specified time interval
*/
void GAUGE_set_message(
    const TCHAR * message_text,      //!< [IN] Text to display above the gauge
    double       life_time_sec        //!< Time (in seconds) to display the text
    );

/**
*   Set gauge position
*/
void GAUGE_set_pos(
    double       dbl_pos             //!< Gauge position in the range [0.0; 1.0] 
    );

/**
*   Returns console screen width.
*/
int GAUGE_screen_width( void );

/**
*   Returns 1 if program output was redirected with '>' 
*/
int GAUGE_is_stdout_redirected( void );

/**
*   Transform time in seconds into string like "02m45s"
*/
void GAUGE_time2str(char * buf, double t);

/**
*   Puts a string to the console
*/
void GAUGE_puts(
    const TCHAR * s,                //!< String 
    FILE * f                        //!< stdout or stderr
    );

int GAUGE_printf(const TCHAR * format, ...);


int GAUGE_esc_pressed(void);

#ifdef _UNICODE
/**
*   Allocates copy of given string, converting to current codepage
*/
char * GAUGE_unicode2ansi(const TCHAR * s);
#endif //_UNICODE

char * GAUGE_unicode2cp(const TCHAR * s, int codepage);
TCHAR * GAUGE_cp2unicode(const char * s, int codepage);


#ifdef __cplusplus
}
#endif //__cplusplus

#endif //sys_gauge_H_INCLUDED
