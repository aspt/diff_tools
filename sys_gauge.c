/** 08.08.2004 @file
*  
*/

#include "sys_gauge.h"

#ifdef _MSC_VER
#   pragma warning (disable:4115)      // rpcasync.h(45) : warning C4115: '_RPC_ASYNC_STATE' : named type definition in parentheses
#endif
#include <windows.h>
#ifdef _MSC_VER
#   pragma warning (default:4115)
#   pragma comment(lib, "User32.lib")   // CharToOemBuff
#endif

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <assert.h>

/*
    Configuration defines:
*/ 
// Use timer for gauge redraw
#ifndef GAUGE_CFG_TIMER_UPDATE
#define GAUGE_CFG_TIMER_UPDATE  1
#endif
// Show "Time ETA" field
#ifndef GAUGE_CFG_SHOW_TIME
#define GAUGE_CFG_SHOW_TIME     1
#endif
// Show "Percent" field
#ifndef GAUGE_CFG_SHOW_PERCENT
#define GAUGE_CFG_SHOW_PERCENT  0
#endif
// Show progress indicator field
#ifndef GAUGE_CFG_SHOW_GAUGE
#define GAUGE_CFG_SHOW_GAUGE    1
#endif
// Show pop-up messages in magenta color
#ifndef GAUGE_CFG_USE_COLOR
#define GAUGE_CFG_USE_COLOR     1
#endif

/*
*   Multimedia timer stuff
*/
#if GAUGE_CFG_TIMER_UPDATE
#   include <mmsystem.h>
#   ifdef _MSC_VER
#       pragma comment(lib, "winmm.lib")
#   endif
#   define CRITICAL_ENTER    EnterCriticalSection(&This->cs)
#   define CRITICAL_LEAVE    LeaveCriticalSection(&This->cs)
#else
#   define CRITICAL_ENTER    
#   define CRITICAL_LEAVE    
#endif

// Max supported screen width
#define MAX_SCREEN_WIDTH 1024

#define MIN(x,y) ((x)<(y) ? (x):(y))
#define MAX(x,y) ((x)>(y) ? (x):(y))


/**
*   Gauge structure
*/
typedef struct
{
    // Application-defined appearance options
    unsigned int    status_width;
    unsigned int    gauge_width;

    // Message-related variables
    unsigned int    screen_message_len;
    clock_t         message_end_time;
    char            message[MAX_SCREEN_WIDTH + 1];

    // Initialization time
    clock_t         start_time;

    // Gauge buffer
    char            gauge_buf[MAX_SCREEN_WIDTH + 1];

    // Back-buffer: line-feed + mix of message and gauge
    char            back_buf[1 + MAX_SCREEN_WIDTH + 1];

    // Console window handle
    HANDLE          hconsole;

    // Flag to suppress gauge display
    int             is_hidden;

    int             cursor_pos;

#if GAUGE_CFG_TIMER_UPDATE
    CRITICAL_SECTION cs;
    MMRESULT        id_event;   // timer identifier
#endif

} gauge_t;

/**
*   Global gauge instance 
*/
static gauge_t This[1];

/**
*   Returns 1 if program output was redirected with '>' (only for application use)
*/
int GAUGE_is_stdout_redirected(void)
{
    DWORD dwFileType = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
    return dwFileType == FILE_TYPE_DISK;
}

/**
*   Returns current screen width (only for application use)
*/
int GAUGE_screen_width(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &csbi))
    {
        return csbi.dwSize.X;
    }
    else
    {
        return 80;
    }
}

/**
*   Transform time in seconds into string like "02m45s"
*/
void GAUGE_time2str(char * buf, double t)
{
    int     hr;
    int     min;
    double  sec;
    int     int_time;

    int_time = (int)floor(t / 60.);
    sec = t - int_time * 60;
    if (!int_time)
    {
        int fraction;
        fraction  = (int)floor(sec*100) - ((int)floor(sec))*100;
        sprintf(buf,"%02d.%02ds", (int)floor(sec), fraction);
        return;
    }
    t = int_time; 
    int_time = (int)floor(t / 60.);
    min = (int)floor(t - int_time * 60);
    if (!int_time)
    {
        sprintf(buf,"%02dm%02ds", min, (int)floor(sec));   // round sec down to avoid 60s :)
        return;
    }
    t = int_time; 
    hr  = (int)floor(t);

    sprintf(buf,"%02dh%02dm", hr, min);
}

