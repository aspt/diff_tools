/** 21.03.2011 @file
*
*   WAV file I/O for audio data in normalized [-1;+1) floating-point format.
*   - read from file to float-point buffer (single or double precision)
*   - save float-point buffer to file
*   - WAV or RAW PCM files supported
*   - 64 or 32-bit float-point PCM files supported
*   - 32, 24, 16 or 8-bit integer PCM files supported
*   - write cue marks to the file
*   
*   Examples:
*
*   Save double-precision float-point buffer to the file: 
*       WAV_save_doubles(pcm_data, nsamples, "output.wav");
*
*   Save double-precision float-point buffer to the CD-audio file.
*       WAV_save_doublesEx(pcm_data, nsamples, "output.wav", WAV_fmt(44100, 2, 16, E_PCM_INTEGER));
*       
*   Load WAV file to double-precision float-point buffer
*       int nsamples;
*       pcm_data = WAV_load_doubles("file.wav", &nsamples);
*
*/
#ifdef _MSC_VER
#   pragma warning (disable:4310)      // warning C4310: cast truncates constant value
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "f_wav_io.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>    // WAV_cue_printf

#define MIN(x,y) ((x)<(y) ? (x):(y))
#define ABS(x)   ((x)>=0 ? (x):-(x))


/************************************************************************/
/*      Portable 64-bit file_size()                                     */
/************************************************************************/
#if defined(_MSC_VER)
#include <io.h>
typedef __int64 filesize_t;
static filesize_t file_size64(FILE * f)
{
    return _filelengthi64(_fileno(f));
}
#if _MSC_VER < 1300
__int64 __cdecl _ftelli64(FILE *);
#endif
static filesize_t file_pos64(FILE * f)
{
    return _ftelli64(f);
}
#elif defined(__GNUC__) && !defined(__arm)
#include <sys/types.h>
#include <sys/stat.h> 
typedef long long filesize_t;
static filesize_t file_size64(FILE * f)
{
    struct stat stat;
    if (f && !fstat(fileno(f), &stat))
    {
        return stat.st_size;
    }
    return 0;
}
static filesize_t file_pos64(FILE * f)
{
    return ftello(f);
}
#elif defined _WIN32 
#include <windows.h>
#include <io.h>
typedef LONGLONG filesize_t;
static filesize_t file_size64(FILE * f)
{
    DWORD lo,hi;
    lo = GetFileSize((HANDLE)_get_osfhandle(_fileno(f)), &hi);
    return ((LONGLONG)hi << 32) | lo;
}
static filesize_t file_pos64(FILE * f)
{
    return ftell(f);
}
#else
typedef long filesize_t;
static filesize_t file_size64(FILE * f)
{
    long size, pos = ftell(f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, pos, SEEK_SET);
    return size;
}
static filesize_t file_pos64(FILE * f)
{
    return ftell(f);
}

#endif

/**
*   Utility: format constructor
*/
pcm_format_t WAV_fmt(unsigned long hz, int ch, int bips, enum pcm_data_type_e pcm_type)
{
    pcm_format_t fmt;
    fmt.hz = hz;
    fmt.ch = ch;
    fmt.bips = bips;
    fmt.pcm_type = pcm_type;
    return fmt; 
}

/**
*   @return size of sample in bytes
*/
unsigned int WAV_bytes_per_sample(const wav_file_t *wf)
{
    return wf->fmt.ch * ABS(wf->fmt.bips) / CHAR_BIT;
}

/**
*   @return symbolic name of the audio format. 
*/
const TCHAR * WAV_format_string (const wav_file_t *wf)
{
    static const TCHAR *g_aFormatNames[] =
    {
       _T("RAW"), _T("WAV")
    };
    return g_aFormatNames[wf->container];
}

/************************************************************************/
/*      WAV header read                                                 */
/************************************************************************/
#define READ_4(x)     {x = 0; if (fread(&x, 1, 4, wf->file) != 4) goto l_fail;}
#define READ_2(x)     {x = 0; if (fread(&x, 1, 2, wf->file) != 2) goto l_fail;}
#define SKIP_BYTES(x) fseek(wf->file, x, SEEK_CUR)
// Error detection (verify that parser within claimed file size)
#define DECREMENT_SIZE(n) {if (n >= remaining_bytes) goto l_fail; remaining_bytes -= n;}

