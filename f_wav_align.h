/** 18.12.2011 @file
*  
*/

#ifndef f_wav_align_H_INCLUDED
#define f_wav_align_H_INCLUDED

#include "f_wav_io.h"

#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus


void ALIGN_align_pair (wav_file_t * wf0, wav_file_t * wf1);
int ALIGN_init (unsigned int maxOffset, unsigned int maxCh);
void ALIGN_close (void);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //f_wav_align_H_INCLUDED
