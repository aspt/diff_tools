#include "output.h"
#include "sys_gauge.h"
#include "sys_dirlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <float.h>
#ifdef UNICODE
#   pragma warning(disable: 4996)   // '_swprintf': swprintf has been changed ...
#else
#   include <windows.h>             // CharToOem
#endif

// maximum field width
#define FIELDW  128

#define ANCHORS_COUNT 9

static FILE *               g_report_file;
static fpos_t               g_report_file_pos;
ptrdiff_t                   g_anchors[ANCHORS_COUNT];

typedef struct
{
    void * link;
    ptrdiff_t anchors[ANCHORS_COUNT];
    TCHAR line[1];
} anchored_line_t;

anchored_line_t * g_last_line = NULL;
anchored_line_t * g_first_line = NULL;

#define DB(x,y) sprint_db_width(x,y,8)
#define SQR(x) ((x)*(x))

static int OffsetInfoHaveOffset (file_stat_t * stat, cmdline_options_t * opt)
{
    return (stat->actualOffsetSamples[0] ||
            stat->actualOffsetSamples[1] ||
            stat->remainingSamples[0] ||
            stat->remainingSamples[1] ||
            opt->offset_bytes[0] ||
            opt->offset_bytes[1]);
}

/**
*   Print numer/denum ratio as a bits count for equivalent 
*   uniform quantizer
*/
static TCHAR * SPrintBips (double numer, double denum, int width)
{
    static TCHAR buf[FIELDW];
    if (numer == 0)
    {
        _sntprintf(buf, FIELDW, _T("%-*s"), width, _T("MATCH   "));
    }
    else if (denum == 0)
    {
        _sntprintf(buf, FIELDW, _T("%-*s"), width, _T("INFINITY"));
    }
    else
    {
        double bips = 1. /  sqrt(12 * numer / denum);
        bips = 1 + log(bips) / log(2);
        if (_isnan(bips) || !_finite(bips))
        {
            _sntprintf(buf, FIELDW, _T("%-*s"), width, _T("overflow"));
        }
        else
        {
            _sntprintf(buf, FIELDW, _T("%-*.3f"), width, bips);
        }
    }
    return buf;
}

/**
*   Print numer/denum ratio in dB
*/
static TCHAR * sprint_db_width(double numer, double denum, int width)
{
    static TCHAR buf[FIELDW];
    if (numer == 0)
    {
        _sntprintf(buf, FIELDW, _T("%-*s"), width, _T("MATCH"));
    }
    else if (denum == 0)
    {
        _sntprintf(buf, FIELDW, _T("%-*s"), width, _T("INF  "));
    }
    else
    { 
        double db = -10 * log10(numer / denum);
        if (_isnan(db) || !_finite(db))
        {
            _sntprintf(buf, FIELDW, _T("%-*s"), width, _T("overflow"));
        }
        else
        {
            _sntprintf(buf, FIELDW, _T("%-*.2f"), width, db);
        }
    }
    buf[width] = 0;
    return buf;
}

/**
*   Calculate positions input line markers and 
*   save the line for log output with aligned markers 
*/
static void anchors_save_line(ptrdiff_t anchors[], TCHAR * s)
{
    int i;
    anchored_line_t *line;
    static ptrdiff_t dummy_anchors[ANCHORS_COUNT] = {0,};
    if (!anchors) anchors = dummy_anchors;
    
    line = (anchored_line_t*)malloc(sizeof(anchored_line_t) + _tcslen(s)*sizeof(TCHAR));
    if (line)
    {
        ptrdiff_t prev = 0;
        memcpy(line->anchors, anchors, ANCHORS_COUNT*sizeof(anchors[0]));
        for (i = ANCHORS_COUNT - 1; i >= 1; i--) 
        {
            anchors[i] -= anchors[i-1];
            g_anchors[i] -= g_anchors[i-1];
        }
        for (i = 0; i < ANCHORS_COUNT; i++) 
        {
            g_anchors[i] = MAX(g_anchors[i], anchors[i]) + prev;
            prev = g_anchors[i];
        }
        _tcscpy(line->line, s);
        
        // Put line to linked list 
        if (g_first_line)
        {
            g_last_line->link = line;
        }
        else
        {
            g_first_line = line;
        }
        g_last_line = line;
        line->link = NULL;
    }
}

