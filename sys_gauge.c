/** 08.08.2004 @file
*  
*/

#include "sys_gauge.h"

#define PORTABLE_GAUGE 1

#if PORTABLE_GAUGE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void GAUGE_init(unsigned int status_width, unsigned int gauge_width) {}
void GAUGE_close() {}
void GAUGE_hide() {}
void GAUGE_show() {}
void GAUGE_set_status(const TCHAR * status_text) {}
void GAUGE_set_message(const TCHAR * message_text, double life_time_sec){}
void GAUGE_set_pos(double dbl_pos) {}
int GAUGE_screen_width(void)  {return 80;}
int GAUGE_is_stdout_redirected(void) {return 0;}
void GAUGE_time2str(char * buf, double t) {}
void GAUGE_puts(const TCHAR * s, FILE * f)
{
     _fputts(s, f); 
}
int GAUGE_printf(const TCHAR * format, ...)
{
    int                         chars_printed;
    TCHAR                       buf[2048];
    va_list                     va;
    va_start(va, format);
#ifdef _WIN32
    chars_printed = _vsntprintf(buf, 2048, format, va);
#else
    chars_printed = vsnprintf(buf, 2048, format, va);
#endif
    // todo: loss linefeed
    GAUGE_puts(buf, stdout);
    return chars_printed;
}
int GAUGE_esc_pressed(void) {return 0;}
char * GAUGE_unicode2ansi(const TCHAR * s) {return NULL;}
char * GAUGE_unicode2cp(const TCHAR * s, int codepage) {return NULL;}
TCHAR * GAUGE_cp2unicode(const char * s, int codepage) {return NULL;}


#else

#ifdef _WIN32
#ifdef _MSC_VER
#   pragma warning (push)
#   pragma warning (disable:4115)      // rpcasync.h(45) : warning C4115: '_RPC_ASYNC_STATE' : named type definition in parentheses
#endif
#include <windows.h>
#if defined _MSC_VER || defined __DMC__
#   pragma warning (pop)
#   pragma comment(lib, "User32.lib")   // CharToOemBuff
#   pragma comment(lib, "Winmm.lib")    // timeSetEvent
#endif
#else
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#define CURSOR_MOVE(y,x) printf("\033[%i;%iH", y, x) // Move cursor to x,y
#define FOREGROUND_COLOR(x) printf("\033[3%im", x) // Set foreground color
// Color identifiers
#define COLOR_BLACK   0
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE 7

typedef struct {int X,Y;} COORD; 

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <stdarg.h>

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
#define GAUGE_CFG_USE_COLOR     0
#endif

/*
*   Multimedia timer stuff
*/
#if GAUGE_CFG_TIMER_UPDATE
#   include <mmsystem.h>
#   ifdef _MSC_VER
#       pragma comment(lib, "winmm.lib")
#   endif
#   define CRITICAL_ENTER    EnterCriticalSection(&h->cs)
#   define CRITICAL_LEAVE    LeaveCriticalSection(&h->cs)
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

    // Flag to suppress gauge display
    int             is_hidden;

#ifdef _WIN32
    // Console window handle
    HANDLE          hconsole;
#endif

    COORD           cursor_pos;

#if GAUGE_CFG_TIMER_UPDATE
    CRITICAL_SECTION cs;
    MMRESULT        id_event;   // timer identifier
#endif

} gauge_t;

/**
*   Global gauge instance 
*/
static gauge_t h[1];

/**
*   Returns 1 if program output was redirected with '>' (only for application use)
*/
int GAUGE_is_stdout_redirected(void)
{
#ifdef _WIN32
    DWORD dwFileType = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
    return dwFileType == FILE_TYPE_DISK;
#else
    //return !isatty(fileno(stdout));
    return !isatty(STDOUT_FILENO);
#endif
}

/**
*   Returns current screen width (only for application use)
*/
int GAUGE_screen_width(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &csbi))
    {
        return csbi.dwSize.X;
    }
    else
    {
        return 80;
    }
#else
    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
        return 80;
    return ws.ws_col;
#endif
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
static int screen_width_pos(COORD * ppos)
{
    int w = 80;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(h->hconsole, &csbi))
    {
        w = csbi.dwSize.X;
        if (ppos)
        {
            *ppos = csbi.dwCursorPosition;
        }
    }