/**
*   Get screen width & current cursor position.
*/
static int screen_width_pos(int * ppos)
{
    int w = 80, pos = 0;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(This->hconsole, &csbi))
    {
        w = csbi.dwSize.X;
        pos = csbi.dwCursorPosition.X; 
    }
    w = MIN(w, MAX_SCREEN_WIDTH);
    pos = MIN(pos, w - 1);
    if (ppos)
    {
        *ppos = pos;
    }
    // screen width reduced to avoid advancing to next line after printing last symbol
    return w - 1;
}

static int tchar2oem(const TCHAR * src, char * dst, size_t max)
{
    int i, len = 0;
    if (src)
    {
#ifdef _UNICODE
        char * p = GAUGE_unicode2ansi(src);
        len = MIN(strlen(p), max);
        strncpy(dst, p, len);
        //memcpy(dst, p, len);
        free(p);
#else
        len = MIN(strlen(src), max);
        strncpy(dst, src, len);
        dst[max-1] = '\0';
        CharToOem(dst, dst);
#endif
        for (i = 0; i < len; i++)
        {
            if (dst[i] == '\n')
            {
                dst[i] = ' ';
            }
        }
    }
    return len;
}

/**
*   Remove gauge from screen
*/
void GAUGE_hide( void )
{
    This->is_hidden = 1;
    GAUGE_puts(NULL, stderr);
}

/**
*   Paint gauge
*/
static void paint( void )
{
    unsigned int output_line_width, screen_w;
    int cursor_pos, i, mess_len;

    if (This->is_hidden)
    {
        return;
    }

    screen_w = screen_width_pos(&cursor_pos);
    if (cursor_pos != This->cursor_pos && cursor_pos)
    {
        // keep application screen output on a separate line
        memset(This->back_buf, ' ', screen_w+1);
        This->back_buf[screen_w] = '\n';
        //fwrite("\n", 1, 1, stderr);
        fwrite(This->back_buf + cursor_pos, screen_w+1-cursor_pos, 1, stderr);
    }

    if (This->screen_message_len && clock() > This->message_end_time)
    {
        // Remove expired message
        This->screen_message_len = 0;
    }
    mess_len = MIN(This->screen_message_len, screen_w);

    output_line_width = This->status_width + This->gauge_width;
#if GAUGE_CFG_SHOW_PERCENT
    output_line_width += 4;
#endif
#if GAUGE_CFG_SHOW_TIME
    output_line_width += (12 + 12);    
#endif
    output_line_width = MIN(screen_w, output_line_width);

    memset(This->back_buf, ' ', screen_w+1);
    This->back_buf[0] = '\r';
    memcpy(This->back_buf+1, This->gauge_buf, MIN(screen_w, output_line_width));
    if (This->screen_message_len)
    {
        memcpy(This->back_buf+1, This->message, mess_len);
    }
    i = 0;
#if GAUGE_CFG_USE_COLOR
    if (This->screen_message_len)
    {
        // Print message in magenta color
        SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE),FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        fwrite(This->back_buf, 1, 1 + mess_len, stderr);
        SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE),FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        i = 1 + mess_len;
    }
#endif
    fwrite(This->back_buf + i, 1, screen_w - i+1, stderr);
    screen_width_pos(&This->cursor_pos);
}

/**
*   Restore previously hidden gauge
*/
void GAUGE_show( void )
{
    CRITICAL_ENTER;
    This->is_hidden = 0;
    paint();
    CRITICAL_LEAVE;
}