/**
*   Print line to report file and stdout.
*   stdout output truncated to fit to screen
*/
static void my_puts(TCHAR * line)
{
#ifndef UNICODE
    CharToOem(line, line);  // Convert file names 
#endif
    if (g_report_file)
    {
#ifdef UNICODE
        char * ansi = GAUGE_unicode2ansi(line);
        if (ansi)
        {
            fputs(ansi, g_report_file);
            free(ansi);
        }
#else
        _fputts(line, g_report_file);
#endif
        fflush(g_report_file);
    }
    if (g_report_file != stdout)
    {
        unsigned int screen_width = GAUGE_screen_width();
        if (_tcslen(line) > screen_width)
        {
            line[screen_width] = 0;
            line[screen_width - 1] = '>';
        }
        GAUGE_puts(line, stdout);
    }
}

/**
*   Printf wrapper over my_puts. Besides, save the line for log output
*   This function used for unaligned output and error messages
*/
int my_printf(const TCHAR * format, ...)
{
    int                         chars_printed;
    TCHAR                       buf[2048];
    va_list                     va;
    va_start(va, format);
    chars_printed = _vsntprintf(buf, 2048, format, va);
    if (g_report_file)
    {
        // Save line to print together with aligned lines after
        // comparison
        anchors_save_line(NULL, buf);
    }
    my_puts(buf);
    return chars_printed;
}

/**
*   Summary report for multiple files comparison
*/
void print_totals(summary_stat_t * tot, FILE * hfile)
{
    _ftprintf(hfile, _T("\nFiles compared: %u from %u"), tot->files_compared, tot->files_count);
    if (tot->files_differs != tot->files_compared && tot->files_differs)
    {
        _ftprintf(hfile, _T("; %u files differs"), tot->files_differs);
    }
    if (tot->total_samples_count)
    {
        _ftprintf(hfile, _T("\nAverage PSNR square wave,    dB : %s"),
            DB(tot->d_sumSqr, (double)tot->total_samples_count));
        _ftprintf(hfile, _T("\nEquivalent quantization    bits : %s"),
            SPrintBips(tot->d_sumSqr, (double)tot->total_samples_count, 8));
        _ftprintf(hfile, _T("\nWorst-case PSNR square wave, dB : %s %s"),
            DB(tot->d_sumSqr_max, 1.0), tot->max_L2_error_file_name);
        _ftprintf(hfile, _T("\nWorst-case diff,             dB : %s %s"),
            DB(tot->d_abs_max * tot->d_abs_max, 1.0), tot->max_Linf_error_file_name);
        _ftprintf(hfile, _T("\nWorst-case diff, 16-bit samples : %.2f "),
            tot->d_abs_max * (1ul<<15));
        if (tot->d_sum)
        {
            _ftprintf(hfile, _T("\nDC offset,       16-bit samples : %+.5f (%sdB)"), 
                tot->d_sum / tot->total_samples_count  * (1ul<<15),
                DB(SQR(tot->d_sum / tot->total_samples_count), 1.)
                );
        }
        if (tot->r_sumSqr && tot->d_sumSqr)
        {
            _ftprintf(hfile, _T("\nAmplification                   : % .5f (%sdB)"),
                sqrt(tot->t_sumSqr / tot->r_sumSqr),
                DB((tot->t_sumSqr / tot->r_sumSqr), 1.)
                );
            if (tot->d_mul_r)
            {
                _ftprintf(hfile, _T("\nS/N correlation                 : %+.5f"),
                    tot->d_mul_r / sqrt(tot->r_sumSqr * tot->d_sumSqr )
                    );
            }
        }
    }
    else
    {
        _ftprintf(hfile, _T("\nWARNING: NO FILES COMPARED"));
    }
    _ftprintf(hfile, _T("\n"));
}

