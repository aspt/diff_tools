#include "sys_gauge.h"
#include "sys_dirlist.h"
#include "output.h"
#include "f_wav_align.h"
#include "wd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

extern int lzf_decompress_data_to_file(FILE * f); // help.c


#if defined _MSC_VER && defined UNICODE
#   pragma warning(disable: 4996)   // '_swprintf': swprintf has been changed ...
#   pragma warning(disable: 4007)   // 'wmain' : must be '__cdecl'
#endif

// Audio buffer size
#define BUF_SIZE_SAMPLES (0x20000)

// Default sample rate, used when generating difference for RAW PCM files.
#define DEFAULT_SAMPLERATE 44100

#define SQR(x) ((x) * (x))

#define LOG2_10 3.321928094887362348

#ifndef MAX_PATH
#  define MAX_PATH 1024
#endif
#ifndef NELEM
#   define NELEM( x )           ( sizeof(x) / sizeof((x)[0]) )
#endif


static TCHAR g_lazy_output_dir[MAX_PATH];
static double g_buf[3][BUF_SIZE_SAMPLES];

static __int64      g_current_file_size;
static __int64      g_total_file_size;
static summary_stat_t g_tot;
static cmdline_options_t g_opt;
static int          g_abort_flag = 0;



static int esc_pressed(void)
{
    if (GAUGE_esc_pressed())
    {
        g_abort_flag = 1;
    }
    return g_abort_flag;
}

/** 
*   return 1 if string p begins with pattern
*/
static int smatch(const TCHAR * pattern, TCHAR**p)
{
    TCHAR *s = *p;
    while (*s && *pattern && *pattern == *s++) pattern++;
    if (!*pattern) 
    {
        *p = s;
    }
    return !*pattern;
}


static int atoi_ex(TCHAR* p)
{
    int mul = 1;
    size_t last_pos = _tcslen(p);
    if (last_pos > 1)
    {
        switch (p[last_pos - 1])
        {
        case 'k':
        case 'K':
            p[last_pos - 1] = 0; mul = 1024;             
            break;
        case 'm':
        case 'M':
            p[last_pos - 1] = 0; mul = 1024 * 1024;      
            break;
        }
    }
    return _ttoi(p) * mul;
}


static void usage(void)
{
    puts("\n"
    "Audio files difference tool                               -:[ "__DATE__" ]:-\n"
    "usage: wd file1 [file2 [file_difference]] [options]\n"
    "\n"
    "Option       Defaults  Note\n"
    "=============================================================================\n"
    "file1        mandatory First (reference) file/directory to compare (WAV/PCM)\n"
    "file2        optional  Second (test) file/directory to compare (WAV/PCM)\n"
    "file_diff    optional  Produce difference between files := file2 - file1\n"
    "-r<file>     optional  Write report to <file>\n"
    "-p<file>     optional  Append report to <file>\n"
    "-bits<int>   16        Set bits per sample for raw PCM\n"
    "-ch<int>     2         Set number of channels for raw PCM\n"
    "-ll          file mode Produce detailed report listing\n"
    "-ls          dir mode  Produce short one-line report listing\n"
    "-lx          no        One-line report listing w/o bitexact files\n"
    "-ob1:<int>   0         Ignore <int> initial data bytes of the first file\n"
    "-ob2:<int>   0         Ignore <int> initial data bytes of the second file\n"
    "-os1:<int>   0         Ignore <int> initial samples of the first file\n"
    "-os2:<int>   0         Ignore <int> initial samples of the second file\n"
    "-align<int>  No        Use \"Best Match\" offset sample before comparison.\n"
    "-saveAligned No        Write aligned second file instead of difference\n"
    "-wo          No        No warn on file open fail\n"
    "-h           No        Produce wd.html help file\n"
    "=============================================================================\n"
    "Notes:\n"
    " * wildcards * and ? in file names accepted\n"
    " * -bits and -ch options are used only for raw PCM files\n"
    " * Negative -bits values convert PCM samples from Motorola format\n"
    " * Fractional -bits values assumes IEEE format: -bits.32 == float\n"
    " * \"Best Match\" alignment applied after -off parameters,\n"
    "   so it gives you a chance to compare parts after dropouts\n"
    " * If only one file name given, file statistics reported\n"
    " * -align option can take <int> argument to increase alignment buffer size\n"
    " * -short listing difference always shown in 16-bit samples\n"
    "Examples:\n"
    "wd -align256k -ls reference.wav totest.wav diff.wav -rTestReport.txt\n"
    "wd ref/*.wav test/*.raw -align\n"
    "See http://asp.lionhost.ru/tools.html for updates");
}


