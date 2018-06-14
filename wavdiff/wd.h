/******************************************************************************
     1.0.0   08.08.2004           Initial Version
******************************************************************************/
#ifndef WAVDIFF_H
#define WAVDIFF_H

#include "f_wav_io.h"
#include "../type_tchar.h"
#include <wchar.h>

#if (defined(__GNUC__) && __GNUC__ >= 4) || (defined __STDC_VERSION__ && __STDC_VERSION__ >= 199901)
#   include <stdint.h>  // hope that all GCC compilers support this C99 extension
#   include <inttypes.h>  // hope that all GCC compilers support this C99 extension
#else
#   if defined (_MSC_VER)
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#       define PRIi64 "I64i"
#       define PRIu64 "I64u"
#   else
typedef long long int64_t;
typedef unsigned long long uint64_t;
#       define PRIi64 "lli"
#       define PRIu64 "llu"
#   endif
#endif

#define MAX_CH 50
#define ACF     1

/**
*   Command-line options
*/
typedef struct
{
    TCHAR           *   file_name[3];
    TCHAR           *   report_name;
    int                 report_append_flag;
    int                 bips;
    int                 ch;
    enum pcm_data_type_e pcm_type;
    int                 is_bips_set; 
    int                 is_ch_set;
    enum
    {
        E_LISTING_DEFAULT = 0,
        E_LISTING_NO_BITEXACT,
        E_LISTING_SHORT,
        E_LISTING_LONG
    }                   listing;
    unsigned int        offset_bytes[2];
    unsigned int        offsetSamples[2];
    int                 align_range_samples;
    int                 save_aligned_flag;
    int                 no_warn_cant_open;
    int                 is_single_file;
} cmdline_options_t;     

/**
*   channel difference statistic data
*/
typedef struct
{
    double  d_max;
    double  d_min;
    double  d_sumSqr;
    double  d_sum;
    double  r_sumSqr;
    double  t_sumSqr;
    double  d_mul_r;
#if ACF
    double  d_mul_dm1;
    double  dm1;
#endif
} channel_stat_t;


#define MAX_FILES 3
/**
*   file pair statistics
*/
typedef struct
{
    size_t          samlpes_count;
    unsigned int    nch;
    channel_stat_t    ch[MAX_CH + 1];

    wav_file_t      *file[2];
    wav_file_t      *diff;

    //
    // Files alignment info
    //
        
    // Number of samples, skipped by alignment procedure
    unsigned long   actualOffsetSamples[2];
        
    // Remaining samples in the files
    int64_t         remainingSamples[2];

} file_stat_t;

/**
*   session statistics
*/
typedef struct
{
    unsigned int files_count;
    unsigned int files_compared;
    unsigned int files_differs;
    int64_t      total_samples_count;
    double       r_sumSqr;
    double       t_sumSqr;
    double       d_mul_r;
    double       d_sumSqr;
    double       d_sum;
    double       d_abs_max;
    double       d_sumSqr_max;
    TCHAR        max_L2_error_file_name[1024];
    TCHAR        max_Linf_error_file_name[1024];
} summary_stat_t;

/**
*   file statistics
*/
typedef struct 
{
    file_stat_t stat;
    long    usedBits32;
    double  entropy256;
    double  entropy64k;
    int     histogram256[256];
    int     histogram64k[256*256];
    int     bips;
} TFileInfo;


double diff_stat_abs_max(channel_stat_t * s);

#endif //WAVDIFF_H