/**
*   Opens file and parse WAV header. 
*   Opened file position set to the beginning of the audio data.
*
*   return  1 if success, 0 if fail
*/
static int wav_read_header(wav_file_t * wf)
{
    unsigned long  remaining_bytes;
    unsigned long  tmp32;
    unsigned short tmp16;
    int            is_format_tag_found = 0;
    
    // Verify file header
    READ_4(tmp32);                          // RiffHeader.Magic    ('RIFF')
    if (tmp32 != 0x46464952ul) 
    {
        goto l_fail;
    }
    READ_4(remaining_bytes);                // RiffHeader.FileSize (ignored)
    READ_4(tmp32);                          // RiffHeader.Type     ('WAVE')
    if (tmp32 != 0x45564157ul) 
    {
        goto l_fail;
    }
    DECREMENT_SIZE(4);
    
    // Loop through chunks until 'data' is found
    // should allow chunks between fmt and data (al_sbr_cm_96_5.wav)
    while (remaining_bytes >= 8)
    {
        unsigned short wFormatTag;
        unsigned long chunk_size;
        READ_4(tmp32);                      // Chunk.Id ('fmt ' or 'data')
        READ_4(chunk_size);                 // ChunkFmt.Size
        if (tmp32 == 0x20746D66ul)          // 'fmt ' chunk found: retrieve audio info
        {
            if (chunk_size < 16) 
            {
                goto l_fail;                // too small 'fmt ' chunk size 
            }
            READ_2(wFormatTag);             // Format.wFormatTag (1 for PCM)
            if (wFormatTag == 3)
            {
                wf->fmt.pcm_type = E_PCM_IEEE_FLOAT;
            }
            else if (wFormatTag == 1 || wFormatTag == 0xFFFE)
            {
                wf->fmt.pcm_type = E_PCM_INTEGER;
            }
            else
            {
                goto l_fail;                // Not PCM data.
            }
            READ_2(tmp16);                  // Format.nChannels
            wf->fmt.ch = (unsigned int)tmp16;
            READ_4(tmp32);                  // Format.nSamplesPerSec
            wf->fmt.hz = tmp32;
            READ_4(tmp32);                  // Format.nAvgBytesPerSec (ignored)
            READ_2(tmp16);                  // Format.nBlockAlign     (ignored)
            READ_2(tmp16);                  // Format.BitsPerSample
            wf->fmt.bips = (unsigned int)tmp16;
            DECREMENT_SIZE(16);
            chunk_size -= 16;               // Skip the rest of 'fmt ' chunk
            is_format_tag_found = 1;
        }
        else if (tmp32 == 0x61746164ul)     // 'data' chunk magic
        {
            if (!is_format_tag_found)
            {
                goto l_fail;                // 'data' chunk before 'fmt ' chunk
            }
            wf->data_bytes = chunk_size;
            wf->header_bytes = ftell(wf->file);
            wf->container = EFILE_WAV;
            return 1;
        }
        SKIP_BYTES(chunk_size);             // Skip unknown chunk
        DECREMENT_SIZE(8 + chunk_size);
    }
l_fail:
    return 0;
}

/************************************************************************/
/*      WAV header write                                                */
/************************************************************************/

// Standard WAV header size
#define WAV_HEADER_SIZE 44

/**
*   Endian-independent byte-write macros
*/
#define WR(x, n) *p++ = (char)(((x) >> 8*n) & 255)
#define WRITE_2(x) WR(x,0); WR(x,1);
#define WRITE_4(x) WR(x,0); WR(x,1); WR(x,2); WR(x,3);

/**
*   Writes standard WAV header in the beginning of the file.
*   return 1 in case of success, or 0 in case of write error or invalid params.
*/
static int wav_write_header(
    FILE * file,                //!< FILE handle
    unsigned long hz,           //!< Sample rate value (for ex. 44100)
    unsigned int  ch,           //!< Number of channels (1,2...)
    unsigned int  bips,         //!< Number of bits per sample (8,16...)
    unsigned long data_size,    //!< PCM data size, excluding headers
    unsigned long file_size,    //!< Total file size in bytes, incl 44-bytes header
    int           format_tag    //!< WAV format tag
    )
{
    int success = 0;
    if (file)
    {
        unsigned long nAvgBytesPerSec = bips * ch * hz >> 3;
        unsigned int nBlockAlign      = bips * ch >> 3;
        char hdr[WAV_HEADER_SIZE];
        char * p = hdr;

        WRITE_4(0x46464952);                   //  0: RiffHeader.Magic = 'RIFF'
        WRITE_4(file_size - 8);                //  4: RiffHeader.FileSize = File size - 8
        WRITE_4(0x45564157);                   //  8: RiffHeader.Type = 'WAVE'
        WRITE_4(0x20746D66);                   //  C: ChunkFmt.Id = 'fmt ' (format description)
        WRITE_4(16L);                          // 10: ChunkFmt.Size = 16   (descriptor size)
        WRITE_2(format_tag);                   // 14: Format.wFormatTag    (see E_WAV_tag type)
        WRITE_2(ch);                           // 16: Format.ch
        WRITE_4(hz);                           // 18: Format.nSamplesPerSec
        WRITE_4(nAvgBytesPerSec);              // 1C: Format.nAvgBytesPerSec
        WRITE_2(nBlockAlign);                  // 20: Format.nBlockAlign
        WRITE_2(bips);                         // 22: Format.BitsPerSample
        WRITE_4(0x61746164);                   // 24: ChunkData.Id = 'data'
        WRITE_4(data_size);                    // 28: ChunkData.Size = File size - 44
                                               //     Total size: 0x2C (44) bytes
        fseek(file, 0, SEEK_SET);              // no rewind() in WinCE
        success = (int)fwrite(hdr, WAV_HEADER_SIZE, 1, file);
    }
    return success;
}

