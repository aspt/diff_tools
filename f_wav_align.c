/** 18.12.2011 @file
*   Find "best match" offset between two PCM files, using FFT for fast convolution.
*/
#include "f_wav_align.h"

#include <math.h>
#include <string.h>
#include <float.h>
#include <assert.h>

#include <stdlib.h>
#include "dsp_ffttricl.h"

typedef real ccf_t;

static ccf_t        * g_fft_twid;
static ccf_t        * g_fft_input[2];
static double       * g_input[2];
static int          g_fft_size;
static unsigned int g_log_fft_size;
static int          g_max_offset;
static int          g_max_fft_size;


#define MIN_FFT_SIZE_LOG    10
#define MAX_FFT_SIZE_LOG    24
#define FREE(x)             if (x) {free(x); x = NULL;}
#define SQR(x)              ((x)*(x))
#define MAX( x, y )         ( (x)>(y)?(x):(y) )
#define MIN( x, y )         ( (x)<(y)?(x):(y) )

int ALIGN_init (unsigned int maxOffset, unsigned int maxCh)
{
    int i;
    int overheadFactor = 8;
    g_max_offset = maxOffset;
    g_log_fft_size = MIN_FFT_SIZE_LOG-1;
    if (maxCh > 6) overheadFactor = 4;
    if (maxCh > 12) overheadFactor = 2;
    do
    {
        g_fft_size = 1 << ++g_log_fft_size;
    } while (g_fft_size < (int)(overheadFactor*g_max_offset*maxCh) && 
             g_log_fft_size < MAX_FFT_SIZE_LOG);

    if (g_fft_size > g_max_fft_size)
    {
        ALIGN_close();
        g_max_fft_size = g_fft_size;

        g_fft_twid = malloc(sizeof(ccf_t) * g_fft_size);
        for (i = 0; i < 2; i ++)
        {
            g_input[i]    = malloc(sizeof(double) * g_fft_size);
            g_fft_input[i] = malloc(sizeof(ccf_t)  * g_fft_size);
            if (!g_input[i] || !g_fft_input[i] || !g_fft_twid)
            {
                ALIGN_close();
                return 0;
            }
        }
        tricl_fft_makelut(g_fft_twid, g_log_fft_size);
    }
    return 1;
}


void ALIGN_close (void)
{
    int i;
    for (i = 0; i < 2; i ++)
    {
        FREE(g_input[i]);
        FREE(g_fft_input[i]);
    }
    FREE(g_fft_twid);
    g_max_fft_size = 0;
}


static int bestMatch(const double * p0, size_t len0, const double * p1, double * pdelta, int ch)
{
    size_t i;
    int n = g_log_fft_size;
    size_t fftSize;
    const double * p0orig = p0; 
    const double * p1orig = p1;
    size_t max_offset = g_max_offset*ch;
    size_t len1;
    double minPwr;
    size_t minOff = 0;
    double pwr = 0;
    double ssd0, ssd1, ssd2;
    size_t off;

    len1 = len0/2;    // moving window = half of reference; 
    if (max_offset > len0*3/4)
    {
        max_offset = len0*3/4;
    }
    if (len1 + max_offset > len0)
    {
        len1 = len0 - max_offset;
    }

    while ((1ul << n >> 1) > len0 + len1)
    {
        n--;
    }

    fftSize = (size_t)1 << n;
    while (len0 + len1 > fftSize)
    {
        len0 /= 2;
        len1 /= 2;
    }

    //
    // |<=============p0===============>|
    // |<---max ofs---->|
    //                  |<=====p1======>|
    // avoid tails
    assert(max_offset + len1 <= len0);
    

    // |<------------------------fftSize----------------...>|
    // |<=============p0===============>|
    //                                  |<=====p1======>|
    // avoid aliasing: 
    assert(len0 + len1 <= fftSize);
    

    // Copy input data to FFT array with shuffle for tricl FFT
    for (i = 0; i < fftSize/2; i++) g_fft_input[0][i*2]   = (real)*p0++;
    for (i = 0; i < fftSize/2; i++) g_fft_input[0][i*2+1] = (real)*p0++;

    // Take only half of second signal (leave room for circular convolution)
    for (i = 0; i < fftSize; i++) g_fft_input[1][i] = 0;
    for (i = 0; i < MIN(len1, fftSize/2); i++)   g_fft_input[1][i*2]   = (real)*p1++;
    if (len1>fftSize/2)
    {
        for (i = 0; i < MIN(len1-fftSize/2, fftSize/2); i++)   g_fft_input[1][i*2+1] = (real)*p1++;
    }

    // Circular convolution using tricl FFT. Input and output are shuffled.
    tricl_fft_r2c(g_fft_input[0], n, g_fft_twid);
    tricl_fft_r2c(g_fft_input[1], n, g_fft_twid);
    tricl_fftconv_mulpr_conj(g_fft_input[0], g_fft_input[1], n);
    g_fft_input[0][0]/=2;
    g_fft_input[0][1]/=2;
    tricl_fft_c2r(g_fft_input[0], n, g_fft_twid);

    // Unshuffle output
    for (i = 0; i < fftSize/2; i++) g_fft_input[1][i]           = g_fft_input[0][i*2  ], 
                                    g_fft_input[1][i+fftSize/2] = g_fft_input[0][i*2+1];

    p0 = p0orig;
    p1 = p1orig;

    // min SSD (Sum of Squared Difference)
    // (a-b)^2 = a^2 + b^2 - 2*a*b
    // a^2 + b^2 == pwr
    for (i = 0; i < len1; i++) pwr += SQR(p0[i]) + SQR(p1[i]);
    minPwr = pwr;

    ssd0 = ssd1 = ssd2 = pwr;
    for (off = 0; off < max_offset+ch; off++, p0++)
    {
        if ((unsigned)off % ch == 0)
        {
            double errLinf = FLT_EPSILON*(16*n + 3)*pwr;
            double delta = pwr - 2*g_fft_input[1][off]/(fftSize/2);
            ssd0 = ssd1;
            ssd1 = ssd2;
            ssd2 = delta;
#if 0
            // Reference convolution
            {
                double ref_pwr=0;
                double ref_dif=0;
                double ref=0;
                for(i=0;i<len1;i++)
                { 
                    ref +=SQR (p0[i]-p1[i]);
                    ref_pwr +=SQR (p0[i])+SQR(p1[i]);
                    ref_dif +=p0[i]*p1[i];
                }

                printf("%5d \t%f \t%f  %f %f \t%f\t%f\t%f\n",off,ssd2,ref,pwr,-2*g_fft_input[1][off]/(fftSize/2), ref_pwr-2*ref_dif, ref_pwr, -2*ref_dif);
            }
#endif

            if (!off)
            {
                minPwr = ssd2;
            }
            else if (ssd1 <= ssd0 && ssd1 < ssd2 && ssd1 + errLinf < minPwr)
            {
                minPwr = ssd1;
                minOff = off-ch;
            }
        }
        pwr -= SQR(p0[0]);
        pwr += SQR(p0[len1]);
    }
    *pdelta = minPwr;
    return (int)minOff;
}