/**
*   Re-print report to log, aligning markers
*/
void anchors_print(summary_stat_t * totals)
{
    time_t timer;
    anchored_line_t * line;
    TCHAR * buf = malloc((g_anchors[ANCHORS_COUNT-1]+1)*sizeof(TCHAR));
    
    timer = time(NULL);
    _ftprintf(g_report_file, _T("\n==============================================================================="));
    _ftprintf(g_report_file, _T("\nCurrent time is %s"), _tasctime(localtime(&timer)));

    // Output lines from linked list
    while (NULL != (line = g_first_line))
    {
        TCHAR * p = line->line;
        if (buf && line->anchors[0])
        {
            int i=0,j,k=0; 
            for (j = 0; j < ANCHORS_COUNT; j++)
            {
                while(i < line->anchors[j] && (buf[k++] = p[i]) != 0) 
                {
                    i++;
                }
                while(k < g_anchors[j]) 
                {
                    buf[k++] = ' ';
                }
            }
            buf[k] = '\0';
            p = buf;
        }
        _fputts(p, g_report_file);
        g_first_line = g_first_line->link;
        free(line);
    }
    print_totals(totals, g_report_file);
    if(buf) free(buf);
}   


void print_name_long(const TCHAR * prefix, wav_file_t * wf, const TCHAR * file_name)
{
    TCHAR buf[FIELDW];
    if (wf->fmt.pcm_type == E_PCM_IEEE_FLOAT)
    {
        _tcscpy(buf, wf->fmt.bips == 32?_T("float  "):_T("double "));
    }
    else
    {
        _sntprintf(buf, FIELDW, _T("%2d bits%s"), labs(wf->fmt.bips), wf->fmt.bips < 0?_T(", big-endian"):_T(""));
    }
    my_printf(_T("%s %s %s %d ch %5d Hz %s (%d samples)\n"),
        prefix,
        WAV_format_string(wf),
        buf,
        wf->fmt.ch,
        wf->fmt.hz,
        file_name,
        WAV_samples_count(wf));
};


static int print_bips_short(TCHAR *p, wav_file_t * wf)
{
    if (wf->fmt.pcm_type == E_PCM_IEEE_FLOAT)
    {
        return _stprintf(p, _T("%s"), wf->fmt.bips == 32?_T("flt"):_T("dbl"));
    }
    else
    {
        return _stprintf(p, _T("%d"), wf->fmt.bips);
    }
}

static TCHAR * print_float(double val, int w, int frac)
{
    static int idx;
    static TCHAR buf[8][FIELDW]; // up to 8 simultaneous numbers
    TCHAR *p = buf[idx = (idx+1)&7];
    if (_isnan(val) || !_finite(val))
    {
        _sntprintf(p, FIELDW, _T("%.*s"), w, _T("overflow"));
    }
    else
    {
        int printed = _sntprintf(p, FIELDW, _T("%.*f"),  frac, val);
        if (printed > w || printed < 0)
        {
            _sntprintf(p, FIELDW, _T("%.*e"),  frac, val);
        }
    }
    return p;
}

static TCHAR * print_sign_float(double val, int w, int frac)
{
    static int idx;
    static TCHAR buf[8][FIELDW]; // up to 8 simultaneous numbers
    TCHAR *p = buf[idx = (idx+1)&7];
    if (_isnan(val) || !_finite(val))
    {
        _sntprintf(p, FIELDW, _T("%.*s"), w, _T("overflow"));
    }
    else
    {
        int printed = _sntprintf(p, FIELDW, _T("%+.*f"),  frac, val);
        if (printed > w || printed < 0)
        {
            _sntprintf(p, FIELDW, _T("%+.*e"),  frac, val);
        }
    }
    return p;
}

static double my_div(double x, double y)
{
    return y != 0 ? x/y : 0;
}