/**
*   Update WAV file header with actual file size, and set file write 
*   position to file end
*/
static int wav_update_header(wav_file_t *wf)
{
    int success;
    long pos;
    //fseek(wf->file, 0, SEEK_END);
    pos = ftell(wf->file);      // Assume file pos is after last write op

    if (wf->cue) 
    {
        // http://www.sonicspot.com/guide/wavefiles.html#cue
        char * hdr, * p;
        wav_cue_t * cue = wf->cue;
        int i = 0, cue_count = 0;
        size_t text_bytes = 0;
        do 
        {
            cue_count++;
            text_bytes += strlen(cue->text) + 1;
            if (text_bytes & 1) text_bytes++;
        } while(NULL != (cue = cue->next));

        hdr = (char*)malloc(12 + 24*cue_count + 12 + 40*cue_count + text_bytes);
        if (!hdr)
        {
            return 0;
        }
        p = hdr;
                                                // Cue Chunk Format 
        WRITE_4(0x20657563);                    //  0: Chunk ID = 'cue ' 
        WRITE_4(4 + 24*cue_count);              //  4: Chunk Data Size
        WRITE_4(cue_count);                     //  8: Num Cue Points

        cue = wf->cue;
        do
        {                                       // Cue Point Format
            i++;
            WRITE_4(i);                         //  0: ID
            WRITE_4(cue->pos_samples);          //  4: Position
            WRITE_4(0x61746164);                //  8: Data Chunk ID = 'data'
            WRITE_4(0);                         //  C: Chunk Start
            WRITE_4(0);                         // 10: Block Start
            WRITE_4(cue->pos_samples);          // 14: Sample Offset
        } while(NULL != (cue = cue->next));

                                                // Associated Data List Chunk Format 
        WRITE_4(0x5453494C);                    //  0: Chunk ID = 'LIST' 
        WRITE_4(4 + 40*cue_count + text_bytes); //  4: Chunk Data Size
        WRITE_4(0x6C746461);                    //  8: Type ID = 'adtl'

        cue = wf->cue;
        i = 0;
        do
        {
            size_t len = strlen(cue->text) + 1;
            if (len & 1) len++;
            ++i;
                                                // Label Chunk Format 
            WRITE_4(0x7478746C);                //  0: Chunk ID  = 'ltxt'
            WRITE_4(20);                        //  4: Chunk Data Size
            WRITE_4(i);                         //  8: Cue Point ID
            WRITE_4(cue->len_samples);          //  C: Sample Length
            WRITE_4(0x206E6772);                // 10: Purpose ID: 'rgn '
            WRITE_2(0);                         // 14: Country
            WRITE_2(0);                         // 16: Language
            WRITE_2(0);                         // 18: Dialect
            WRITE_2(0);                         // 1A: Code Page

            //WRITE_4(0x65746F6E);              //  0: Chunk ID  = 'note'
            WRITE_4(0x6C62616C);                //  0: Chunk ID  = 'labl'
            WRITE_4(4+len);                     //  4: Chunk Data Size
            WRITE_4(i);                         //  8: Cue Point ID
            strcpy(p, cue->text);
            p += len;
        } while(NULL != (cue = cue->next));


        fwrite(hdr, 12 + 24*cue_count + 12 + 40*cue_count + text_bytes, 1, wf->file);
        free(hdr);
    }

    success = wav_write_header(wf->file, wf->fmt.hz, wf->fmt.ch, wf->fmt.bips, pos - WAV_HEADER_SIZE, ftell(wf->file), wf->fmt.pcm_type);

    fseek(wf->file, pos, SEEK_SET);
    return success;
}


/**
*   @return number of samples, which fit into given number of bytes
*/
wavpos_t WAV_bytes_to_samples(const wav_file_t * wf, wavpos_t bytes)
{
    return bytes / WAV_bytes_per_sample(wf);
}

/**
*   @return file size in samples
*/
wavpos_t WAV_samples_count(const wav_file_t * wf)
{
    return WAV_bytes_to_samples(wf, wf->data_bytes);
}

/**
*   @return current position in samples
*/
wavpos_t WAV_get_sample_pos(const wav_file_t *wf)
{
    return WAV_bytes_to_samples(wf, file_pos64(wf->file) - wf->header_bytes);
}

/**
*   @return remained data size in samples
*/
wavpos_t WAV_get_remaining_samples(const wav_file_t *wf)
{
    return WAV_bytes_to_samples(wf, wf->data_bytes + wf->header_bytes - file_pos64(wf->file));
}

/************************************************************************/
/*                         Data conversion functions                    */
/************************************************************************/

/**
*   Convert normalized floating-point data to integer PCM data.
*/
static void wav_pack_IEEE_to_int (
    const void *input,      //!< [IN] Buffer with data in the range [-1; +1)
    int is_double,          //!< float or double input
    size_t size,            //!< Number of elements in the input buffer
    int bits_per_sample,    //!< Bits per sample for output buffer
    void *output            //!< [OUT] Output buffer (PCM data)
)
{
    unsigned int i;
    char *dst  = (char *)output;
    double scale = ldexp(1, bits_per_sample - 1);

    assert(bits_per_sample > 0);

    for (i = 0; i < size; i++)
    {
        int     j;
        long    tmp;
        double  val;
        if (is_double)
            val = scale * ((double*)input)[i];
        else
            val = scale * ((float*)input)[i];

        val += 0.5;

        if (val > scale - 1)
        {
            val = scale - 1;
        }
        else if (val < -scale)
        {
            val = -scale;
        }
        tmp = (long) floor(val); 

        if (bits_per_sample == 8)     // unsigned PCM for 8-bit WAV's
        {
            tmp += 128;
        }

        for (j = 0; j < bits_per_sample / CHAR_BIT; j++)
        {
            *dst++ = ((char *) &tmp) [j];
        }
    }
}