#else
    int i, r = 0;
    int row = 0;
    char buf[10] = {0,};
    char *cmd = "\033[6n";

    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1)
    {
        w = ws.ws_col;
    }

    if (ppos)
    {
        write(STDOUT_FILENO, cmd, sizeof(cmd));
        r = read(STDIN_FILENO, buf, sizeof(buf));

        for (i = 0; i < r; ++i) {
            if (buf[i] == 27 || buf[i] == '[') {
                continue;
            }

            if (buf[i] >= '0' && buf[i] <= '9') {
                row = (row * 10) + (buf[i] - '0');
            }

            if (buf[i] == ';' || buf[i] == 'R' || buf[i] == 0) {
                break;
            }
        }
        ppos->X = row;
        ppos->Y = 0;
    }

#endif
    w = MIN(w, MAX_SCREEN_WIDTH);
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
#ifdef _WIN32
        CharToOem(dst, dst);
#endif
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
    h->is_hidden = 1;
    GAUGE_puts(NULL, stderr);
}

/**
*   Paint gauge
*/
static void paint( void )
{
    unsigned int output_line_width, screen_w;
    int i, mess_len;
    COORD cursor_pos = {0,0};

    if (h->is_hidden)
    {
        return;
    }

    screen_w = screen_width_pos(&cursor_pos);
    if (cursor_pos.X != h->cursor_pos.X && cursor_pos.X)
    {
        // keep application screen output on a separate line
        memset(h->back_buf, ' ', screen_w+1);
        h->back_buf[screen_w] = '\n';
        fwrite(h->back_buf + cursor_pos.X, screen_w+1-cursor_pos.X, 1, stderr);
    }

    if (h->screen_message_len && clock() > h->message_end_time)
    {
        // Remove expired message
        h->screen_message_len = 0;
    }
    mess_len = MIN(h->screen_message_len, screen_w);

    output_line_width = h->status_width + h->gauge_width;
#if GAUGE_CFG_SHOW_PERCENT
    output_line_width += 4;
#endif
#if GAUGE_CFG_SHOW_TIME
    output_line_width += (12 + 12);    
#endif
    output_line_width = MIN(screen_w, output_line_width);

    memset(h->back_buf, ' ', screen_w+1);

#if 0
    memcpy(h->back_buf, h->gauge_buf, MIN(screen_w, output_line_width));
    if (h->screen_message_len)
    {
        memcpy(h->back_buf, h->message, mess_len);
    }

    i = 0;
#if GAUGE_CFG_USE_COLOR
    if (h->screen_message_len)
    {
        // Print message in magenta color
        SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE),FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        fwrite(h->back_buf, 1, mess_len, stderr);
        SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE),FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        i = mess_len;
    }
#endif
    cursor_pos.X = 0;
    SetConsoleCursorPosition(h->hconsole, cursor_pos);
    fwrite(h->back_buf + i, screen_w - i, 1, stderr);
    screen_width_pos(&h->cursor_pos);

#else
    h->back_buf[0] = '\r';
    memcpy(h->back_buf+1, h->gauge_buf, MIN(screen_w, output_line_width));
    if (h->screen_message_len)
    {
        memcpy(h->back_buf+1, h->message, mess_len);
    }

    i = 0;
#if GAUGE_CFG_USE_COLOR
    if (h->screen_message_len)
    {
        // Print message in magenta color
#ifdef _WIN32
        SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE),FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
#else
        FOREGROUND_COLOR(COLOR_MAGENTA);
#endif

        fwrite(h->back_buf, 1, 1 + mess_len, stderr);
#ifdef _WIN32
        SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE),FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#else
        FOREGROUND_COLOR(COLOR_WHITE);
#endif
        i = 1 + mess_len;
    }
#endif
    cursor_pos.X = 0;
#ifdef _WIN32
    SetConsoleCursorPosition(h->hconsole, cursor_pos);
#else
    CURSOR_MOVE(cursor_pos.Y, cursor_pos.X);
#endif
    fwrite(h->back_buf + i, screen_w - i+1, 1, stderr);
    screen_width_pos(&h->cursor_pos);
#endif
}