/**
*   Comparison report (short or long)
*/
void OUTPUT_print_file_stat (wav_file_t * wf[2], file_stat_t * diff, cmdline_options_t * opt)
{
    unsigned int i;
    double  dblPCMscale;
    static TCHAR s[4096];
    TCHAR *  p;
    double absmax_scaled;

    channel_stat_t * tot = diff->ch + diff->nch;
    channel_stat_t * ch = diff->ch;
    unsigned int nch = diff->nch;
    double n_samples = (double)diff->samlpes_count;
    double tot_samples = nch * n_samples;
    int have_offset = OffsetInfoHaveOffset(diff, opt);

    int stat_bips = MIN(labs(wf[0]->fmt.bips), labs(wf[1]->fmt.bips));
    if (wf[0]->fmt.pcm_type == E_PCM_IEEE_FLOAT)
    {
        stat_bips = wf[1]->fmt.pcm_type == E_PCM_IEEE_FLOAT ? 16 : labs(wf[1]->fmt.bips);
    }
    if (wf[1]->fmt.pcm_type == E_PCM_IEEE_FLOAT)
    {
        stat_bips = wf[0]->fmt.pcm_type == E_PCM_IEEE_FLOAT ? 16 : labs(wf[0]->fmt.bips);
    }
    dblPCMscale = ldexp(1, stat_bips - 1);
    if (opt->listing == E_LISTING_SHORT || (opt->listing == E_LISTING_NO_BITEXACT && (tot->d_sumSqr != 0 || have_offset)))
    {
        ptrdiff_t anchors[ANCHORS_COUNT];
        p = s;
        p += _stprintf(p, _T("PSNR (dB):%-9.9s"), DB(tot->d_sumSqr, tot_samples));

#if 0
        // use minimum BIPS of comparing files
        p += _stprintf(p, _T("Max*2^%2d:(%.0f"), stat_bips, diff_stat_abs_max(ch + 0) * dblPCMscale);
        for (i = 1; i < nch; i++)
        {
            p += _stprintf(p, _T(" : %.0f"), diff_stat_abs_max(ch + i) * dblPCMscale);
        }
        p += _stprintf(p, _T(")"));
#else
        // always use 16-bit BIPS
        absmax_scaled = diff_stat_abs_max(diff->ch + 0) * (1<<15);
        p += _stprintf(p, _T("Max*2^16:(%s"), print_float(absmax_scaled, 9, (absmax_scaled != 0 && absmax_scaled < 1)?2:0));
        for (i = 1; i < nch; i++)
        {
            absmax_scaled = diff_stat_abs_max(diff->ch + i) * (1<<15);
            p += _stprintf(p, _T(" : %s"), print_float(absmax_scaled, 9, (absmax_scaled != 0 && absmax_scaled < 1)?2:0));
        }
        p += _stprintf(p, _T(")"));
#endif
#define FIX_ANCHORS(z) for (anchors[z] = p - s; anchors[z] < g_anchors[z]; anchors[z]++) *p++ = ' ';

        while (p - s < 40 || (p - s) % 8)
        {
            *p++ = ' ';
        }
        FIX_ANCHORS(0)

        if (wf[0]->container != wf[0]->container)
        {
            p += _stprintf(p, _T(" %s<->%s/"), WAV_format_string(wf[0]), WAV_format_string(wf[1]));
        }
        else
        {
            p += _stprintf(p, _T(" %s/"), WAV_format_string(wf[0]));
        }
        FIX_ANCHORS(1)

        if (wf[0]->fmt.bips != wf[1]->fmt.bips || wf[0]->fmt.pcm_type != wf[1]->fmt.pcm_type)
        {
            p += print_bips_short(p, wf[0]);
            p += _stprintf(p, _T("<->"));
            p += print_bips_short(p, wf[1]);
        }
        else
        {
            p += print_bips_short(p, wf[0]);
        }
        FIX_ANCHORS(2)

        if (wf[0]->fmt.hz != wf[1]->fmt.hz && wf[0]->fmt.hz != 0 && wf[1]->fmt.hz != 0)
        {
            p += _stprintf(p, _T("/%5lu<->%5lu "), wf[0]->fmt.hz, wf[1]->fmt.hz);
        }
        else
        {
            p += _stprintf(p, _T("/%5lu "), wf[0]->fmt.hz ? wf[0]->fmt.hz : wf[1]->fmt.hz);
        }
        FIX_ANCHORS(3)

        p += _stprintf(p, _T("[%7u smp] "), diff->samlpes_count);

        FIX_ANCHORS(4)

        if (have_offset)
        {
            p += _stprintf(p, _T(" Offsets: "));
            if (opt->offset_bytes[0])
            {
                p += _stprintf(p, _T("%u bytes + "), opt->offset_bytes[0]);
            }
            if (diff->actualOffsetSamples[0])
            {
                p += _stprintf(p, _T("%lu+"), diff->actualOffsetSamples[0]);
            }
            p += _stprintf(p, _T("[1st]"));
            if (diff->remainingSamples[0])
            {
                p += _stprintf(p, _T("+%") _T(PRIi64), diff->remainingSamples[0]);
            }
            FIX_ANCHORS(5)
            p += _stprintf(p, _T(" <-> "));
            if (opt->offset_bytes[1])
            {
                p += _stprintf(p, _T("%u bytes + "), opt->offset_bytes[1]);
            }
            if (diff->actualOffsetSamples[1])
            {
                p += _stprintf(p, _T("%u+"), diff->actualOffsetSamples[1]);
            }
            p += _stprintf(p, _T("[2nd]"));
            if (diff->remainingSamples[1])
            {
                p += _stprintf(p, _T("+%") _T(PRIi64), diff->remainingSamples[1]);
            }
        }
        else
        {
            FIX_ANCHORS(5)
        }
        
        while ((p - s) % 16)
        {
            *p++ = ' ';
        } 
        FIX_ANCHORS(6)

        p += _stprintf(p, _T(" %s"), PATH_after_last_separator(opt->file_name[0]));
        if (_tcscmp(
                 PATH_after_last_separator(opt->file_name[0]), 
                 PATH_after_last_separator(opt->file_name[1])
                 ))
        {
            while ((p - s) % 16)
            {
                *p++ = ' ';
            } 
            FIX_ANCHORS(7)

            p += _stprintf(p, _T(" <-> "));
            p += _stprintf(p, _T(" %s"), PATH_after_last_separator(opt->file_name[1]));
        }
        else
        {
            FIX_ANCHORS(7)
        }

        *p++ = '\n';
        *p++ = '\0';
        FIX_ANCHORS(8)
        anchors_save_line(anchors, s);
        my_puts(s);
    }
    else if (opt->listing == E_LISTING_LONG)
    {
        double denum;
        print_name_long(_T("\nComparing "), wf[0],  opt->file_name[0]);
        print_name_long(_T("With      "), wf[1],  opt->file_name[1]);
        
        my_printf(_T("Compared: %d samples (%5.1f%%).\n"),
                 diff->samlpes_count,
                 100. * diff->samlpes_count/ (MAX(WAV_samples_count(wf[0]), WAV_samples_count(wf[1]))));

        if (have_offset)
        {
            p = s;
            if (opt->offset_bytes[0])
            {
                p += _stprintf(p, _T("%u bytes skipped from 1st file. "), opt->offset_bytes[0]);
            }
            if (diff->actualOffsetSamples[0])
            {
                p += _stprintf(p, _T("%u samples skipped from 1st file. "), diff->actualOffsetSamples[0]);
            }
            if (diff->remainingSamples[0])
            {
                p += _stprintf(p, _T("%") _T(PRIi64) _T(" samples remains in 1st file. "), diff->remainingSamples[0]);
            }
            if (p != s)
            {
                my_printf(_T("%s\n"), s); 
            }

            p = s;
            if (opt->offset_bytes[1])
            {
                p += _stprintf(p, _T("%u bytes skipped from 2nd file. "), opt->offset_bytes[1]);
            }
            if (diff->actualOffsetSamples[1])
            {
                p += _stprintf(p, _T("%u samples skipped from 2nd file. "), diff->actualOffsetSamples[1]);
            }
            if (diff->remainingSamples[1])
            {
                p += _stprintf(p, _T("%") _T(PRIi64) _T(" samples remains in 2nd file. "), diff->remainingSamples[1]);
            }
            if (p != s)
            {
                my_printf(_T("%s\n"), s); 
            }
        }

        p = s;
        p += _stprintf(p, _T("                        Total          |"));
        if (diff->nch != 1) for (i = 0; i < diff->nch; i++)
        {
            p += _stprintf(p, _T("Ch%2u           "), i + 1);
        }
        my_printf(_T("%s\n"), s); p = s;

        p += _stprintf(p, _T("PSNR Square(dB):        %-15.15s|"), DB(tot->d_sumSqr, tot_samples));
        if (nch != 1) for (i = 0; i < nch; i++)
        {
            p += _stprintf(p, _T("%-15.15s"), DB(diff->ch[i].d_sumSqr, n_samples));
        }
        my_printf(_T("%s\n"), s); p = s;

        p += _stprintf(p, _T("PSNR Sine  (dB):        %-15.15s|"), DB(2 * tot->d_sumSqr, tot_samples));
        if (nch != 1) for (i = 0; i < nch; i++)
        {
            p += _stprintf(p, _T("%-15.15s"), DB(2 * diff->ch[i].d_sumSqr, n_samples));
        }
        my_printf(_T("%s\n"), s); p = s;

        p += _stprintf(p, _T("SNR        (dB):        %-15.15s|"), DB(tot->d_sumSqr, tot->r_sumSqr ));
        if (nch != 1) for (i = 0; i < nch; i++)
        {
            p += _stprintf(p, _T("%-15.15s"), DB(diff->ch[i].d_sumSqr, diff->ch[i].r_sumSqr));
        }
        my_printf(_T("%s\n"), s); p = s;

        p += _stprintf(p, _T("Quantizer  bits:        %-15.15s|"), SPrintBips(tot->d_sumSqr, tot_samples, 8));
        if (nch != 1) for (i = 0; i < nch; i++)
        {
            p += _stprintf(p, _T("%-15.15s"), SPrintBips(diff->ch[i].d_sumSqr, n_samples, 8));
        }
        my_printf(_T("%s\n"), s); p = s;

        p += _stprintf(p, _T("RMSE   (* 2^%2d):        %-15s|"), stat_bips, print_float(dblPCMscale * sqrt(my_div(tot->d_sumSqr, tot_samples)), 9,3 ));
        if (nch != 1) for (i = 0; i < nch; i++)
        {
            p += _stprintf(p, _T("%-15s"), print_float(dblPCMscale * sqrt(my_div(diff->ch[i].d_sumSqr, n_samples)), 9,3));
        }
        my_printf(_T("%s\n"), s); p = s;

        absmax_scaled = diff_stat_abs_max(tot) * dblPCMscale;
        p += _stprintf(p, _T("|Max|  (* 2^%2d):        %-15s|"), stat_bips, print_float(absmax_scaled, 9, (absmax_scaled != 0 && absmax_scaled < 1)?5:0));
        if (nch != 1) for (i = 0; i < nch; i++)
        {
            absmax_scaled = diff_stat_abs_max(diff->ch + i) * dblPCMscale;
            p += _stprintf(p, _T("%-15s"), print_float(absmax_scaled, 9, (absmax_scaled != 0 && absmax_scaled < 1)?5:0));
        }
        my_printf(_T("%s\n"), s); p = s;

        p += _stprintf(p, _T("DC     (* 2^%2d):        %-15s|"), stat_bips, print_float(my_div(tot->d_sum, tot_samples) * dblPCMscale, 9, 4));
        if (nch != 1) for (i = 0; i < nch; i++)
        {
            p += _stprintf(p, _T("%-15s"), print_float(my_div(diff->ch[i].d_sum, n_samples) * dblPCMscale, 9, 4));
        }

        my_printf(_T("%s\n"), s); p = s;

        denum = sqrt(tot->d_sumSqr * tot->r_sumSqr);
        p += _stprintf(p, _T("S/N correlation:        %-15s|"), print_sign_float(my_div(tot->d_mul_r, denum), 15, 6));
        if (nch != 1) for (i = 0; i < nch; i++)
        {
            denum = sqrt(diff->ch[i].d_sumSqr * diff->ch[i].r_sumSqr);
            p += _stprintf(p,_T("%-15s"), print_sign_float(my_div(diff->ch[i].d_mul_r, denum), 15, 6));
        }
        my_printf(_T("%s\n"), s); p = s;
#if ACF
        p += _stprintf(p, _T("Noise ACF (R01):        %-15s|"), print_sign_float(my_div(tot->d_mul_dm1, tot->d_sumSqr), 15, 6));
        if (nch != 1) for (i = 0; i < nch; i++)
        {
            p += _stprintf(p, _T("%-15s"), print_sign_float(my_div(ch[i].d_mul_dm1, ch[i].d_sumSqr), 15, 6));
        }
        my_printf(_T("%s\n"), s); p = s;
#endif

        p += _stprintf(p, _T("Amplification  :        %-15s|"), print_float(sqrt(my_div(tot->t_sumSqr, tot->r_sumSqr)), 11, 6));
        if (nch != 1) for (i = 0; i < nch; i++)
        {
            p += _stprintf(p, _T("%-15s"), print_float(sqrt(my_div(ch[i].t_sumSqr, ch[i].r_sumSqr)), 11, 6));
        }
        my_printf(_T("%s\n"), s); p = s;
    }
}