/**
*   Convert integer PCM data to normalized floating-point data in-place.
*/
static void wav_int_to_IEEE (
    void *buf,              //!< [IN/OUT] Buffer for in-place conversion 
    size_t size,            //!< Number of elements in the input buffer
    int bits_per_sample,    //!< Bits per sample for input data buffer
    int is_double
)
{
    unsigned char * src = (unsigned char *) buf + ABS(bits_per_sample) * size / CHAR_BIT;
//    long   tmp;
    int   tmp;
    int    sizeof_sample = ABS(bits_per_sample) / CHAR_BIT;
//    int    shift        = (CHAR_BIT*sizeof(long)) - ABS(bits_per_sample);
    int    shift        = 32 - ABS(bits_per_sample);
    double scale        = ldexp(1, 1 - ABS(bits_per_sample));

    if (size)
    {
        if (bits_per_sample < 0)
        {
            // Read big-endian data
            do 
            {
                size--;
                src -= sizeof_sample;
                tmp = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | (src[3]);
                tmp >>= shift;
                if (is_double)
                {
                    ((double *) buf)[--size] = (double) tmp * scale;
                }
                else
                {
                    ((float *) buf)[--size] = (float) (tmp * scale);
                }
            } while(size);
        }
        else
        {
            // Read little-endian data
            do 
            {
                src -= sizeof_sample;
                //tmp = *(long *) src;
//tmp = *(volatile int *) src;
tmp = src[0] + 256*(src[1] + 256*(src[2] + 256*src[3]));
//printf("%d %p\n", tmp, src);
                tmp = tmp << shift >> shift;
                if (bits_per_sample == 8)        // unsigned PCM for 8-bit WAV's
                {
                    tmp = (tmp&255) - 128;
                }
                if (is_double)
                {
                    ((double *) buf)[--size] = (double) tmp * scale;
                }
                else
                {
                    ((float *) buf)[--size] = (float) (tmp * scale);
                }
            } while(size);
        }
    }
}

/**
*   Convert doubles to floats
*/
static void wav_double_to_float(const double *src, float *dst, size_t size)
{
    while(size-- > 0)
    {
        *dst++ = (float)*src++;
    }
}

/**
*   Convert floats to doubles
*/
static void wav_float_to_double(const float *src, double *dst, size_t size)
{
    src += size; dst += size;
    while(size-- > 0)
    {
        *--dst = *--src;
    }
}

/**
*   Allocate and initialize wav_file_t object
*/
static wav_file_t * wav_ctor(const TCHAR * file_name, const TCHAR * mode) 
{
    wav_file_t * wf = NULL;
    if (file_name)
    {
        wf = calloc(1, sizeof(wav_file_t));
    }
    if (wf) 
    {
        wf->file = _tfopen(file_name, mode);
        if (!wf->file)
        {
            free(wf);
            wf = NULL;
        }
    }
    return wf;
}


/************************************************************************/
/*                  WAV file reader functions                           */
/************************************************************************/

/**
*   Opens WAV or raw PCM file for reading. Fix data size for incomplete
*   WAV files, so that such files can be read with WFR_readIEEEDoubles()
*   @return 1 if successful, 0 otherwise
*/
wav_file_t * WAV_open_read (
    const TCHAR *file_name,                //!< [IN] Input file name
    const pcm_format_t *raw_pcm_defaults   //!< [IN, opt] Default PCM data format
)
{
    wav_file_t * wf = wav_ctor(file_name, _T("rb"));
    if (!wf)
    {
        return NULL;
    }
    // Try to open file as a WAV
    if (wav_read_header(wf))
    {
        // WAV file
        int valid_format;
        wavpos_t file_size;
        wf->container = EFILE_WAV;

        if (wf->fmt.pcm_type == E_PCM_IEEE_FLOAT && wf->fmt.bips < 32)
        {
            // Override sample type: same behavior as CoolEdit for 16-bit float-point files
            wf->fmt.pcm_type = E_PCM_INTEGER;
        }

        valid_format = wf->fmt.ch != 0 && wf->fmt.bips != 0 && wf->fmt.hz != 0;
        if (wf->fmt.pcm_type == E_PCM_IEEE_FLOAT && wf->fmt.bips != 32 && wf->fmt.bips != 64)
        {
            valid_format = 0;
        }

        if (valid_format)
        {
            // WAV header is present, but format is not valid.
            // pretend that this file is RAW PCM
            if (raw_pcm_defaults)
            {
                wf->container = EFILE_RAW;
                wf->fmt = *raw_pcm_defaults;
            }
            else
            {
                WAV_close_read(wf);
                return NULL;
            }
        }

        wf->header_bytes = ftell(wf->file);
        file_size = file_size64(wf->file);
        
        if (!wf->data_bytes || file_size < wf->data_bytes + wf->header_bytes)
        {
            // fix bad WAV header to fit actual file size
            if (file_size > wf->header_bytes)
            {
                wf->data_bytes = file_size - wf->header_bytes;
            }
            else
            {
                wf->data_bytes = 0;
            }
        }
    }
    else if (raw_pcm_defaults)
    {
        // Assume raw PCM file
        rewind(wf->file);
        wf->container = EFILE_RAW;
        wf->fmt = *raw_pcm_defaults;
        wf->data_bytes = file_size64(wf->file);
        wf->header_bytes = 0;
    }
    else
    {
        WAV_close_read(wf);
        return NULL;
    }

    return wf;
}

/**
*   Close WAV file reader object
*/    
void WAV_close_read (wav_file_t *wf)
{
    if (wf)
    {
        if (wf->file)
        {
            fclose(wf->file);
        }
        free(wf);
    }
}

