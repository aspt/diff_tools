
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <direct.h>
#include <windows.h>
#include "f_wav_io.h"

#pragma warning (disable:4996) //warning C4996: 'mkdir': The POSIX name for this item is deprecated. Instead, use the ISO C++ conformant name: _mkdir
void GenSine(
    double * signal,
    double ampl,
    double k,
    int size
    )
{
//  Iterating the following two lines of code yields a circle!
//  x = x + y/k;
//  y = y - x/k;

    double x = ampl;
    double y = 0;
    while (--size >= 0)
    {
        *signal++ = x;
        x = x + y/k;
        y = y - x/k;
    }
}

void addNoise(
    double * signal,
    double ampl,
    int size
    )
{
    while (--size >= 0)
    {
        *signal++ += ((double)rand()/RAND_MAX-.5)*ampl;
    }
}

void addDC(
    double * signal,
    double ampl,
    int size
    )
{
    while (--size >= 0)
    {
        *signal++ += ampl;
    }
}

void addScale(
    double * signal,
    double ampl,
    int size
    )
{
    while (--size >= 0)
    {
        *signal++ *= ampl;
    }
}

void addOffset(
    double * signal,
    int offs,
    int size
    )
{
    memmove(signal+offs,signal, (size-offs)*sizeof(double));
    memset(signal,0, (offs)*sizeof(double));
}


#define SIZE 1024*128
enum
{
    I8,
    I16,
    I24,
    I32,
    F32,
    F64
};

const int bits_grade[] = {8,16,24,32,32,64};



static void _save(const char * path, double * s, int size, int ch, int grade, int dGrade)
{
    char p[100];
    pcm_format_t fmt;
    int bit_grade = grade + dGrade;
    if (bit_grade > F64 || bit_grade < I8) return;
    (void)mkdir(path);
    sprintf(p, "%s\\%c%02d_%d.wav", path, grade < F32?'i':'f', bits_grade[grade], ch);
    fmt.hz = 44100;
    fmt.ch = ch;
    fmt.bips = bits_grade[bit_grade];
    fmt.pcm_type = bit_grade < F32?E_PCM_INTEGER:E_PCM_IEEE_FLOAT;
    WAV_save_doublesEx(s, size/ch, p, fmt);
}

static void save(const char * path, double * s, int size)
{
    int g;
    int g_ch[] = {1,2,8,48};
    int c;
    for (c = 0; c < sizeof(g_ch)/sizeof(g_ch[0]); c++)
    {
        for (g = I8; g <= F64; g++)
        {
            _save(path, s, size, g_ch[c], g, 0);
        }
    }
}

static void save_d(const char * path, double * s, int size, int d)
{
    int g;
    int g_ch[] = {1,2,8,48};
    int c;
    for (c = 0; c < sizeof(g_ch)/sizeof(g_ch[0]); c++)
    {
        for (g = I8; g <= F64; g++)
        {
            _save(path, s, size, g_ch[c], g, d);
        }
    }
}


int main()
{
    static double z[SIZE];
    static double y[SIZE];
    GenSine(z, 1., 10, SIZE);

    save("ref\\", z, SIZE);
    memcpy(y,z,SIZE*sizeof(double));
    addNoise(y, pow(2,-15), SIZE);
    save("noise\\", y, SIZE);

    memcpy(y,z,SIZE*sizeof(double));
    addDC(y, pow(2,-15), SIZE);
    save("DC\\", y, SIZE);

    memcpy(y,z,SIZE*sizeof(double));
    addScale(y, 0.9999, SIZE);
    save("Scale\\", y, SIZE);

    memcpy(y,z,SIZE*sizeof(double));
    addOffset(y, 48*55, SIZE);
    addNoise(y, pow(2,-15), SIZE);
    save("offs\\", y, SIZE);

    save_d("dg-5\\", z, SIZE, -5);
    save_d("dg-4\\", z, SIZE, -4);
    save_d("dg-3\\", z, SIZE, -3);
    save_d("dg-2\\", z, SIZE, -2);
    save_d("dg-1\\", z, SIZE, -1);
    save_d("dg-0\\", z, SIZE,  0);
    save_d("dg+1\\", z, SIZE,  1);
    save_d("dg+2\\", z, SIZE,  2);
    save_d("dg+3\\", z, SIZE,  3);
    save_d("dg+4\\", z, SIZE,  4);
    save_d("dg+5\\", z, SIZE,  5);

    (void)mkdir("ref\\1");
    (void)mkdir("ref\\1\\1");
    (void)mkdir("ref\\2");
    (void)mkdir("ref\\2\\2");
    (void)mkdir("offs\\1");
    (void)mkdir("offs\\1\\1");
    (void)mkdir("offs\\2");
    (void)mkdir("offs\\2\\2");
    CopyFile("ref\\f32_1.wav", "ref\\2\\2\\f32_1.wav", FALSE);
    CopyFile("ref\\f32_2.wav", "ref\\2\\2\\f32_2.wav", FALSE);
    CopyFile("ref\\f32_48.wav", "ref\\2\\2\\f32_48.wav", FALSE);
    CopyFile("ref\\f32_8.wav", "ref\\2\\2\\f32_8.wav", FALSE);
    CopyFile("offs\\f32_1.wav", "offs\\2\\2\\f32_1.wav", FALSE);
    CopyFile("offs\\f32_2.wav", "offs\\2\\2\\f32_2.wav", FALSE);
    CopyFile("offs\\f32_48.wav", "offs\\2\\2\\f32_48.wav", FALSE);
    CopyFile("offs\\f32_8.wav", "offs\\2\\2\\f32_8.wav", FALSE);

    return 0;
}