static int read_cmdline_options (int argc, TCHAR *argv[], cmdline_options_t *opt)
{
    int i;
    memset(opt, 0, sizeof(*opt));
    opt->pcm_type = E_PCM_INTEGER;
    opt->bips = 16;
    opt->ch = 2;

    for (i = 1; i < argc; i++)
    {
        TCHAR *  p   = argv[i];

        if (*p == '-' || *p == '/')
        {
            p++;
            if (smatch(_T("r"), &p))
            {
                if (!*p)
                {
                    _tprintf(_T("ERROR: -r option without file name\n"));
                    return 0;
                }
                else
                {
                    opt->report_append_flag = 0;
                    opt->report_name = p;
                }
            }
            else if (smatch(_T("p"), &p))
            {
                if (!*p)
                {
                    _tprintf(_T("ERROR: -p option without file name\n"));
                    return 0;
                }
                else
                {
                    opt->report_append_flag = 1;
                    opt->report_name = p;
                }
            }
            else if (smatch(_T("bits"), &p))
            {
                if (*p == '.') 
                {
                    opt->pcm_type = E_PCM_IEEE_FLOAT;
                    p++;
                }
                else
                {
                    opt->pcm_type = E_PCM_INTEGER;
                }
                opt->is_bips_set = 1;
                opt->bips = _ttoi(p);
            }
            else if (smatch(_T("ch"), &p))
            {
                opt->ch = _ttoi(p);
                opt->is_ch_set = 1;
            }
            else if (smatch(_T("wo"), &p))
            {
                opt->no_warn_cant_open = 1;
            }
            else if (smatch(_T("ls"), &p))
            {
                opt->listing = E_LISTING_SHORT;
            }
            else if (smatch(_T("ll"), &p))
            {
                opt->listing = E_LISTING_LONG;
            }
            else if (smatch(_T("lx"), &p))
            {
                opt->listing = E_LISTING_NO_BITEXACT;
            }
            else if (smatch(_T("ob1:"), &p))
            {
                opt->offset_bytes[0] = atoi_ex(p); opt->offsetSamples[0] = 0;
            }
            else if (smatch(_T("ob2:"), &p))
            {
                opt->offset_bytes[1] = atoi_ex(p); opt->offsetSamples[1] = 0;
            }
            else if (smatch(_T("os1:"), &p))
            {
                opt->offsetSamples[0] = atoi_ex(p); opt->offset_bytes[0] = 0;
            }
            else if (smatch(_T("os2:"), &p))
            {
                opt->offsetSamples[1] = atoi_ex(p); opt->offset_bytes[1] = 0;
            }
            else if (smatch(_T("saveAligned"), &p))
            {
                opt->save_aligned_flag = 1;
            }
            else if (smatch(_T("align"), &p))
            {
                opt->align_range_samples = *p ? atoi_ex(p) : 1024*8*2;
            }
            else if (smatch(_T("h"), &p))
            {
            FILE * f;
                f = _tfopen(_T("wd.html"), _T("wb"));
                if (f)
                {
                    if (lzf_decompress_data_to_file(f))
                    {
                        fclose(f);
                        _tprintf(_T("wd.html help file created\n"));
                        system("start wd.html");
                    }
                    else fclose(f);
                }
                else
                {
                    _tprintf(_T("ERROR: can't open file wd.html for writing\n"));
                }
                return 0;
            }
            else
            {
                _tprintf(_T("ERROR: Unknown option %s\n"), p - 1);
                return 0;
            }
        }
        else if (!opt->file_name[0])
        {
            opt->file_name[0] = p;
        }
        else if (!opt->file_name[1])
        {
            opt->file_name[1] = p;
        }
        else if (!opt->file_name[2])
        {
            opt->file_name[2] = p;
        }
        else
        {
            _tprintf(_T("ERROR: Unknown option %s\n"), p);
            return 0;
        }
    }
    return 1;
}