/**
*   Single file statistic report 
*/
void OUTPUT_showStat(TFileInfo* nfo)
{
    unsigned int i;
    static TCHAR s[4096];
    TCHAR *  p;
    channel_stat_t * tot = nfo->stat.ch + nfo->stat.nch;
    channel_stat_t * ch = nfo->stat.ch;
    unsigned int nch = nfo->stat.nch;
    double n_samples = (double)nfo->stat.samlpes_count;
    double tot_samples = n_samples * nch;
    double  dblPCMscale;

    int stat_bips = labs(nfo->bips);
    if (stat_bips > 24) stat_bips = 16;
    dblPCMscale = ldexp(1, stat_bips - 1);
    p = s;
    p += _stprintf(p, _T("                        Total          |"));
    if (nch != 1) for (i = 0; i < nch; i++)
    {
        p += _stprintf(p, _T("Ch%2u           "), i + 1);
    }
    my_printf(_T("%s\n"), s); p = s;
    p += _stprintf(p, _T("RMS square (dB):        %-15.15s|"), DB(1.0, my_div(tot->d_sumSqr, tot_samples)));
    if (nch != 1) for (i = 0; i < nch; i++)
    {
        p += _stprintf(p, _T("%-15.15s"), DB(1.0, my_div(ch[i].d_sumSqr, n_samples)));
    }
    my_printf(_T("%s\n"), s); p = s;
    p += _stprintf(p, _T("RMS    (* 2^%2d):        %-15s|"), stat_bips, print_float(dblPCMscale * sqrt(my_div(tot->d_sumSqr, tot_samples)), 9, 3));
    if (nch != 1) for (i = 0; i < nch; i++)
    {
        p += _stprintf(p, _T("%-15s"), print_float(dblPCMscale * sqrt(my_div(ch[i].d_sumSqr, n_samples)), 9, 3));
    }
    my_printf(_T("%s\n"), s); p = s;
    p += _stprintf(p, _T("Max    (* 2^%2d):        %-15s|"), stat_bips, print_float(tot->d_max * dblPCMscale, 9, 0));
    if (nch != 1) for (i = 0; i < nch; i++)
    {
        p += _stprintf(p, _T("%-15s"), print_float(ch[i].d_max * dblPCMscale, 9, 0));
    }
    my_printf(_T("%s\n"), s); p = s;
    p += _stprintf(p, _T("Min    (* 2^%2d):        %-15s|"), stat_bips, print_float(tot->d_min * dblPCMscale, 9, 0));
    if (nch != 1) for (i = 0; i < nch; i++)
    {
        p += _stprintf(p, _T("%-15s"), print_float(ch[i].d_min * dblPCMscale, 9, 0));
    }
    my_printf(_T("%s\n"), s); p = s;
    p += _stprintf(p, _T("DC     (* 2^%2d):        %-15s|"), stat_bips, print_float(my_div(tot->d_sum, tot_samples) * dblPCMscale, 9, 4));
    if (nch != 1) for (i = 0; i < nch; i++)
    {
        p += _stprintf(p, _T("%-15s"), print_float(my_div(ch[i].d_sum, n_samples) * dblPCMscale, 9, 4));
    }
    my_printf(_T("%s\n"), s); p = s;
#if ACF
    p += _stprintf(p, _T("Autocorrel  R01:        %-15s|"), print_sign_float(my_div(tot->d_mul_dm1, tot->d_sumSqr), 15, 4));
    if (nch != 1) for (i = 0; i < nch; i++)
    {
        p += _stprintf(p, _T("%-15s"), print_sign_float(my_div(ch[i].d_mul_dm1, ch[i].d_sumSqr), 15, 4));
    }
    my_printf(_T("%s\n"), s); p = s;
#endif
    my_printf(_T("Entropy      Q7:        %.2f bits per sample (8-bit quantizer)\n"), nfo->entropy256);
    my_printf(_T("Entropy     Q15:        %.2f bits per sample (16-bit quantizer)\n"), nfo->entropy64k);
    my_printf(_T("Used bits mask : %08X\n"), nfo->usedBits32);
    
}

