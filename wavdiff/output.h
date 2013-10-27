/******************************************************************************
     1.0.0   08.08.2004           Initial Version
******************************************************************************/
#ifndef OUTPUT_H
#define OUTPUT_H

#include "wd.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX
#  define MAX(x,y) ((x)>(y) ? (x):(y))
#endif

#ifndef MIN
#  define MIN(x,y) ((x)<(y) ? (x):(y))
#endif

#ifndef ABS
#  define ABS(x)   ((x)>=0 ? (x):(-x))
#endif


int my_printf (
    const TCHAR * format,
    ...
    );
    
void OUTPUT_init(
    cmdline_options_t *opt
    );

void OUTPUT_close(
    cmdline_options_t *opt,
    summary_stat_t* totals
    );

void OUTPUT_print_file_stat (
    wav_file_t * wf[2], 
    file_stat_t * diff, 
    cmdline_options_t * opt
    );

void OUTPUT_showStat(
    TFileInfo* pInfo
    );

void OUTPUT_update_gauge_status(
    const TCHAR * file_name,
    const summary_stat_t * tot
    );

#ifdef __cplusplus
}
#endif

#endif //OUTPUT_H