/**
*   Read PCM data as doubles. Avoid reading of extra non-PCM chunks at the
*   end of WAV files. 
*   @return number of SAMPLES read: sample = double * wf->fmt.uiCh
*/
static size_t wav_read_IEEE (
    wav_file_t *wf,        //!< WAV file reader structure
    void *out_buf,         //!< [OUT] Buffer with data in the range [-1; +1)
    size_t samples_count,  //!< Number of samples in the buffer
    int is_double
)
{
    size_t samples_read = 0;

    if (!wf)
    {
        return 0;
    }

    if (wf->file)
    {
        void * work_buf = out_buf;
        wavpos_t samples_remaining;
        if (!is_double && wf->fmt.pcm_type == E_PCM_IEEE_FLOAT && wf->fmt.bips > 32)
        {
            // read from double-precision to float: allocate work buffer
            work_buf = malloc(samples_count*wf->fmt.ch*sizeof(double));
            if (!work_buf) 
            {
                return 0;
            }
        }

        samples_remaining = WAV_get_remaining_samples(wf);
        samples_read = (size_t)MIN((wavpos_t)samples_count, samples_remaining);
        samples_read = fread(work_buf, WAV_bytes_per_sample(wf), samples_read, wf->file);
        if (wf->fmt.pcm_type == E_PCM_IEEE_FLOAT) 
        {
            if (is_double && wf->fmt.bips == 32)
            {
                wav_float_to_double(work_buf, work_buf, samples_read * wf->fmt.ch);
            }
            else if (!is_double && wf->fmt.bips == 64)
            {
                wav_double_to_float(work_buf, out_buf, samples_read * wf->fmt.ch);
            }
        }
        else
        {
            wav_int_to_IEEE(out_buf, samples_read * wf->fmt.ch, wf->fmt.bips, is_double);
        }
        if (samples_read < samples_count)
        {
            if (is_double)
            {
                memset((double*)out_buf + samples_read * wf->fmt.ch, 0, 
                    (samples_count - samples_read) * wf->fmt.ch * sizeof(double));
            }
            else
            {
                memset((float*)out_buf + samples_read * wf->fmt.ch, 0, 
                    (samples_count - samples_read) * wf->fmt.ch * sizeof(float));
            }
        }
        if (work_buf != out_buf)
        {
            free(work_buf);
        }
    }

    return samples_read;
}


/************************************************************************/
/*                  WAV file writer functions                           */
/************************************************************************/

static wav_file_t * wav_open_write_or_append (
    wav_file_t * wf,                        //!< [IN] Output file name
    const pcm_format_t raw_pcm_defaults,    //!< [IN] PCM data format
    enum pcm_container_e container
    )
{
    if (!wf)
    {
        return NULL;
    }
    if (container == EFILE_WAV)
    {
        if (!wav_write_header(wf->file, 0, 0, 0, 0, WAV_HEADER_SIZE, 1))
        {
            WAV_close_write(wf);
            return NULL;
        }
    }
    
    wf->container = container;
    wf->fmt = raw_pcm_defaults;
    wf->data_bytes = 0;
    wf->header_bytes = ftell(wf->file);
    
    return wf;
}

/**
*   Open WAV file writer object.
*   @return 1 if successful, 0 otherwise
*/
wav_file_t * WAV_open_write (
    const TCHAR *file_name,               //!< [IN] Output file name
    const pcm_format_t raw_pcm_defaults,  //!< [IN] PCM data format
    enum pcm_container_e container
)
{
    return wav_open_write_or_append(wav_ctor(file_name, _T("wb")), raw_pcm_defaults, container);
}


/**
*   Close WAV file writer object.
*/
void WAV_close_write(wav_file_t *wf)
{
    if (wf)
    {
        if (wf->file)
        {
            if (wf->container == EFILE_WAV)
            {
                wav_update_header(wf);
            }
            fclose(wf->file);
        }
        while (wf->cue) 
        {
            void * t = wf->cue->next;
            free(wf->cue);
            wf->cue = t;
        }
        free(wf);
    }
}

/**
*   Update WAV header and flush file data.
*/
void WAV_flush(wav_file_t *wf)
{
    if (wf && wf->file)
    {
        if (wf->container == EFILE_WAV)
        {
            wav_update_header(wf);
        }
        fflush(wf->file);
        //fseek(wf->file, 0, SEEK_END);
    }
}

/**
*   Add cue mark at specified location
*/
void WAV_cue_add(wav_file_t *wf, int pos_samples, int len_samples, const char * text)
{
    if (wf)
    {
        wav_cue_t * cue = wf->cue;
        if (cue && (unsigned)pos_samples == cue->pos_samples + cue->len_samples && !strcmp(cue->text, text))
        {
            cue->len_samples += len_samples;
        }
        else
        {
            cue = (wav_cue_t *)malloc(sizeof(wav_cue_t) + strlen(text));
            if (cue) 
            {
                strcpy(cue->text, text);
                cue->pos_samples = pos_samples;
                cue->len_samples = len_samples;
                cue->next = wf->cue;
                wf->cue = cue;
            }
        }
    }
}

/**
*   Add cue mark with printf()-like format spec
*/
void WAV_cue_printf(wav_file_t *wf, int pos_samples, int len_samples, const char * text,...)
{
    char cue[1024];
    va_list va;
    va_start(va, text);
#ifdef _MSC_VER
    _vsnprintf(cue, 1024, text, va);
#else
    vsnprintf(cue, 1024, text, va);
#endif
    WAV_cue_add(wf, pos_samples, len_samples, cue);
}