static int verify_cmdline_options(cmdline_options_t *opt)
{
    if (!opt->file_name[0])
    {
        _tprintf(_T("ERROR: Input file name was not specified\n"));
        return 0;
    }

    if (opt->file_name[1] && !_tcscmp(opt->file_name[0], opt->file_name[1]))
    {
        _tprintf(_T("ERROR: Trying to compare file %s with itself!\n"), opt->file_name[0]);
        return 0;
    }

    if (opt->file_name[2] &&
        (!_tcscmp(opt->file_name[2], opt->file_name[0]) || 
         !_tcscmp(opt->file_name[2], opt->file_name[1]))
       )
    {
        _tprintf(_T("ERROR: Difference file %s must not be the same as the file under test\n"), opt->file_name[2]);
        return 0;
    }

    if ( (opt->pcm_type != E_PCM_IEEE_FLOAT && ABS(opt->bips) > 32) || ABS(opt->bips) % 8)
    {
        _tprintf(_T("ERROR: BPS value %d is not supported!\n"), opt->bips);
        return 0;
    }

    if ((unsigned) opt->ch > MAX_CH)
    {
        _tprintf(_T("ERROR: Channels value %d is not supported!\n"), opt->ch);
        return 0;
    }

    return 1;
}


static void copy_wav_format(const wav_file_t * s, wav_file_t * d, cmdline_options_t * opt)
{
    if (opt->is_bips_set)
    {
        d->fmt.pcm_type = opt->pcm_type;
        d->fmt.bips = opt->bips;
    }
    else
    {
        d->fmt.bips = s->fmt.bips;
        d->fmt.pcm_type = s->fmt.pcm_type;
    }
    d->fmt.hz = s->fmt.hz;
    if (opt->is_ch_set)
    {
        d->fmt.ch = opt->ch;
    }
    else
    {
        d->fmt.ch = s->fmt.ch;
    }
}


static int syncronize_formats(wav_file_t ** file, cmdline_options_t * opt)
{
    if (file[0]->container != EFILE_RAW && file[1]->container != EFILE_RAW)
    {
        if (file[0]->fmt.ch != file[1]->fmt.ch)
        {
            my_printf(_T("ERROR: Different number of channels: File %s have %d channels and file %s have %d channels.\n"),
                     opt->file_name[0],
                     file[0]->fmt.ch,
                     opt->file_name[1],
                     file[1]->fmt.ch);
            return 0;
        }
    }
    else if (file[0]->container != EFILE_RAW)
    {
        copy_wav_format(file[0], file[1], opt);
    }
    else if (file[1]->container != EFILE_RAW)
    {
        copy_wav_format(file[1], file[0], opt);
    }
    if (file[0]->fmt.ch != file[1]->fmt.ch)
    {
        my_printf(_T("ERROR: Different number of channels: File %s have %d channels and file %s have %d channels.\n"),
            opt->file_name[0],
            file[0]->fmt.ch,
            opt->file_name[1],
            file[1]->fmt.ch);
        return 0;
    }
    return 1;
}