/**
*   Restore previously hidden gauge
*/
void GAUGE_show( void )
{
    CRITICAL_ENTER;
    h->is_hidden = 0;
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
    gauge_width = screen_width_pos(NULL) - h->status_width;

    // Reduce maximum gauge width if percent or time indicators required
#if GAUGE_CFG_SHOW_PERCENT
    gauge_width -= 4;
#endif
#if GAUGE_CFG_SHOW_TIME
    gauge_width -= (12 + 12 + 1);    
#endif

    // Limit gauge to application specified limit
    gauge_width = MIN(gauge_width, h->gauge_width);

    // Calculate gauge position and sub-position 
    int_pos        = (unsigned int)(gauge_width*dbl_pos);

    // Set pointer to initial gauge position in internal buffer
    p = h->gauge_buf + h->status_width;

#if 1
    // Draw first part of the gauge
    memset(p, '=', int_pos);

    // Draw second part of the gauge
    memset(p + int_pos, '.', gauge_width - int_pos);
    
    // Draw sub-position character
    if (int_pos < gauge_width)
    {
        p[int_pos] = "\\|/"[(int)floor((gauge_width*dbl_pos - int_pos)*3)];
    }
#else
    // Draw first part of the gauge
    memset(p, '\xdb', int_pos);

    // Draw second part of the gauge
    memset(p + int_pos, '\xb0', gauge_width - int_pos);
    
    // Draw sub-position character
    if (int_pos < gauge_width)
    {
        p[int_pos] = "\xb0\xb1\xb2"[(int)floor((gauge_width*dbl_pos - int_pos)*3)];
    }
#endif

    p += gauge_width;
    
    // Print percent
#if GAUGE_CFG_SHOW_PERCENT
    p += sprintf(p, "%3d%%", (int)(dbl_pos * 100));
#endif

    // Print time
#if GAUGE_CFG_SHOW_TIME
    {
        char time[100], eta[100];
        double sec = (double)(clock() - h->start_time)/CLOCKS_PER_SEC;
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
    memset(h, 0, sizeof(*h));

    gauge_width = MIN(gauge_width, MAX_SCREEN_WIDTH);

    // Set application-defined parameters
    h->status_width = status_width;
    h->gauge_width  = gauge_width;

#ifdef _WIN32
    h->hconsole = GetStdHandle(STD_ERROR_HANDLE);
#endif
#if GAUGE_CFG_TIMER_UPDATE
    InitializeCriticalSection(&h->cs);
    h->id_event = timeSetEvent(
        100,        //UINT uDelay,                
        100,        //UINT uResolution,           
        timer_proc, //LPTIMECALLBACK lpTimeProc,  
        0,          //DWORD dwUser,               
        TIME_PERIODIC | TIME_CALLBACK_FUNCTION  //UINT fuEvent
        );
#endif

    h->start_time     = clock();
}

/**
*   Destructor
*/
void GAUGE_close( void )
{
#if GAUGE_CFG_TIMER_UPDATE
    if (!h->is_hidden)
    {
        // Refresh position if gauge closed w/o hiding 
        CRITICAL_ENTER;
        paint();
        CRITICAL_LEAVE;
    }
    timeKillEvent(h->id_event);
    DeleteCriticalSection(&h->cs);
#endif
}

#ifdef _UNICODE
char * GAUGE_unicode2cp(const TCHAR * s, int codepage)
{
    int len = WideCharToMultiByte(codepage, 0, s, -1, NULL, 0, NULL, NULL);
    char * d = malloc(len);
    if (d)
    {
        WideCharToMultiByte(codepage, 0, s, -1, d, len, NULL, NULL);
    }
    return d;
}
TCHAR * GAUGE_cp2unicode(const char * s, int codepage)
{
    int len = MultiByteToWideChar(codepage, 0, s, -1, NULL, 0);
    TCHAR * d = (TCHAR*)malloc(len*sizeof(TCHAR));
    if (d)
    {
        MultiByteToWideChar(codepage, 0, s, -1, d, len);
    }
    return d;
}

char * GAUGE_unicode2ansi(const TCHAR * s)
{
    return GAUGE_unicode2cp(s, GetConsoleOutputCP());
//    int len, output_codepage;
//    char * d;
//
//    //SetConsoleOutputCP(CP_UTF8);
//    output_codepage = GetConsoleOutputCP();  //CP_ACP
//    len = WideCharToMultiByte(output_codepage, 0, s, -1, NULL, 0, NULL, NULL);
//    d = malloc(len);
//    if (d)
//    {
//        WideCharToMultiByte(output_codepage, 0, s, -1, d, len, NULL, NULL);
//    }
//    return d;
}

#else

static char* my_strdup (const char * s)
{
    char * d = malloc(strlen(s) + 1);
    if (d) strcpy(d, s);
    return d;
}

char * GAUGE_unicode2cp(const char * s, int codepage)
{
    return my_strdup(s);
}
char * GAUGE_cp2unicode2cp(const char * s, int codepage)
{
    return my_strdup(s);
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
    memset(h->gauge_buf, ' ', h->status_width); // remove previous message
    tchar2oem(status_text, h->gauge_buf, h->status_width+1);
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
    h->message_end_time = clock() + (clock_t)(lifetime_sec*CLOCKS_PER_SEC);
    h->screen_message_len = tchar2oem(message_text, h->message, MAX_SCREEN_WIDTH);
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
    int len = s ? _tcslen(s) : 0;
    CRITICAL_ENTER;
    memset(h->back_buf, ' ', w+2);
    h->back_buf[0] = '\r';
    tchar2oem(s, h->back_buf+1, w);
    if (len && s[len-1] == _T('\n'))
    {
        h->back_buf[MIN(w, len) - 1] = '\n';
    }
    fwrite(h->back_buf, 1, w + 1 + !!s, f); // puts(NULL) don't feeds line
    fflush(f);
    paint();
    CRITICAL_LEAVE;
}

int GAUGE_printf(const TCHAR * format, ...)
{
    int                         chars_printed;
    TCHAR                       buf[2048];
    va_list                     va;
    va_start(va, format);
#ifdef _WIN32
    chars_printed = _vsntprintf(buf, 2048, format, va);
#else
    chars_printed = vsnprintf(buf, 2048, format, va);
#endif
// todo: loss linefeed
    GAUGE_puts(buf, stdout);
    return chars_printed;
}


static int get_key_win32(void)
{
#ifdef _WIN32
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
#else
    int character = 0;
    struct termios orig_term_attr;
    struct termios new_term_attr;

    /* set the terminal to raw mode */
    tcgetattr(fileno(stdin), &orig_term_attr);
    memcpy(&new_term_attr, &orig_term_attr, sizeof(struct termios));
    new_term_attr.c_lflag &= ~(ECHO|ICANON);
    new_term_attr.c_cc[VTIME] = 0;
    new_term_attr.c_cc[VMIN] = 0;
    tcsetattr(fileno(stdin), TCSANOW, &new_term_attr);

    ioctl( fileno(stdin), FIONREAD, &t )
    if (t > 0)
    {
        /* read a character from the stdin stream without blocking */
        /*   returns EOF (-1) if no character is available */
        character = fgetc(stdin);
    }

    /* restore the original terminal attributes */
    tcsetattr(fileno(stdin), TCSANOW, &orig_term_attr);

    return character;
#endif
} 

int GAUGE_esc_pressed(void)
{
    int key = get_key_win32();
    return key == 27;
    return 0;
}

#endif // PORTABLE_GAUGE

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
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
#pragma warning (disable:4115)      // rpcasync.h(45) : warning C4115: '_RPC_ASYNC_STATE' : named type definition in parentheses
#include <windows.h>
#pragma warning (default:4115)
#include <conio.h>
#endif

#define EPSILON 0.002
#ifndef _WIN32
#define Sleep(x) usleep(x*1000)
#endif
int main()
{
    double pos = 0;
    double step = 0.001;
    GAUGE_init(
        10,     // Status field width
        1000    // Gauge width: deliberately big value produces full-screen gauge 
        );  

    GAUGE_set_status(_T("Time12312313123:"));
    GAUGE_set_pos(0);
    GAUGE_set_message(_T("1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"), 1.5);
    Sleep(200);
    GAUGE_set_pos(0);
    Sleep(200);
    GAUGE_set_message(_T("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"), 1.5);
    Sleep(200);
    GAUGE_set_pos(0);
    Sleep(200);
    GAUGE_set_message(_T("yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy"), 1.5);
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
//      if (fabs(pos - 0.5) < EPSILON) GAUGE_set_message(_T("This is One Half! This is One Half!This is One Half!This is One Half!This is One Half!This is One Half!This is One Half!This is One Half!This is One Half!This is One Half! "), 1.5);
        if (fabs(pos - 0.5) < EPSILON) GAUGE_set_message(_T("This is a message"), 1.5);
        if (fabs(pos - 0.4) < EPSILON) GAUGE_puts(_T("GAUGE_puts GAUGE_puts привет"), stdout);
        Sleep(1);
    }
    GAUGE_hide();
    return 0;
}

// dmc sys_gauge.c -Dsys_gauge_test && sys_gauge.exe && del *.obj *.map sys_gauge.exe

#endif // sys_gauge_test