/**
*   Write normalized floating-point data to the WAV file.
*   @return number of samples written
*/
static size_t wav_write_IEEE (
    wav_file_t *wf,         //!< WAV file writer structure
    const void *in_buf,     //!< [IN] Buffer with data in the range [-1; +1)
    size_t samples_count,   //!< Number of samples in the input buffer
    int is_double
)
{
    size_t samples_written = 0;

    if (!wf)
    {
        return 0;
    }

    assert((unsigned) wf->fmt.bips <= 64);
    assert(wf->fmt.bips % CHAR_BIT == 0);

    if (wf->file)
    {
        void   *pcm_buf;
        if (wf->fmt.pcm_type == E_PCM_IEEE_FLOAT && wf->fmt.bips == (is_double?64:32))
        {
            pcm_buf = (void*)in_buf;
        }
        else
        {
            pcm_buf = malloc(samples_count * WAV_bytes_per_sample(wf));
        }
        
        if (pcm_buf)
        {
            switch (wf->fmt.pcm_type)
            {
                case E_PCM_INTEGER:
                    wav_pack_IEEE_to_int(in_buf, is_double, samples_count * wf->fmt.ch, wf->fmt.bips, pcm_buf);
                    break;
                case E_PCM_IEEE_FLOAT:
                    if (pcm_buf != in_buf)
                    {
                        if (is_double)
                        {
                            wav_double_to_float(in_buf, pcm_buf, samples_count * wf->fmt.ch);
                        }
                        else
                        {
                            wav_float_to_double(in_buf, pcm_buf, samples_count * wf->fmt.ch);
                        }
                    }
                    break;
            }
            samples_written = fwrite(pcm_buf, WAV_bytes_per_sample(wf), samples_count, wf->file);
            wf->data_bytes += samples_written * WAV_bytes_per_sample(wf);
            if (pcm_buf != in_buf)
            {
                free(pcm_buf);
            }
        }
    }

    return samples_written;
}

/************************************************************************/
/*   Wrappers for wav_write_IEEE() & wav_read_IEEE()                    */
/************************************************************************/

size_t WAV_write_doubles(wav_file_t *wf, const double *buf, size_t samples_count)
{
    return wav_write_IEEE(wf, buf, samples_count, 1);
}

size_t WAV_write_floats(wav_file_t *wf, const float *buf, size_t samples_count)
{
    return wav_write_IEEE(wf, buf, samples_count, 0);
}

size_t WAV_read_doubles(wav_file_t *wf, double *buf, size_t samples_count)
{
    return wav_read_IEEE(wf, buf, samples_count, 1);
}

size_t WAV_read_floats(wav_file_t *wf, float *buf, size_t samples_count)
{
    return wav_read_IEEE(wf, buf, samples_count, 0);
}

/**
*   Save normalized floating-point data to the WAV file (mono, 44100 hz).
*   @return number of samples written
*/
size_t WAV_save_doublesEx (
    const double *x,                        //!< [IN] Buffer with data in the range [-1; +1)
    size_t size,                            //!< samples to write
    const TCHAR *file_name,                 //!< [IN] WAV file name
    const pcm_format_t fmt                  //!< PCM format
)
{
    wav_file_t * wf = WAV_open_write(file_name, fmt, EFILE_WAV);
    if (wf)
    {
        WAV_write_doubles(wf, x, size);
        WAV_close_write(wf);
        return size;
    }
    return 0;
}

size_t WAV_save_doubles (
    const double *x,                        //!< [IN] Buffer with data in the range [-1; +1)
    size_t size,                            //!< samples to write
    const TCHAR *file_name                  //!< [IN] WAV file name
)
{
    return WAV_save_doublesEx(x, size, file_name, WAV_fmt(44100, 1, 64, E_PCM_IEEE_FLOAT));
}


size_t WAV_save_doubles2Ex (
    const double        *x0,                //!< [IN] Buffer with left channel data in the range [-1; +1)
    const double        *x1,                //!< [IN] Buffer with right channle data in the range [-1; +1)
    int                 step,               //!< step between samples in the channes
    size_t              size,               //!< samples to write
    const TCHAR         *file_name,         //!< [IN] WAV file name
    const pcm_format_t  fmt                 //!< PCM format
    )
{
    wav_file_t * wf = WAV_open_write(file_name, fmt, EFILE_WAV);
    double * pcm = (double *)malloc(2*size*sizeof(double));
    if (wf && pcm)
    {
        size_t i;
        for (i = 0; i < size; i++)
        {
            pcm[2*i+0] = x0[i*step];
            pcm[2*i+1] = x1[i*step];
        }
        WAV_write_doubles(wf, pcm, size);
        WAV_close_write(wf);
        return size;
    }
    return 0;
}

size_t WAV_save_doubles_stereo (
    const double        *x0,                //!< [IN] Buffer with left channel data in the range [-1; +1)
    const double        *x1,                //!< [IN] Buffer with right channle data in the range [-1; +1)
    int                 step,               //!< step between samples in the channes
    size_t              size,               //!< samples to write
    const TCHAR         *file_name          //!< [IN] WAV file name
    )
{
    return WAV_save_doubles2Ex(x0, x1, step, size, file_name, WAV_fmt(44100, 2, 64, E_PCM_IEEE_FLOAT));
}







size_t WAV_save_floatsEx (
    const float *x,                        //!< [IN] Buffer with data in the range [-1; +1)
    size_t size,                            //!< samples to write
    const TCHAR *file_name,                 //!< [IN] WAV file name
    const pcm_format_t fmt                  //!< PCM format
)
{
    wav_file_t * wf = WAV_open_write(file_name, fmt, EFILE_WAV);
    if (wf)
    {
        WAV_write_floats(wf, x, size);
        WAV_close_write(wf);
        return size;
    }
    return 0;
}

size_t WAV_save_floats (
    const float *x,                        //!< [IN] Buffer with data in the range [-1; +1)
    size_t size,                            //!< samples to write
    const TCHAR *file_name                  //!< [IN] WAV file name
)
{
    return WAV_save_floatsEx(x, size, file_name, WAV_fmt(44100, 1, 32, E_PCM_IEEE_FLOAT));
}