static int open_file(file_stat_t * stat, const cmdline_options_t * opt, int idx)
{
    wav_file_t * file;
    pcm_format_t  default_format;
    unsigned long offset_bytes;

    default_format.bips = opt->bips;
    default_format.pcm_type = opt->pcm_type;
    default_format.ch = opt->ch;
    default_format.hz = DEFAULT_SAMPLERATE;

    stat->file[idx] = file = WAV_open_read(opt->file_name[idx], &default_format);
    if (!file)
    {
        if (!opt->no_warn_cant_open)
        {
            my_printf(_T("ERROR: Can't open file %s\n"), opt->file_name[idx]);
        }
        return 0;
    }
    else
    {
        offset_bytes = opt->offset_bytes[idx] + opt->offsetSamples[idx] * WAV_bytes_per_sample(file);

        if (offset_bytes)
        {
            if (file->data_bytes <= offset_bytes)
            {
                my_printf(_T("ERROR: File %s have only %d data bytes; can not offset by %d bytes!\n"),
                         opt->file_name[idx],
                         (int)file->data_bytes,
                         offset_bytes);
                return 0;
            }
            fseek(file->file, offset_bytes, SEEK_CUR);
        }
    }

    if (ABS(file->fmt.bips) % 8 != 0 || (file->fmt.pcm_type != E_PCM_IEEE_FLOAT && ABS(file->fmt.bips) > 32))
    {
        my_printf(_T("ERROR: File %s have %d bits per sample and can not be processed.\n"),
                 opt->file_name[idx],
                 file->fmt.bips);
        return 0;
    }

    if (file->fmt.ch > MAX_CH)
    {
        my_printf(_T("ERROR: File %s have %d channels and can not be processed.\n"), opt->file_name[idx], file->fmt.ch);
        return 0;
    }

    return 1;
}


static int open_files(file_stat_t * stat, cmdline_options_t *opt)
{
    int i;
    wav_file_t ** file = stat->file;
    for (i = 0; i < 2; i++)
    {
        if (!open_file(stat, opt, i))
        {
            goto Cleanup;
        }
    }

    // Check if both files are zero length
    if (!file[0]->data_bytes && !file[1]->data_bytes)
    {
        my_printf(_T("WARNING: Both files have no samples. %s <-> %s\n"), opt->file_name[0], opt->file_name[1]);
        goto Cleanup;
    }

    // Check if zero-length file compared against non zero-length
    if (!file[0]->data_bytes || !file[1]->data_bytes)
    {
        my_printf(_T("ERROR: File %s have %ld samples, but file %s have %ld samples.\n"),
                 opt->file_name[0],
                 (long)WAV_samples_count(file[0]),
                 opt->file_name[1],
                 (long)WAV_samples_count(file[1]));
        goto Cleanup;
    }

    // Verify that both files have same audio format
    if (!syncronize_formats(file, opt))
    {
        goto Cleanup;
    }

    // Create difference file, ignore any errors...
    if (opt->file_name[2])
    {
        pcm_format_t  fmt;
        fmt.hz = file[0]->fmt.hz ? file[0]->fmt.hz : DEFAULT_SAMPLERATE;
        fmt.ch = file[0]->fmt.ch;
        if (file[0]->fmt.pcm_type == file[1]->fmt.pcm_type)
        {
            fmt.bips = MAX(ABS(file[0]->fmt.bips), ABS(file[1]->fmt.bips));
            fmt.pcm_type = file[0]->fmt.pcm_type;
        }
        else if (file[0]->fmt.pcm_type == E_PCM_IEEE_FLOAT)
        {
            fmt.pcm_type = file[0]->fmt.pcm_type;
            fmt.bips = file[0]->fmt.bips;
        }
        else
        {
            fmt.pcm_type = file[1]->fmt.pcm_type;
            fmt.bips = file[1]->fmt.bips;
        }

        if (g_lazy_output_dir[0])
        {
            DIR_force_directory(g_lazy_output_dir);
            g_lazy_output_dir[0] = 0;
        }

        stat->diff = WAV_open_write(opt->file_name[2], fmt, EFILE_WAV);
    }

    // Align files if specified
    if (opt->align_range_samples > 0)
    {
        if (!ALIGN_init(g_opt.align_range_samples, file[0]->fmt.ch))
        {
            my_printf(_T("ERROR: memory allocation error.\n"));
            goto Cleanup;
        }
        ALIGN_align_pair(file[0], file[1]);
    }

    return 1;

Cleanup:

    for (i = 0; i < 2; i++)
    {
        if (file[i])
        {
            WAV_close_read(file[i]);
        }
    }
    return 0;
}


