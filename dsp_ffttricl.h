/** 20.03.2008 @file
*  
*/

#ifndef dsp_ffttricl_H_INCLUDED
#define dsp_ffttricl_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus

#define __restrict
#define __unused

#include "type_real.h"


/**T
The function {\em tricl\_fft\_makelut}($LUT$, $n$) generates an FFT 
lookup table suitable for use in computing FFTs of length up to $2^n$.
The input $n$ must satisfy $0 \leq n \leq 29$, and $LUT$ must have 
space to store $2^n$ doubles (i.e., $2^{n + 3}$ bytes).
*/

void tricl_fft_makelut(real *, int);

/**T
The function {\em tricl\_fft\_fft}($DAT$, $n$, $LUT$) computes a length
$2^n$ in-place FFT on the values $z_k$ where $z_k = \textrm{DAT}_{2 k} +
\textrm{DAT}_{2 k + 1} i$, using the precomputed lookup table $LUT$, leaving
the output in a wacky order.  The input $n$ must satisfy $0 \leq n \leq 29$,
$DAT$ must be an array of $2^n$ complex values ($2^{n + 1}$ doubles), and
$LUT$ must be as created by {\em tricl\_fft\_makelut}($LUT$, $m$) for some
$m \geq n$.
*/
void tricl_fft_fft(real * __restrict, int, const real * __restrict);

/**T
The function {\em tricl\_fft\_ifft}($DAT$, $n$, $LUT$) computes an inverse
FFT corresponding to {\em tricl\_fft\_fft}; it takes its input in the wacky
order from the output of that function, and leaves its output in normal order.
*/
void tricl_fft_ifft(real * __restrict, int, const real * __restrict);
void tricl_fft_r2c(real * __restrict, int, const real * __restrict);
void tricl_fft_c2r(real * __restrict, int, const real * __restrict);

/**T
The function {\em tricl\_fftconv\_scale}($DAT$, $n$) multiplies the 
$2^n$ complex values ($2^{n + 1}$ doubles) stored in $DAT$ by $2^{-n}$.
*/
void tricl_fftconv_scale(real *, int);

/**T
The function {\em tricl\_fftconv\_mulpw}($DAT1$, $DAT2$, $n$) computes 
the product of $2^n$ pairs of complex values from $DAT1$ and $DAT2$ and
writes the resulting values into $DAT1$.
*/
void tricl_fftconv_mulpw(real * __restrict, real * __restrict, int);
void tricl_fftconv_mulpr(real * __restrict, real * __restrict, int);
void tricl_fftconv_mulpr_conj(real * __restrict DAT1, real * __restrict DAT2, int n);

/**T
The function {\em tricl\_fft\_sqrpw}($DAT$, $n$) squares $2^n$ complex
values from $DAT$ and writes the resulting values into $DAT$.
*/
void tricl_fftconv_sqrpw(real *, int);


// Return frequency-domain index of element i after complex transform with size = n
extern unsigned int fftfreq_c(unsigned int i, unsigned int n);

// Generates permutation table for complex transform
extern void fftfreq_ctable(unsigned int *,unsigned int);

// Return frequency-domain index of element i after real-to-complex transform with size = n
extern unsigned int fftfreq_r(unsigned int i, unsigned int n);

// Generates permutation table for real-to-complex transform
extern void fftfreq_rtable(unsigned int *,unsigned int);

// Scales real-to-complex transform in frequency domain, such that IFFT(Scale(FFT(x))) = x
void tricl_fft_r2c_scale(real * dat, int logn);

// Reorders input before real-to-complex transform, must be used!
real * tricl_fft_r2c_preproc(const real * dat, int logn, real * out);

// Reorders output after complex-to-real transform, must be used!
real * tricl_fft_c2r_postproc(const real * dat, int logn, real * out);

// 2D FFT
void tricl_fft2d(real * out, const real * inp, int logx, int logy, int w, int h,  const real * twid);

// 2D IFFT
void tricl_ifft2d(real * out, real * inp, int logx, int logy, int w, int h, const real * twid);

// 2D convolution helper
void tricl_fft2dconv_mulpw(real * __restrict DAT1, real * __restrict DAT2, int n);


typedef struct tricl_fft_real_spectr_t tricl_fft_real_spectr_t;
tricl_fft_real_spectr_t * tricl_fft_real_spectr_mem_alloc(int n);
real * tricl_fft_r2spec(tricl_fft_real_spectr_t * h, const real * x);
real * tricl_fft_r2power(tricl_fft_real_spectr_t * h, const real * x);
void tricl_fft_power2db(const real * x, real * db, int n, double dbm0, double floor);


void tricl_fft_bluestein(real * x, int xsize, int sign, real  * out);
void tricl_fft_bluestein_ex(real * x, int xsize, double freq_scalefactor, real  * out);

typedef struct tricl_fft_bluestein_t tricl_fft_bluestein_t;
tricl_fft_bluestein_t * tricl_fft_bluestein_alloc(int n);
void tricl_fft_bluestein_free(tricl_fft_bluestein_t * h);
void tricl_fft_bluestein_r2c(tricl_fft_bluestein_t * h, const real * x, real * out);
void tricl_fft_bluestein_c2r(tricl_fft_bluestein_t * h, const real * x, real * out);




typedef struct 
{
    int nfft;      // radix-2 transform size
    int log2nfft;  // 
    int ntime;
    int mfreq;
    real * lut;
    real * chirp_time1;
    real * chirp_time2;
    real * chirp_freq;
} tricl_fft_chirpz_t;

void tricl_fft_chirpz_free(tricl_fft_chirpz_t * h);
tricl_fft_chirpz_t * tricl_fft_chirpz_alloc(int n, int m, const real a[2], const real w[2], real freq_scalefactor);
void tricl_fft_chirpz(tricl_fft_chirpz_t * h, real * x, real  * out);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //dsp_ffttricl_H_INCLUDED