static void * wav_load (
    const TCHAR * file_name,                //!< [IN] WAV file name
    int * size,                             //!< [OUT] number of samples read (mono assumed!)
    pcm_format_t * fmt,                     //!< [OUT, opt] PCM format
    int isdouble
)
{
    wav_file_t * w;
    void * buf = 0;
    *size = 0;
    w = WAV_open_read(file_name, NULL);
    if (!w)
    {
        pcm_format_t default_fmt = WAV_fmt(44100, 1, 16, E_PCM_INTEGER);
        w = WAV_open_read(file_name, &default_fmt);
    }
    if (w)
    {
        size_t samples = (size_t)WAV_samples_count(w);
        buf = malloc(samples * (isdouble?sizeof(double):sizeof(float)) * w->fmt.ch);
        if (buf)
        {
            wav_read_IEEE(w, buf, samples, isdouble);
            *size = (int)(samples * w->fmt.ch);
        }
        if (fmt)
        {
            *fmt = w->fmt;
        }
        WAV_close_read(w);
    }
    return buf;
}
/**
*   Allocate and read WAV file data as normalized floating-point.
*   @return data buffer
*/
double * WAV_load_doubles (
    const TCHAR * file_name,                //!< [IN] WAV file name
    int * size,                             //!< [OUT] number of samples read (mono assumed!)
    pcm_format_t * fmt                      //!< [OUT, opt] PCM format
)
{
    return wav_load(file_name, size, fmt, 1);
}


float * WAV_load_floats (
    const TCHAR * file_name,                //!< [IN] WAV file name
    int * size,                             //!< [OUT] number of samples read (mono assumed!)
    pcm_format_t * fmt                      //!< [OUT, opt] PCM format
)
{
    return wav_load(file_name, size, fmt, 0);
}


#define NELEM( x )           ( sizeof(x) / sizeof((x)[0]) )
static int wav_append_doubles_ex (
    const double *x,                        //!< [IN] Buffer with data in the range [-1; +1)
    size_t size,                            //!< samples to write
    const TCHAR *file_name,                 //!< [IN] WAV file name
    const pcm_format_t fmt 
)
{
#if 1
    // Open - Append - Close
    wav_file_t *wf = wav_ctor(file_name, _T("r+b"));
    if (!wf)
    {
        wf = wav_ctor(file_name, _T("wb"));
    }
    if (wf) 
    {
        wf = wav_open_write_or_append(wf, fmt, EFILE_WAV);
    }
    if (wf)
    {
        fseek(wf->file, 0, SEEK_END);
        if (wf)
        {
            WAV_write_doubles(wf, x, size);
            WAV_close_write(wf);
            return 1;
        }
    }
    return 0;
#else
    // Keep up to 16 opened files, appending and updating WAV header
    static struct
    {
        char * name;
        wav_file_t * wfw;
    } file[16], *pf;
    int i;

    for (i = 0; i < NELEM(file); i++)
    {
        if (file[i].name && !strcmp(file[i].name, file_name))
        {
            break;
        }
    }

    if (i >= NELEM(file))
    {
        for (i = 0; i < NELEM(file); i++)
        {
            if (!file[i].name)
            {
                file[i].name = malloc(strlen(file_name)+1);
                strcpy(file[i].name, file_name);
                file[i].wfw = WAV_open_write(file_name, fmt, EFILE_WAV);
                break;
            }
        }
    }

    if (i < NELEM(file))
    {
        WAV_write_doubles(file[i].wfw, x, size);
        return wav_update_header(file[i].wfw);
    }
    return 0;
#endif
}


int WAV_append_doubles(
    const double *pcm,       //!< [IN] Buffer with data in the range [-1; +1)
    size_t nsamples,         //!< samples to write
    const TCHAR *file_name   //!< [IN] WAV file name
)
{
    return wav_append_doubles_ex(pcm, nsamples, file_name, WAV_fmt(44100, 1, 64, E_PCM_IEEE_FLOAT));
}


int WAV_append_doubles_pair (
    const double *pcm_l,     //!< [IN] Buffer with data in the range [-1; +1)
    const double *pcm_r,     //!< [IN] Buffer with data in the range [-1; +1)
    int step,                //!< step between samples
    size_t nsamples,         //!< samples to write
    const TCHAR *file_name   //!< [IN] WAV file name
)
{
    int success = 0;
    double * p = malloc(2*nsamples*sizeof(double));
    if (p)
    {
        size_t i;
        for (i = 0; i < nsamples; i++)
        {
            p[2*i+0] = *pcm_l; 
            p[2*i+1] = *pcm_r;
            pcm_l += step;
            pcm_r += step;
        }
        success = wav_append_doubles_ex(p, nsamples, file_name, WAV_fmt(44100, 2, 64, E_PCM_IEEE_FLOAT));
        free(p);
    }
    return success;
}

#ifdef f_wav_io_test
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
/*
*   WAV i/o test: 
*   1. output test float-point signal to WAV PCM file (possibly in integer format)
*   2. read PCM file data, compare with original floating-point and report PSNR
*/

#include "f_wav_io.h"
#include <math.h>
#include <assert.h>

#define TEST_SIZE 0x1000

#define TEST_DOUBLE     1
#if TEST_DOUBLE
#   define TYPE double
#else
#   define TYPE float
#endif