static void diff_stat_gather(file_stat_t * stat, const double * p1, const double * p2, double * diff, size_t nsamples)
{
    unsigned int i, c;
    for (i = 0; i < nsamples; i++)
    {
        for (c = 0; c < stat->nch; c++)
        {
            channel_stat_t * s = stat->ch + c;
            double r = *p1++;
            double t = *p2++;
            double d = t - r;
            *diff++ = d;
            s->d_max = MAX(d, s->d_max);
            s->d_min = MIN(d, s->d_min);
            s->d_mul_r += d * r;
            s->d_sum += d;
            s->d_sumSqr += SQR(d);
            s->r_sumSqr += SQR(r);
            s->t_sumSqr += SQR(t);
#if ACF
            s->d_mul_dm1 += d * s->dm1;
            s->dm1 = d;
#endif
        }
    }
    stat->samlpes_count += nsamples;
}


static void diff_stat_sum_channels(file_stat_t * stat)
{
    channel_stat_t * avr = stat->ch + stat->nch;
    unsigned int c;
    for (c = 0; c < stat->nch; c++)
    {
        channel_stat_t * s = stat->ch + c;
        avr->d_max = MAX(avr->d_max, s->d_max);
        avr->d_min = MIN(avr->d_min, s->d_min);
        avr->d_mul_r += s->d_mul_r;
        avr->d_sum += s->d_sum;
        avr->d_sumSqr += s->d_sumSqr;
        avr->r_sumSqr += s->r_sumSqr;
        avr->t_sumSqr += s->t_sumSqr;
#if ACF
        avr->d_mul_dm1 += s->d_mul_dm1;
#endif
    }
}


double diff_stat_abs_max(channel_stat_t * stat)
{
    return MAX(fabs(stat->d_max), fabs(stat->d_min));
}


static void diff_stat_update_totals(file_stat_t * stat, summary_stat_t * tot, const TCHAR * file_name) 
{
    channel_stat_t * sumch = stat->ch + stat->nch;
    size_t tot_samples = stat->samlpes_count * stat->nch;
    tot->total_samples_count += tot_samples;
    tot->r_sumSqr += sumch->r_sumSqr;
    tot->t_sumSqr += sumch->t_sumSqr;
    if (tot->d_sumSqr_max < sumch->d_sumSqr / tot_samples)
    {
        tot->d_sumSqr_max = sumch->d_sumSqr / tot_samples;
        _tcsncpy(tot->max_L2_error_file_name, file_name, NELEM(tot->max_L2_error_file_name));
    }
    if (tot->d_abs_max < diff_stat_abs_max(sumch))
    {
        tot->d_abs_max = diff_stat_abs_max(sumch);
        _tcsncpy(tot->max_Linf_error_file_name, file_name, NELEM(tot->max_Linf_error_file_name));
    }
    tot->d_sumSqr += sumch->d_sumSqr;
    tot->d_sum += sumch->d_sum;
    tot->d_mul_r += sumch->d_mul_r;
    if (sumch->d_sumSqr)
    {
        g_tot.files_differs++;
    }
}

static int CompareFiles (file_stat_t * stat, cmdline_options_t * opt)
{
    int succeess = 0;
    wav_file_t ** file = stat->file; 
    int phase = 0;
    stat->nch = file[0]->fmt.ch;

    while (!esc_pressed())
    {
        size_t samples[2], samplesToCompare;

        samples[phase] = WAV_read_doubles(file[phase], g_buf[phase], BUF_SIZE_SAMPLES / file[phase]->fmt.ch );
        phase ^= 1;
        samples[phase] = WAV_read_doubles(file[phase], g_buf[phase], BUF_SIZE_SAMPLES / file[phase]->fmt.ch );
        
        samplesToCompare = MIN(samples[0], samples[1]);
        if (!samplesToCompare)
        {
            succeess = 1;
            break;
        }

        diff_stat_gather(stat, g_buf[0], g_buf[1], g_buf[2], samplesToCompare);
        if (stat->diff)
        {
            WAV_write_doubles(stat->diff, g_buf[opt->save_aligned_flag?1:2], samplesToCompare);
        }

        GAUGE_set_pos((double) (stat->samlpes_count * WAV_bytes_per_sample(file[0]) + g_current_file_size) /
                     g_total_file_size);

    }
    diff_stat_sum_channels(stat);
    return succeess;
}