// Size of gauge status field
#define GAUGE_STATUS_LEN 40


void OUTPUT_update_gauge_status(const TCHAR * file_name, const summary_stat_t * tot)
{
    static TCHAR compact_path[GAUGE_STATUS_LEN+1];
    int len = 0;
    if (tot->files_compared)
    {
        len = _sntprintf(compact_path, GAUGE_STATUS_LEN, _T("[%s] "), sprint_db_width (tot->d_sumSqr_max, 1, 5));
        if (len < 0)
        {
            len = 0;
        }
    }
    // Create/update gauge
    PATH_compact_path(compact_path + len, file_name, GAUGE_STATUS_LEN+1-len);
    GAUGE_set_status(compact_path);
}


void OUTPUT_init(cmdline_options_t *opt)
{
    GAUGE_init(GAUGE_STATUS_LEN, 200);
    
    if (opt->report_name)
    {
        // Open report file, if specified explicitly 
        g_report_file = _tfopen(opt->report_name, opt->report_append_flag?_T("r+t"):_T("wt"));
        if (g_report_file)
        {
            fseek(g_report_file, 0, SEEK_END);
        }
        else if (opt->report_append_flag)
        {
            // Try to re-open in overwrite mode
            g_report_file = _tfopen(opt->report_name, _T("wt"));
        }
    }
    else if (GAUGE_is_stdout_redirected())
    {
        // Use stdout as a report file, if program output was redirected 
        g_report_file = stdout;
    }

    if (g_report_file)
    {
        fgetpos(g_report_file, &g_report_file_pos);
    }

    if (opt->listing == E_LISTING_DEFAULT)
    {
        opt->listing = (opt->is_single_file ? E_LISTING_LONG : E_LISTING_SHORT);
    }
}


void OUTPUT_close(cmdline_options_t *opt, summary_stat_t * totals)
{
    // keep gauge for execution time info 
    // GAUGE_Hide(); // remove gauge
    GAUGE_close();

    if (!opt->is_single_file)
    {
        // it was a batch session:

        // re-print report, aligning output for readability
        // There is no sense to align single-file report
        if (g_report_file) 
        {
            fsetpos(g_report_file, &g_report_file_pos);
            anchors_print(totals);
        }

        // Print summary report
        // For single-file processing 'long listing' format provides similar info 
        print_totals(totals, stderr);
    }

    if (g_report_file)
    {
        fclose(g_report_file);
    }
}