void test_sine (const pcm_format_t * fmt, int isRAW)
{
    char name[100];
    static TYPE buf[TEST_SIZE];
    static TYPE bufr[TEST_SIZE];
    int i;
    wav_file_t * wfw, *wfr;

    // make test signal
    for (i = 0; i < TEST_SIZE; i++)
    {
        buf[i] = sin(3.1415926*i/400);
    }

    // test file name
    sprintf(name, "test_%d_%d_%c.%s", fmt->ch, fmt->bips, fmt->pcm_type==E_PCM_INTEGER ?'i':'f', isRAW?"pcm":"wav");

    // write PCM data
    if (NULL == (wfw = WAV_open_write(name, *fmt, isRAW?EFILE_RAW:EFILE_WAV)))
    {
        printf("\nERROR creating file %s\n", name);
        return;
    }
    WAV_cue_add(wfw, 0, 0, "0");
    WAV_cue_add(wfw, TEST_SIZE/fmt->ch/2, 20, "half");
    WAV_cue_add(wfw, TEST_SIZE/fmt->ch-1, 40,"end");

#if TEST_DOUBLE
    WAV_write_doubles(wfw, buf, TEST_SIZE/fmt->ch);
#else
    WAV_write_floats(wfw, buf, TEST_SIZE/fmt->ch);
#endif
    WAV_close_write(wfw);

    // read PCM data
    if (NULL == (wfr = WAV_open_read(name, fmt)))
    {
        printf("\nERROR opening file %s\n", name);
        return;
    }
    assert(wfr->fmt.hz == fmt->hz);
    assert(wfr->fmt.ch == fmt->ch);
    assert(wfr->fmt.bips == fmt->bips);
#if TEST_DOUBLE
    WAV_read_doubles(wfr, bufr, TEST_SIZE/fmt->ch);
#else
    WAV_read_floats(wfr, bufr, TEST_SIZE/fmt->ch);
#endif

    // calculate difference
    {
        double d = 0;
        for (i = 0; i < TEST_SIZE; i++)
        {
            d += (buf[i] - bufr[i])*(buf[i] - bufr[i]);
        }
        
        // report PSNR
        if (d) d = 10*log10(d/TEST_SIZE);
        printf("\nPSNR (%2d bits): %f", wfr->fmt.bips, d);
    }
}

void test_read_write()
{
    pcm_format_t fmt;
    fmt.hz = 44100;
    fmt.ch = 2;

    fmt.pcm_type = E_PCM_IEEE_FLOAT;
    fmt.bips = 32;
    test_sine(&fmt, 0); test_sine(&fmt, 1);
    fmt.bips = 64;
    test_sine(&fmt, 0); test_sine(&fmt, 1);

    fmt.pcm_type = E_PCM_INTEGER;
    fmt.bips = 16;
    test_sine(&fmt, 0); test_sine(&fmt, 1);
    fmt.bips = 8;
    test_sine(&fmt, 0); test_sine(&fmt, 1);
    fmt.bips = 24;
    test_sine(&fmt, 0); test_sine(&fmt, 1);

// sample output for #define TEST_DOUBLE     1
//PSNR (32 bits): -156.728118
//PSNR (32 bits): -156.728118
//PSNR (64 bits): 0.000000
//PSNR (64 bits): 0.000000
//PSNR (16 bits): -101.589837
//PSNR (16 bits): -101.589837
//PSNR ( 8 bits): -52.723502
//PSNR ( 8 bits): -52.723502
//PSNR (24 bits): -149.201410
//PSNR (24 bits): -149.201410
}


/*
*   PCM to WAV conversion test application
*/

void help()
{
    printf("\nConversion from linear 16-bit RAW PCM to interleaved 24-bit WAV");
    printf("\nusage: raw2wav -b <samples> -r <hz> -c <ch> <pcm16> <wav24>");
    printf("\n\t-b input file buffer size (def: 2048)");
    printf("\n\t-r sample rate, hz");
    printf("\n\t-c number of channel");
}

int raw2wav24(int argc, char* argv[])
{
    int i;
    int buf_size = 2048;
    pcm_format_t fmt;
    wav_file_t *wfr, *wfw;
    char * in = NULL;
    char * out = NULL;
    double buf[2048*2];
    double buf2[2048*2];
    int read = 0;
    
    fmt.hz = 44100;        
    fmt.ch = 1;        
    fmt.bips = 16;
    fmt.pcm_type = E_PCM_INTEGER;  

    if (argc == 1)
    {   
        help();
        return 1;
    }
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            switch(argv[i][1])
            {
                case 'b':
                    buf_size = atoi(argv[++i]);
                    break;
                case 'r':
                    fmt.hz =  atoi(argv[++i]);
                    continue;
                case 'c':
                    fmt.ch =  atoi(argv[++i]);
                    continue;
                default:
                printf("\nillegal option:%s", argv[i]);
                help();
                return 1;
            }
            
        }
        if (!in) 
            in = argv[i];
        else if (!out) 
            out = argv[i];
        else 
        {
            printf("\nillegal option:%s", argv[i]);
            help();
            return 1;
        }
    }
    wfr = WAV_open_read(in, &fmt);
    if (!wfr)
    {
            printf("\ncan not open %s", in);
            return 1;
    }

    fmt.bips = 24;
    wfw = WAV_open_write(out, fmt, EFILE_WAV);
    if (!wfw)
    {
            printf("\ncan not open %s", out);
            return 1;
    }
    do
    {
        read = WAV_read_doubles(wfr, buf, buf_size);
        if (read != buf_size)
        {
            break;
        }
        if (fmt.ch==2)
        {
            int i;
            for(i=0;i<buf_size; i++)
            {
                buf2[2*i+0] = buf[i];
                buf2[2*i+1] = buf[buf_size+i];
            }
            for(i=0;i<2*buf_size; i++) buf[i] = buf2[i];
        }

        WAV_write_doubles(wfw, buf, read);
    } while (read);
    WAV_close_write(wfw);
    WAV_close_read(wfr);

    return 0;
}

int main(int argc, char* argv[])
{
#if 1
    return raw2wav24(argc, argv);
#else
    test_read_write();
    return 0;
#endif
}
// dmc f_wav_io.c -Df_wav_io_test && f_wav_io.exe

#endif