static void InfoUpdate(TFileInfo* info, double * buf, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
    {
        unsigned int c;
        for (c = 0; c < info->stat.nch; c++)
        {
            double val = *buf++;
            //double val2 = val * val;
            val = MAX(val, -1); val = MIN(val, (double)0x7FFF/0x8000);
            info->usedBits32 |= (long) (0x80000000 * val);
            info->histogram64k[(long) (0x8000 * val) + 0x8000]++;
            info->histogram256[(long) (0x80 * val) + 0x80]++;
        }   
    }
}


static void FileStat (cmdline_options_t * opt)
{
    int i;
    size_t count;
    static TFileInfo  info;
    size_t nsamples;
    wav_file_t * file;

    memset(&info, 0, sizeof(info));

    if (!open_file(&info.stat, opt, 0))
    {
        return;
    }

    file = info.stat.file[0];
    info.stat.nch  = file->fmt.ch;
    info.bips = file->fmt.bips;

    my_printf(_T("\nInformation for file: %s\n"), opt->file_name[0]);
    my_printf(_T("Format: %s, %d bits per sample, %d channels, %d Hz\n"),
             WAV_format_string(file), file->fmt.bips, file->fmt.ch, file->fmt.hz);
    my_printf(_T("Reported file size: %") _T(PRIi64) _T(" samples (%.2f sec.)\n"),
             WAV_samples_count(file),
             file->fmt.hz ? (double) WAV_samples_count(file) / file->fmt.hz : 0.);

    while (0 != (nsamples = WAV_read_doubles(file, g_buf[0], BUF_SIZE_SAMPLES / file->fmt.ch )))
    {
        InfoUpdate(&info, g_buf[0], nsamples);
        diff_stat_gather(&info.stat, g_buf[1], g_buf[0], g_buf[2], nsamples);
        GAUGE_set_pos((double) (info.stat.samlpes_count * WAV_bytes_per_sample(file) + g_current_file_size) /
                     g_total_file_size);
    }
    diff_stat_sum_channels(&info.stat);

    my_printf(_T("Actual size       : %d samples read\n"), info.stat.samlpes_count);
    count = info.stat.samlpes_count * info.stat.nch;
    if (count)
    {
        info.entropy256 = 0;
        for (i = 0; i < 256; i++)
        {
            double prob = (double)info.histogram256[i] / count;
            if (prob )
                info.entropy256 -= prob*log10(prob)*LOG2_10;
        }
        info.entropy64k = 0;
        for (i = 0; i < 0x10000; i++)
        {
            double prob = (double)info.histogram64k[i] / count;
            if (prob )
                info.entropy64k -= prob*log10(prob)*LOG2_10;
        }
    }
    OUTPUT_showStat(&info);
}


static int RunCompare (cmdline_options_t *opt)
{
    int i, success = 0;
    file_stat_t stat = {0,};
    OUTPUT_update_gauge_status(opt->file_name[0], &g_tot);
    // If only one argument specified, show file statistics
    if (!opt->file_name[1])
    {
        FileStat(opt);
        return 0;
    }

    if (!open_files(&stat, opt))
    {
        return 0;
    }
    // Save files position
    for (i = 0; i < 2; i++)
    {
        wav_file_t * file = stat.file[i];
        stat.actualOffsetSamples[i] = (unsigned long)(WAV_get_sample_pos(file) - WAV_bytes_to_samples(file, opt->offset_bytes[i]));
        stat.remainingSamples[i] =  WAV_samples_count(file) - WAV_get_sample_pos(file);
    }
              
    if (opt->save_aligned_flag)
    {
        int samplesCount = stat.actualOffsetSamples[0] - stat.actualOffsetSamples[1];

        // if first file have larger offset, pad difference with zeros
        if (samplesCount > 0 && stat.diff)
        {
            double buf[MAX_CH] = {0,};
            while (samplesCount--)
            {
                WAV_write_doubles(stat.diff, buf, 1);
            }
        }
    }
    // Compare files
    if (!CompareFiles(&stat, opt))
    {
        // Comparison terminated, g_abort_flag set
        return 0;
    }
    WAV_close_write(stat.diff);
    for (i = 0; i < 2; i++)
    {
        stat.remainingSamples[i] -= stat.samlpes_count;
    }
    
    if (!stat.samlpes_count)
    {
        //TODO - format this
        my_printf(_T("ERROR: no samples compared"));
        success = 0;
    }
    else
    {
        diff_stat_update_totals(&stat, &g_tot, opt->file_name[1]);
        
        // Output comparison result
        OUTPUT_print_file_stat(stat.file, &stat, opt);
        success = 1;
    }
    for (i = 0; i < 2; i++)
    {
        WAV_close_read(stat.file[i]);
    }
    return success;
}