/**
*   Draw gauge in the back buffer
*/
static void update_pos(double dbl_pos)
{
    char          * p;
    unsigned int    gauge_width;
    unsigned int    int_pos;

    // Limit input parameter to valid range
    dbl_pos = MAX(dbl_pos, 0.);
    dbl_pos = MIN(dbl_pos, 1.);

    // Calculate maximum width of the gauge
    gauge_width = screen_width_pos(NULL) - This->status_width;

    // Reduce maximum gauge width if percent or time indicators required
#if GAUGE_CFG_SHOW_PERCENT
    gauge_width -= 4;
#endif
#if GAUGE_CFG_SHOW_TIME
    gauge_width -= (12 + 12);    
#endif

    // Limit gauge to application specified limit
    gauge_width = MIN(gauge_width, This->gauge_width);

    // Calculate gauge position and sub-position 
    int_pos        = (unsigned int)(gauge_width*dbl_pos);

    // Set pointer to initial gauge position in internal buffer
    p = This->gauge_buf + This->status_width;

    // Draw first part of the gauge
    memset(p, '\xdb', int_pos);

    // Draw second part of the gauge
    memset(p + int_pos, '\xb0', gauge_width - int_pos);
    
    // Draw sub-position character
    if (int_pos < gauge_width)
    {
        p[int_pos] = "\xb0\xb1\xb2"[(int)floor((gauge_width*dbl_pos - int_pos)*3)];
    }

    p += gauge_width;
    
    // Print percent
#if GAUGE_CFG_SHOW_PERCENT
    p += sprintf(p, "%3d%%", (int)(dbl_pos * 100));
#endif

    // Print time
#if GAUGE_CFG_SHOW_TIME
    {
        char time[100], eta[100];
        double sec = (double)(clock() - This->start_time)/CLOCKS_PER_SEC;
        GAUGE_time2str(time, sec);
        GAUGE_time2str(eta,  dbl_pos == 0 ? 3600*9999 : sec * (1/dbl_pos - 1) );
        p += sprintf(p, " Time %.6s  ETA: %.6s", time, eta);
    }
#endif
}


#if GAUGE_CFG_TIMER_UPDATE
/**
*   Timer callback procedure
*/
#ifndef _WIN64
#define DWORD_PTR DWORD
#endif
void CALLBACK timer_proc(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
    (void)uTimerID, (void)uMsg, (void)dwUser, (void)dw1, (void)dw2;
    CRITICAL_ENTER;
    paint();
    CRITICAL_LEAVE;
}
#endif

/**
*   Constructor
*/
void GAUGE_init(
    unsigned int status_width,     //!< Status field width (0 - disabled)
    unsigned int gauge_width       //!< Maximum gauge width (adjusted according to screed width)
    )
{
    memset(This, 0, sizeof(*This));

    gauge_width = MIN(gauge_width, MAX_SCREEN_WIDTH);

    // Set application-defined parameters
    This->status_width = status_width;
    This->gauge_width  = gauge_width;

    This->hconsole = GetStdHandle(STD_ERROR_HANDLE);

#if GAUGE_CFG_TIMER_UPDATE
    InitializeCriticalSection(&This->cs);
    This->id_event = timeSetEvent(
        100,        //UINT uDelay,                
        100,        //UINT uResolution,           
        timer_proc, //LPTIMECALLBACK lpTimeProc,  
        0,          //DWORD dwUser,               
        TIME_PERIODIC | TIME_CALLBACK_FUNCTION  //UINT fuEvent
        );
#endif

    This->start_time     = clock();
}

/**
*   Destructor
*/
void GAUGE_close( void )
{
#if GAUGE_CFG_TIMER_UPDATE
    if (!This->is_hidden)
    {
        // Refresh position if gauge closed w/o hiding 
        CRITICAL_ENTER;
        paint();
        CRITICAL_LEAVE;
    }
    timeKillEvent(This->id_event);
    DeleteCriticalSection(&This->cs);
#endif
}

#ifdef _UNICODE
char * GAUGE_unicode2ansi(const TCHAR * s)
{
    int len, output_codepage;
    char * d;

    //SetConsoleOutputCP(CP_UTF8);
    output_codepage = GetConsoleOutputCP();  //CP_ACP
    len = WideCharToMultiByte(output_codepage, 0, s, -1, NULL, 0, NULL, NULL);
    d = malloc(len);
    if (d)
    {
        WideCharToMultiByte(output_codepage, 0, s, -1, d, len, NULL, NULL);
    }
    return d;
}
#endif

/**
*   Set status field
*/
void GAUGE_set_status(
    const TCHAR * status_text       //!< [IN] Text to display in the status field
    )
{
    CRITICAL_ENTER;
    memset(This->gauge_buf, ' ', This->status_width); // remove previous message
    tchar2oem(status_text, This->gauge_buf, This->status_width+1);
#if ! GAUGE_CFG_TIMER_UPDATE
    paint();
#endif
    CRITICAL_LEAVE;
}

/**
*   Set pop-up message over the gauge
*/
void GAUGE_set_message(
    const TCHAR * message_text,     //!< [IN] Text to display above the gauge
    double lifetime_sec           //!< Time (in seconds) to display the text
    )
{
    CRITICAL_ENTER;
    This->message_end_time = clock() + (clock_t)(lifetime_sec*CLOCKS_PER_SEC);
    This->screen_message_len = tchar2oem(message_text, This->message, MAX_SCREEN_WIDTH);
    paint();
    CRITICAL_LEAVE;
}

