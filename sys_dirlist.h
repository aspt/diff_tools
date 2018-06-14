/** 20.03.2008 @file
*   Directory listing for win32 with globbing and sorting support 
*   Option for 3-directory parsing (reference | source | difference)
*/

#ifndef dirlist_H_INCLUDED
#define dirlist_H_INCLUDED

#include <stddef.h>
#include "type_tchar.h"
//#include <tchar.h>
#include <wchar.h>

#define DIR_MAX_PATH        0x1000

// flag for 3-directory support 
#ifndef DIR_ENABLE_DIR3
#   define DIR_ENABLE_DIR3     1
#endif

#ifndef _MSC_VER
typedef long long __int64;
#endif

#ifdef __cplusplus
extern "C" {
#endif  //__cplusplus


/**
*           Callback return codes.
*           Application return this code during file list creation
*/
typedef enum
{
    
    /*
     *      Continue traversing operation.
     */
    E_DIR_CONTINUE,

    /*
     *      Skip current item.
     */
    E_DIR_SKIP,

    /*
     *      Callback signals to abort current operation.
     */
    E_DIR_ABORT

} dir_scan_callback_action_t;


/**
*           Directory sorting modes
*/
typedef enum
{
    /*
     *      Output files just after parent folder
     *
     *      e, f, D1\, D1\c, D1\d, D2\, D2\a, D2\b 
     */
    E_DIR_SORT_FILES_AFTER_FOLDER,

    /*
     *      Sort each folder independently.
     *
     *      D1\, D2\, e, f, D1\c, D1\d, D2\a, D2\b 
     */
    E_DIR_SORT_IN_FOLDERS,

    /*
     *      Make a plain index of all files and sub-folders and sort it.
     *
     *      D2\a, D2\b D1\c, D1\d, D1\, D2\, e, f, 
     */
    E_DIR_SORT_THROUGH

} dir_subdirs_sort_mode_t;

/**
*           Directory item descriptor
*/
typedef struct dir_entry_tag
{
//  Private
    struct dir_entry_tag * link;            // next item in the parent directory
    struct dir_entry_tag * items;           // subdirectory items
    struct dir_entry_tag * parent;          // root folder

//  Public
    __int64         last_write_time;        // Modification time in windows format
    __int64         creation_time;          // Creation time in windows format
    __int64         size;                   // for dir - size of folder+subfolders
    int             is_folder;              // flag to distinguish file and folder
    int             is_readonly;            // 
    int             attributes;
    size_t          name_len;               // strlen(name)
    TCHAR           name[1];                // variable-length name 
} dir_entry_t;

/**
*           Directory main structure
*/
typedef struct
{
    TCHAR           root_path[DIR_MAX_PATH];// root path
    dir_entry_t   ** index;                 // sorted index (owned by directory)
    unsigned long   items_count;            // files+folders count
    unsigned long   files_count;            // files count
    __int64         files_size;             // summary files size
    dir_entry_t *    items;                 // items list
    int             is_single_file;         // flag: 1 if root specifies single file
} dir_directory_t;



/**
*   Create directory listing with optional filtering, using path mask
*   and/or application callback
*/
void DIR_open(
    dir_directory_t * dir, 
    const TCHAR* path_mask,
    dir_scan_callback_action_t (*on_item_callback) (const TCHAR * path, dir_entry_t * fd, void * token),
    void * token
);

/**
*   Release directory listing memory 
*/
void DIR_close(dir_directory_t * dir);

/**
*   Iterate directory listing, calling callback for each item
*   Iteration order can be changed by the active index DIR_set_index()
*/
void DIR_for_each(
    dir_directory_t * dir, 
    dir_scan_callback_action_t (*on_item_callback) (const TCHAR * path, dir_entry_t * fd, void * token),
    void * token
);


/**
*   Same as DIR_for_each(), but start from given sub-directory
*/
int DIR_for_each_in_folder(
    TCHAR * path, 
    dir_entry_t * fd, 
    dir_scan_callback_action_t (*on_item_callback) (const TCHAR * path, dir_entry_t * fd, void * token),
    void * token
    );

/**
*   Create index array with given sort mode and comparison function. 
*   To activate this index, use DIR_set_index()
*   @return sorted array of pointers do directory entries.
*   note: application responsible to free() this memory, unless DIR_set_index() was used.
*/
dir_entry_t **  DIR_new_index(
    dir_directory_t * dir, 
    int (*comp)(const dir_entry_t* , const dir_entry_t* ), 
    dir_subdirs_sort_mode_t mode
);

/**
*   Change directory sorting mode, according to given index.
*   Return old index. New index set belongs to the directory listing, and will be 
*   released in DIR_close().
*   Setting index to NULL restore natural directory order (as in DIR_open()).
*/
dir_entry_t **  DIR_set_index(
    dir_directory_t * dir, 
    dir_entry_t **    new_index
);


/**
*       Various sorting functions for use with DIR_new_index
*/
int DIR_sort_names_descending(const dir_entry_t* pp1, const dir_entry_t* pp2);
int DIR_sort_size_descending (const dir_entry_t* pp1, const dir_entry_t* pp2);
int DIR_sort_time_descending (const dir_entry_t* pp1, const dir_entry_t* pp2);
int DIR_sort_path_descending (const dir_entry_t* pp1, const dir_entry_t* pp2);
dir_entry_t ** DIR_rev_index(dir_directory_t * dir, dir_entry_t ** index);


/**
*       Support functions
*/
int DIR_force_directory(TCHAR * path);
size_t DIR_get_root_relative_path(const dir_entry_t * item, TCHAR * path);
TCHAR * PATH_after_last_separator(TCHAR * path);
TCHAR * PATH_compact_path(TCHAR * compact_path, const TCHAR * path, size_t len);
void  PATH_strip_quotes(TCHAR * path);
TCHAR * PATH_ensure_terminating_separator(TCHAR * path);
int PATH_mask_match(const TCHAR *glob, const TCHAR *str);
int PATH_multimask_match(const TCHAR *glob, const TCHAR *str);


unsigned long DIR_files_count(dir_entry_t * fd, int recursive);
int DIR_is_file (const TCHAR * file_name);
int DIR_is_directory (const TCHAR * dir_name);

/**
*       List of file mask support.
*   Note: there is no internal support for file masks
*   in the directory. Files matching should be implemented
*   via _MatchName and callback functions,
*/
typedef struct file_mask_tag
{
    struct file_mask_tag * link;
    int             is_include;
    TCHAR           mask[1];
} file_mask_t;

void DIR_file_mask_list_close(file_mask_t * list);
int DIR_file_mask_list_add_include_mask(file_mask_t ** list, const TCHAR * mask);
int DIR_file_mask_list_add_exclude_mask(file_mask_t ** list, const TCHAR * mask);
int DIR_file_mask_list_match_name(file_mask_t * list, const TCHAR * file_name);


#if DIR_ENABLE_DIR3
typedef struct
{
    dir_directory_t  dir;

    TCHAR mirror[2][DIR_MAX_PATH];
    TCHAR *mirror_begin[2];     // can be NULL
    TCHAR *mirror_end[2];
    TCHAR *mirror_mask[2];      // can be NULL
    TCHAR *root_end;
    TCHAR *root_mask;
    int swap_root_mirror;

    dir_scan_callback_action_t (*on_item_callback) (
            const TCHAR * path, 
            const TCHAR * mirror1, 
            const TCHAR * mirror2, 
            dir_entry_t * fd, 
            void * token);
    void * token;

} TDIR3_directory;

void DIR3_open(
    TDIR3_directory * dir, 
    const TCHAR * path_mask,
    const TCHAR * mirror1,
    const TCHAR * mirror2,
    dir_scan_callback_action_t (*on_item_callback) (
            const TCHAR * path, 
            const TCHAR * mirror1, 
            const TCHAR * mirror2, 
            dir_entry_t * fd, 
            void * token),
    void * token
);

void DIR3_close(TDIR3_directory * dir);

void DIR3_for_each(
    TDIR3_directory * dir, 
    dir_scan_callback_action_t (*on_item_callback) (
            const TCHAR * path, 
            const TCHAR * mirror1, 
            const TCHAR * mirror2, 
            dir_entry_t * fd, 
            void * token),
    void * token
);
#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //dirlist_H_INCLUDED