/**
*   Align two WAV files, by moving current file read position.
*/
void ALIGN_align_pair (wav_file_t * wf0, wav_file_t * wf1)
{
    int i;
    unsigned long offset;
    size_t samples[2] = {0,};
    double w0,w1;
    int offs0,offs1;
    size_t smpNeed = g_fft_size / wf0->fmt.ch;
    size_t smpZero = 0;
    long initialPos[2];
    wav_file_t * wf[2];
    wf[0] = wf0;
    wf[1] = wf1;

    for (i = 0; i < 2; i++)
    {
        initialPos[i] = ftell(wf[i]->file);
    }

    do
    {
        size_t smpZerox[2];
        for (i = 0; i < 2; i++)
        {
            size_t j;
            samples[i] -= smpZero;
            memmove(g_input[i], g_input[i] + smpZero * wf[i]->fmt.ch, samples[i] * wf[i]->fmt.ch * sizeof(g_input[i][0]));
            samples[i] += WAV_read_doubles(wf[i], g_input[i] + samples[i] * wf[i]->fmt.ch, smpNeed - samples[i]);

            // clear tail incomplete sample (required only for odd channels) 
            for (j = samples[i] * wf[i]->fmt.ch; j < (unsigned)g_fft_size; j++)
            {
                g_input[i][j] = 0;
            }
            for (j = 0; j < samples[i] * wf[i]->fmt.ch && !g_input[i][j]; j++) 
            {
            }
            smpZerox[i] = j / wf0->fmt.ch;
        }
        smpZero = MIN(smpZerox[0], smpZerox[1]);
    } while (smpZero);

    // Restore WAV file position
    for (i = 0; i < 2; i++)
    {
        fseek(wf[i]->file, initialPos[i], SEEK_SET);
    }

    if (!samples[0] || !samples[1])
    {
        // No non-zero samples: do not change position
        return;
    }   

    offs0 = bestMatch(g_input[0], samples[0]*wf[0]->fmt.ch, g_input[1], &w0, wf[0]->fmt.ch);
    offs1 = bestMatch(g_input[1], samples[1]*wf[0]->fmt.ch, g_input[0], &w1, wf[0]->fmt.ch);
    if (w1 < w0) 
    {
        offs0 = offs1; 
        wf[0] = wf[1];
    }
    offset = offs0 / (int) wf[0]->fmt.ch;
    if (offset >= samples[0])
    {
        offset = 0;
    }
    else
    {
        fseek(wf[0]->file, offset * WAV_bytes_per_sample(wf[0]), SEEK_CUR);
    }
    return;
}