/**
*   Set position
*/
void GAUGE_set_pos(
    double dbl_pos                   //!< Gauge position in the range [0.0; 1.0] 
    )
{
    CRITICAL_ENTER;
    update_pos(dbl_pos);
#if ! GAUGE_CFG_TIMER_UPDATE
    paint();
#endif
    CRITICAL_LEAVE;
}


/**
*   Puts a string to the console
*   If string is NULL, clears the gauge.
*/
void GAUGE_puts(
    const TCHAR * s,
    FILE * f
    )
{
    int w = screen_width_pos(NULL);
    CRITICAL_ENTER;
    memset(This->back_buf, ' ', w+2);
    This->back_buf[0] = '\r';
    tchar2oem(s, This->back_buf+1, w);
    fwrite(This->back_buf, 1, w + 1 + !!s, f); // puts(NULL) don't feeds line
    fflush(f);
    paint();
    CRITICAL_LEAVE;
}


static int get_key_win32(void)
{
    INPUT_RECORD buf;
    unsigned long num_read;
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), 0); 
    if (WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE),0) != WAIT_OBJECT_0)
    {
        return 0;
    }
    if (! ReadConsoleInput(
        GetStdHandle(STD_INPUT_HANDLE), // input buffer handle
        &buf,                           // buffer to read into
        1,                              // size of read buffer
        &num_read) )                    // number of records read
    {
        return 0;
    }
    if (buf.EventType == KEY_EVENT && buf.Event.KeyEvent.bKeyDown)
    {
        return buf.Event.KeyEvent.wVirtualKeyCode;
    }
    return 0;
} 

int GAUGE_esc_pressed(void)
{
    int key = get_key_win32();
    return key == 27;
}

#ifdef sys_gauge_test
/******************************************************************************
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!                                                                       !!!!
!!!!                 !!!!!!!!  !!!!!!!!   !!!!!!!   !!!!!!!!               !!!!
!!!!                    !!     !!        !!            !!                  !!!!
!!!!                    !!     !!        !!            !!                  !!!!
!!!!                    !!     !!!!!!     !!!!!!!      !!                  !!!!
!!!!                    !!     !!               !!     !!                  !!!!
!!!!                    !!     !!               !!     !!                  !!!!
!!!!                    !!     !!!!!!!!   !!!!!!!      !!                  !!!!
!!!!                                                                       !!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
******************************************************************************/
#include "sys_gauge.h"
#include <math.h>
#include <assert.h>
#pragma warning (disable:4115)      // rpcasync.h(45) : warning C4115: '_RPC_ASYNC_STATE' : named type definition in parentheses
#include <windows.h>
#pragma warning (default:4115)
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <conio.h>


#define EPSILON 0.002
main()
{
    double pos = 0;
    double step = 0.001;
    GAUGE_init(
        10,     // Status field width
        1000    // Gauge width: deliberately big value produces full-screen gauge 
        );  

    GAUGE_set_status("Time12312313123:");
    GAUGE_set_pos(0);
    GAUGE_set_message("1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890", 1.5);
    Sleep(200);
    GAUGE_set_pos(0);
    Sleep(200);
    GAUGE_set_message("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 1.5);
    Sleep(200);
    GAUGE_set_pos(0);
    Sleep(200);
    GAUGE_set_message("yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy", 1.5);
    Sleep(200);
    GAUGE_set_pos(0);
    Sleep(200);

    while (!GAUGE_esc_pressed())
    {
        GAUGE_set_pos(pos);
        pos += step;
        if (pos > 1.05 || pos < -0.05) 
        {
            step = -step;
        }
//      if (fabs(pos - 0.5) < EPSILON) GAUGE_set_message("This is One Half! This is One Half!This is One Half!This is One Half!This is One Half!This is One Half!This is One Half!This is One Half!This is One Half!This is One Half! ", 1.5);
        if (fabs(pos - 0.5) < EPSILON) GAUGE_set_message("This is a message", 1.5);
        if (fabs(pos - 0.4) < EPSILON) GAUGE_puts("GAUGE_puts GAUGE_puts привет", stdout);
        Sleep(1);
    }
    GAUGE_hide();
}

// dmc sys_gauge.c -Dsys_gauge_test && sys_gauge.exe && del *.obj *.map sys_gauge.exe

#endif // sys_gauge_test
