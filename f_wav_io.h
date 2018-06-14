/** 21.03.2011 @file
*  WAV file I/O for audio data in normalized floating-point format.
*/

#ifndef f_wav_io_H_INCLUDED
#define f_wav_io_H_INCLUDED

#include <stdio.h> 
#include <limits.h> 
//#include <tchar.h>
#include "type_tchar.h"
#include <wchar.h>


#ifdef __cplusplus
extern "C" {
#endif

#if defined (_MSC_VER)
typedef __int64  wavpos_t;
#else
typedef long long wavpos_t;
#endif

/**
*   PCM sample format
*/
enum pcm_data_type_e
{
    E_PCM_INTEGER = 1,                      //!< Integer PCM format
    E_PCM_IEEE_FLOAT = 3                    //!< IEEE floating-point PCM format
};

/**
*   Audio file format
*/
enum pcm_container_e
{
    EFILE_RAW = 0,                          //!< RAW PCM format
    EFILE_WAV                               //!< WAV PCM format
};

/**
*   PCM stream descriptor
*/
typedef struct
{
    unsigned long           hz;                 //!< Sample rate, hz
    unsigned int            ch;                 //!< Number of channels
    int                     bips;               //!< Bits per sample
    enum pcm_data_type_e    pcm_type;           //!< PCM Sample format
} pcm_format_t;

/**
*   Cue list
*/
typedef struct wav_cue_tag
{
    struct wav_cue_tag * next;
    unsigned pos_samples;
    unsigned len_samples;
    char text[1];
} wav_cue_t;

/**
*   Audio file descriptor
*/
typedef struct  
{
    FILE *                  file;               //!< FILE handle
    enum pcm_container_e    container;          //!< Container: RAW/WAV
    unsigned long           header_bytes;       //!< Initial data position (header size)
    wavpos_t                data_bytes;         //!< PCM data size, bytes
    pcm_format_t            fmt;
    TCHAR                   file_mode;
    wav_cue_t              *cue;
} wav_file_t;


/**
*   @return PCM format descriptor
*
*   Example: WAV_open_read("file.pcm", WAV_fmt(44100, 2, 32, E_PCM_IEEE_FLOAT));
*/
pcm_format_t WAV_fmt(unsigned long hz, int ch, int bips, enum pcm_data_type_e pcm_type);

/**
*   @return size of sample in bytes
*/
unsigned int WAV_bytes_per_sample(const wav_file_t *wf);

/**
*   @return symbolic name of the audio format. 
*/
const TCHAR * WAV_format_string (const wav_file_t *wf);


/************************************************************************/
/*                  WAV file reader functions                           */
/************************************************************************/


/**
*   Opens WAV or raw PCM file for reading. Fix data size for incomplete
*   WAV files, so that such files can be read with WAV_read_doubles()
*   @return 1 if successful, 0 otherwise
*/
wav_file_t * WAV_open_read (
    const TCHAR    *file_name,              //!< [IN] Input file name
    const pcm_format_t *raw_pcm_defaults    //!< [IN, opt] Default PCM data format
    );

/**
*   Close WAV file reader object
*/    
void WAV_close_read (
    wav_file_t *wfr                         //!< WAV file reader structure
);

/**
*   Read PCM data as doubles. Avoid reading of extra non-PCM chunks at the
*   end of WAV files. 
*   @return number of SAMPLES read: sample = double * wf->fmt.uiCh
*/
size_t WAV_read_doubles (
    wav_file_t *wfr,                        //!< WAV file reader structure
    double *buf,                            //!< [OUT] Buffer with data in the range [-1; +1)
    size_t samples_count                    //!< Number of samples in the buffer
);
size_t WAV_read_floats (
    wav_file_t *wfr,                        //!< WAV file reader structure
    float *buf,                             //!< [OUT] Buffer with data in the range [-1; +1)
    size_t samples_count                    //!< Number of samples in the buffer
);


/**
*   @return current read position in samples.
*/    
wavpos_t WAV_get_sample_pos(const wav_file_t *wf);

/**
*   @return number of remaining samples.
*/    
wavpos_t WAV_get_remaining_samples(const wav_file_t *wf);

/**
*   @return file size in samples
*/
wavpos_t WAV_samples_count(const wav_file_t * wf);

/**
*   @return number of samples, which fit into given number of bytes
*/
wavpos_t WAV_bytes_to_samples(const wav_file_t * wf, wavpos_t bytes);

/************************************************************************/
/*                  WAV file writer functions                           */
/************************************************************************/

/**
*   Open WAV file writer object.
*   @return 1 if successful, 0 otherwise
    wav_file_t * wf = WAV_open_write(name, WAV_fmt(44100, 1, 64, E_PCM_IEEE_FLOAT), EFILE_WAV);
*/
wav_file_t * WAV_open_write (
    const TCHAR    *file_name,              //!< [IN] Output file name
    const pcm_format_t raw_pcm_defaults,    //!< [IN] PCM data format
    enum pcm_container_e container
    );

/**
*   Close WAV file writer object.
*/    
void WAV_close_write (
    wav_file_t *wfw                         //!< WAV file writer structure
);

/**
*   Update WAV header and flush file data.
*/
void WAV_flush (
    wav_file_t *wfw                         //!< WAV file writer structure
);

/**
*   Write normalized floating-point data to the WAV file.
*   @return number of samples written
*
*   example:  WAV_write_doubles(wavfile, pcm_data, nsamples);
*/
size_t WAV_write_doubles (                  
    wav_file_t *wfw,                        //!< WAV file writer structure
    const double *buf,                      //!< [IN] Buffer with data in the range [-1; +1)
    size_t samples_count                    //!< Number of samples in the input buffer
);
size_t WAV_write_floats (
    wav_file_t *wfw,                        //!< WAV file writer structure
    const float *buf,                       //!< [IN] Buffer with data in the range [-1; +1)
    size_t samples_count                    //!< Number of samples in the input buffer
);

/**
*   Add cue mark at specified location
*
*   example:  WAV_cue_add(wavfile, position, range, "label");
*/
void WAV_cue_add(wav_file_t *wf, int pos_samples, int len_samples, const char * text);

/**
*   Add cue mark with printf()-like format spec
*
*   example:  WAV_cue_printf(wavfile, position, range, "level = %.1fdB", level_db);
*/
void WAV_cue_printf(wav_file_t *wf, int pos_samples, int len_samples, const char * text, ...);

/**
*   Save normalized floating-point data to the WAV file (mono, 44100 hz).
*   @return number of samples written
*
*   example:  WAV_save_doubles(pcm_data, nsamples, _T("output.wav"));
*/
size_t WAV_save_doublesEx (
    const double        *x,                 //!< [IN] Buffer with data in the range [-1; +1)
    size_t              size,               //!< samples to write
    const TCHAR         *file_name,         //!< [IN] WAV file name
    const pcm_format_t  fmt                 //!< PCM format
);
size_t WAV_save_doubles (
    const double        *x,                 //!< [IN] Buffer with data in the range [-1; +1)
    size_t              size,               //!< samples to write
    const TCHAR         *file_name           //!< [IN] WAV file name
);
size_t WAV_save_floatsEx (
    const float         *x,                 //!< [IN] Buffer with data in the range [-1; +1)
    size_t              size,               //!< samples to write
    const TCHAR         *file_name,         //!< [IN] WAV file name
    const pcm_format_t  fmt                 //!< PCM format
);
size_t WAV_save_floats (
    const float         *x,                 //!< [IN] Buffer with data in the range [-1; +1)
    size_t              size,               //!< samples to write
    const TCHAR         *file_name          //!< [IN] WAV file name
);



size_t WAV_save_doubles2Ex (
    const double        *x0,                //!< [IN] Buffer with left channel data in the range [-1; +1)
    const double        *x1,                //!< [IN] Buffer with right channle data in the range [-1; +1)
    int                 step,               //!< step between samples in the channes
    size_t              size,               //!< samples to write
    const TCHAR         *file_name,         //!< [IN] WAV file name
    const pcm_format_t  fmt                 //!< PCM format
    );
size_t WAV_save_doubles_stereo (
    const double        *x0,                //!< [IN] Buffer with left channel data in the range [-1; +1)
    const double        *x1,                //!< [IN] Buffer with right channle data in the range [-1; +1)
    int                 step,               //!< step between samples in the channes
    size_t              size,               //!< samples to write
    const TCHAR         *file_name          //!< [IN] WAV file name
    );

/**
*   Allocate and read WAV file data as normalized floating-point.
*   @return data buffer
*   Example:
    int size;
    pcm_format_t fmt;
    double * pcm = WAV_load_doubles(_T("signal.wav"), &size, &fmt);
*/
double * WAV_load_doubles (
    const TCHAR * file_name,                //!< [IN] WAV file name
    int * size,                             //!< [OUT] number of samples read (mono assumed!)
    pcm_format_t * fmt                      //!< [OUT, opt] PCM format
);

float * WAV_load_floats (
    const TCHAR * file_name,                //!< [IN] WAV file name
    int * size,                             //!< [OUT] number of samples read (mono assumed!)
    pcm_format_t * fmt                      //!< [OUT, opt] PCM format
);

/**
*   Append data to the WAV file with given name
*/
int WAV_append_doubles (
    const double        *pcm,               //!< [IN] Buffer with data in the range [-1; +1)
    size_t              nsamples,           //!< samples to write
    const TCHAR         *file_name          //!< [IN] WAV file name
);

int WAV_append_doubles_pair (
    const double        *pcm_l,             //!< [IN] Buffer with data in the range [-1; +1)
    const double        *pcm_r,             //!< [IN] Buffer with data in the range [-1; +1)
    int                 step,               //!< step between samples
    size_t              nsamples,           //!< samples to write
    const TCHAR         *file_name          //!< [IN] WAV file name
);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //f_wav_io_H_INCLUDED