static dir_scan_callback_action_t process_file_callback (const TCHAR * path, const TCHAR * path2, const TCHAR * pathDiff, dir_entry_t * fd, void * not_used)
{
    not_used = not_used;
    if (fd->is_folder)
    {
        if (!fd->size)
        {
            return E_DIR_CONTINUE;
        }

        my_printf(_T("SUBFOLDER: ------------------------------- %s -------------------------------\n"), path);
        if (pathDiff)
        {
            // Create difference sub-folder
            // The CreateDirectory() can be used here, but DIR_force_directory()
            // is used only to remove dependency on <windows.h>
            // CreateDirectory(pathDiff, NULL);
            // DIR_force_directory((char*)pathDiff);
            _tcscpy(g_lazy_output_dir, pathDiff);
        }
    }
    else
    {
        g_opt.file_name[0] = (TCHAR*)path;
        g_opt.file_name[1] = (TCHAR*)path2;
        g_opt.file_name[2] = (TCHAR*)pathDiff;
        g_tot.files_count++;
        g_tot.files_compared += RunCompare(&g_opt);
        g_current_file_size += fd->size;
        GAUGE_set_pos((double) (g_current_file_size) / g_total_file_size);
    }

    if (g_abort_flag)
    {
        my_printf(_T("WARNING: Comparison terminated (ESC key pressed)\n"), path);
    }
    return g_abort_flag?E_DIR_ABORT:E_DIR_CONTINUE;
}


int _tmain (int argc, TCHAR *argv[])
{
    static TDIR3_directory dir;
    int errorlevel = 1;

    // Read and verify command-line options
    if (argc == 1 || !read_cmdline_options(argc, argv, &g_opt) || !verify_cmdline_options(&g_opt))
    {
        usage();
        goto Cleanup;
    }

    // Prepare file list: file list is sorted by the file name
    DIR3_open(&dir, g_opt.file_name[0], g_opt.file_name[1], g_opt.file_name[2], NULL, NULL);
    if (!dir.dir.items_count)
    {
        _tprintf(_T("ERROR: not found %s\n"), g_opt.file_name[dir.swap_root_mirror]);
        goto Cleanup;
    }
    DIR_set_index(&dir.dir, DIR_new_index(&dir.dir, DIR_sort_names_descending, E_DIR_SORT_FILES_AFTER_FOLDER));
    
    // Init interface
    g_opt.is_single_file = dir.dir.is_single_file;
    OUTPUT_init(&g_opt);
    if (!dir.dir.is_single_file)
    {
        if (g_opt.file_name[1])
        {
            my_printf(_T("Comparing %s with %s\n"), g_opt.file_name[0], g_opt.file_name[1]);
        }
        else
        {
            my_printf(_T("Scanning %s\n"), g_opt.file_name[0]);
        }
        if (g_opt.file_name[2])
        {
            _tcscpy(g_lazy_output_dir, g_opt.file_name[2]);
        }
    }
    g_total_file_size = dir.dir.files_size;

    DIR3_for_each(&dir, process_file_callback, NULL);
    if (!dir.dir.is_single_file)
    {
        TCHAR status[100];
        _stprintf(status, _T("%u of %u files compared"), g_tot.files_compared, g_tot.files_count);
        OUTPUT_update_gauge_status(status, &g_tot);
    }

    // Set ERRORLEVEL = 1 if files not bit-exact or there was comparison errors
    errorlevel = g_tot.d_abs_max != 0 || g_tot.files_compared != g_tot.files_count;
    DIR3_close(&dir);
    OUTPUT_close(&g_opt, &g_tot);

Cleanup:
    ALIGN_close();
    assert(_CrtCheckMemory());
    return errorlevel;
}

