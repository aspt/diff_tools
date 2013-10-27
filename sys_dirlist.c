/** 20.03.2008 @file
*   Win32 directory management functions:
*   Directory listing (DIR_*)
*   Pattern matching aka globbing (DIR_FileMaskList_*)
*   Parallel listing of 3 directories: master + two mirrors (DIR3_*)
*   File/Path name management (PATH_*)
*
*   DIR3_ used to compare two folder trees and produce a result in a 3rd tree:
*   1st tree is scanned, and appropriate names for 2nd and 3rd tree are generated.
*/

#include "sys_dirlist.h"
#include <windows.h>
#include <shlwapi.h>                // needed for StrCmpI
#include <ctype.h>
#include <assert.h>

#ifdef _MSC_VER
#pragma comment(lib, "shlwapi.lib") // for MS compiler
#endif

#define IS_SEPARATOR(ch) ((ch) == '\\' || (ch) == '/')
//#define RECURSIVE_GLOB

// Parameters structure used to provide thread-safety
typedef struct 
{
    TCHAR * path;
    TCHAR * path_end;
    TCHAR * mask;
    dir_scan_callback_action_t (*on_item_callback)   (const TCHAR * path, dir_entry_t * fd, void * token);
    void * token;
} dir_scan_params_t;

/**
*   recursive merge sorting for strings
*/
static void merge_sort_kernel(void* a[], void* s[], size_t n, int (*comp)(const void *, const void *))
{
    size_t m = n >> 1;
    size_t i, j, k;
    void** b= a + m;
    if (n <= 16) 
    {
        for (i = 1; i < n; i++) 
        {
            void* tmp = a[i];
            for (j = i-1; (ptrdiff_t)j >= 0 && comp(tmp, a[j]) < 0; j--) a[j+1] = a[j];
            a[j+1] = tmp;
        }
    }
    else
    {
        memcpy(s, a, m*sizeof(int));
        merge_sort_kernel(s, a, m, comp);
        merge_sort_kernel(b, a, n - m, comp);
        for (i = 0, j = m, k = 0; i < m && j < n;)
        {
            a[k++] = comp(s[i] , a[j])<0 ? s[i++] : a[j++];
        }
        while (i < m) a[k++] = s[i++];
    }
}

/**
*   string sorting
*/
static void sort_ptrs(void* a[], size_t n, int (*comp)(const void *, const void *))
{
    void**s = malloc(n/2*sizeof(void*));
    if (s)
    {
        merge_sort_kernel(a, s, n, comp);
        free(s);
    }
}

/************************************************************************/
/*                WIN32_FIND_DATA support functions                     */
/************************************************************************/

/**
*   Return 1 if special folder '.' or '..' is found
*/
static int fd_is_dot_folder (const WIN32_FIND_DATA * fd)
{
    return 
        (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
         fd->cFileName[0] == '.' &&
        (fd->cFileName[1] == 0 || 
            (fd->cFileName[1] == '.' && fd->cFileName[2] == 0));
}

/**
*   Return file size
*/
static __int64 fd_size (const WIN32_FIND_DATA * fd)
{
    ULARGE_INTEGER  fileSize;
    fileSize.LowPart = fd->nFileSizeLow; 
    fileSize.HighPart = fd->nFileSizeHigh; 
    return fileSize.QuadPart;
}

/**
*   Return file last write time 
*/
static __int64 fd_last_write_time (const WIN32_FIND_DATA * fd)
{
    __int64 fileTime;
    fileTime =  *(__int64*)&fd->ftLastWriteTime.dwLowDateTime;
    return fileTime;
}

/**
*   Return 1 if file_name is a file, 0 if it is directory or not exist
*/
int DIR_is_file (const TCHAR * file_name)
{
    long attr = GetFileAttributes(file_name);
    return attr != ~0u && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

/************************************************************************/
/*                Path string processing support functions              */
/************************************************************************/
/**
*   remove surrounding quotes ("). in-place operation.
*/
void  PATH_strip_quotes(TCHAR * path)
{
    size_t len = _tcslen(path);
    if (len < 2)
    {
        return;
    }
    if (path[0] == '"' && path[len-1] == '"')
    {
        len -= 2;
        memmove(path, path + 1, len*sizeof(TCHAR));
        path[len] = '\0';
    }
}

/**
*   finds file name in given path.
*   Example:
*   dir/file => return 'file'
*   a:       => return 'a:'
*/
TCHAR *  PATH_after_last_separator(TCHAR * path)
{
    TCHAR * ptr;
    ptr = path + _tcslen(path);
    while(--ptr >= path)
    {
        if (IS_SEPARATOR(*ptr)) break;
    }
    return ptr + 1;
}

/**
*    Append path separator to given path
*/
TCHAR * PATH_ensure_terminating_separator(TCHAR * path)
{
    size_t len;

    len = _tcslen(path);
    if (len)
    {
        if (!IS_SEPARATOR(path[len - 1]))
        {
            path[len    ] = '\\';
            path[len + 1] = '\0';
        }
    }
    return path;
}

/**
*   Decomposes given string to root path and search mask:
*   for ex:
*   [c:/root/ *.c      ][*.c             ][my/dir          ]
*   [c:/root/       *.c][             *.c][my/dir/         ]
*                  ^                 ^               ^
*   [my/file          ]
*   [my/file          ]
*   
*/
static TCHAR * separate_path_mask(TCHAR * path, size_t bufsize, int is_single_file)
{
    TCHAR * mask = path + bufsize;
    TCHAR * p = path + _tcslen(path) - 1;
    int mask_found = 0;
    *--mask = 0;
    do 
    {
        TCHAR c = *p;
        if (c == '*' || c == '?') mask_found = 1;
        if (c == ':' || c == '\\' || c == '/') 
        {
            break;
        }
        *--mask = c;
    } while (--p >= path);
    
    if (!is_single_file)
    {
        if (!mask_found) *mask = '\0';
        if ( mask_found)  *++p = '\0'; 
        
        // Ensure that root path ended with separator
        PATH_ensure_terminating_separator(path);
    }
    return mask;
}

/**
*   return 1 if str matches with pattern pat. Code from C snippets.
*/
static int patimat (const TCHAR *pat, const TCHAR *str)
{
#ifdef RECURSIVE_GLOB
      switch (*pat)
      {
      case '\0':    return !*str;
      case '*' :    return patimat(pat+1, str) || *str && patimat(pat, str+1);
      case '?' :    return *str && patimat(pat+1, str+1);
      default  :    return (_totupper(*pat) == _totupper(*str)) && patimat(pat+1, str+1);
      }
#else
    const TCHAR * s, * p;
    int star = 0;

loopStart:
    for (s = str, p = pat; *s; ++s, ++p) 
    {
        switch (*p) 
        {
            case '?':
                break;
            case '*':
                star = 1;
                str = s, pat = p;
                do { ++pat; } while (*pat == '*');
                if (!*pat) return 1;
                goto loopStart;
            default:
                if (_totupper(*s) != _totupper(*p))
                {
                    if (!star) return 0;
                    str++;
                    goto loopStart;
                }
                break;
        }
    }
    while (*p == '*') ++p;
    return (!*p);
#endif
}


/************************************************************************/
/*                             Destructor's                             */
/************************************************************************/

static void DIR_entry_close(dir_entry_t * dir)
{
    dir_entry_t * next;
    for (; dir; dir = next)
    {
        next = dir->link;
        if (dir->is_folder)
        {
            DIR_entry_close(dir->items);
        }
        free(dir);
    }
}

void DIR_close(dir_directory_t * dir)
{
    if (dir->index)
    {
        free(dir->index);
        dir->index = NULL;
    }
    DIR_entry_close(dir->items);
}
        

/************************************************************************/
/*                            Constructors                              */
/************************************************************************/

static dir_entry_t * new_dir_entry(WIN32_FIND_DATA * fd, dir_entry_t * parent)
{
    dir_entry_t * entry;
    size_t name_lenght = _tcslen(fd->cFileName);
    entry = calloc(1, sizeof(*entry) + name_lenght*sizeof(TCHAR));
    if (entry)
    {
        entry->last_write_time = fd_last_write_time(fd);
        entry->size = fd_size(fd);
        entry->is_folder = fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        entry->name_len = name_lenght;
        entry->parent = parent;
        memcpy(entry->name, fd->cFileName, (name_lenght + 1)*sizeof(TCHAR));
    }
    return entry;
}


static dir_scan_callback_action_t dir_scan(dir_directory_t * dir, dir_entry_t ** items, dir_scan_params_t * params, dir_entry_t * parent)
{
    HANDLE              ffh;
    WIN32_FIND_DATA     fd;
    dir_entry_t        * entry = NULL;   // to make compiler happy
    dir_entry_t       ** tail = items;
    dir_scan_callback_action_t code;

    *items = NULL;

    // Fill list of entries in this folder
    ffh = FindFirstFile(params->path, &fd);
    if (ffh != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!fd_is_dot_folder(&fd))
            {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && 
                    params->mask[0] && 
                    !patimat(params->mask, fd.cFileName)) 
                {
                    continue;
                }
                entry = new_dir_entry(&fd, parent);
                if (entry)
                {
                    if (params->on_item_callback)
                    {
                        memcpy(params->path_end, entry->name, (entry->name_len + 1)*sizeof(TCHAR));            // "path\" += "directory" or "path\" += "filename"

                        code = params->on_item_callback(params->path, entry, params->token);
                        if (code == E_DIR_SKIP)
                        {
                            free(entry);
                            continue;
                        }
                        if (code == E_DIR_ABORT)
                        {
                            free(entry);
                            FindClose(ffh);
                            return E_DIR_ABORT;
                        }
                    }
                    
                    *tail = entry;
                    tail = &entry->link;

                    dir->items_count++;
                    if (!entry->is_folder)
                    {
                        dir->files_count++;
                        dir->files_size += entry->size;
                        if (parent)
                        {
                            parent->size += entry->size;
                        }
                    }
                }
            }
        } while (FindNextFile(ffh, &fd));
        FindClose(ffh);
    
        // Initialize sub-folder entries recursively, call callbacks
        for (entry = *items; entry; entry = entry->link)
        {
            if (entry->is_folder)
            {
                memcpy(params->path_end , entry->name, (entry->name_len + 1)*sizeof(TCHAR));        // "path\" += "directory" or "path\" += "filename"
                _tcscpy(params->path_end + entry->name_len, _T("\\*"));                   // Append mask
                params->path_end += entry->name_len + 1;                             // Set name pointer to * character
                code = dir_scan(dir, &entry->items, params, entry);                // Scan folder (recursion) with "path\directory\"
                if (parent)
                {
                    parent->size += entry->size;
                }
                params->path_end -= entry->name_len + 1;                             // Cut any junk after root path
                if (code == E_DIR_ABORT)
                {
                    return E_DIR_ABORT;
                }
            }
        }
    }
    return E_DIR_CONTINUE;
}

static void setup_scan_params(
    dir_directory_t * dir,                  ///< This object 
    const TCHAR * path,                      ///< Search path
    dir_scan_params_t * par
)
{
    TCHAR * root = dir->root_path;
    _tcscpy(root, path);
    PATH_strip_quotes(root);
    dir->is_single_file = DIR_is_file(root);

    // Pre-process path, move file mask to the end of buffer,
    // ensure terminating backslashes for directory path
    par->mask   = separate_path_mask(root, DIR_MAX_PATH, dir->is_single_file);
    par->path    = root;
    par->path_end = PATH_after_last_separator(root);

    if (!dir->is_single_file)
    {
        // Append '*' mask for OS directory scan function
        _tcscpy(par->path_end, _T("*"));
    }    
}

void DIR_open(
    dir_directory_t * dir,                  ///< This object 
    const TCHAR * path,                      ///< Search path
    dir_scan_callback_action_t (*on_item_callback) (  ///< Callback function
        const TCHAR * path,                 ///< Path of found item
        dir_entry_t * fd,                    ///< Item descriptor
        void * token                        ///< Token
        ),
    void * token                            ///< Token for callback
)
{
    dir_scan_params_t  params;
    memset(dir, 0, sizeof(*dir));
    setup_scan_params(dir, path, &params);
    params.on_item_callback = on_item_callback;
    params.token = token;
    dir_scan(dir, &dir->items, &params, NULL);
    *params.path_end = 0;                    // Cut any junk after root path
}



/************************************************************************/
/*                            Traversing                                */
/************************************************************************/

size_t DIR_get_root_relative_path(const dir_entry_t * item, TCHAR * path)
{
    size_t len = 0;
    if (item->parent)
    {
        len = DIR_get_root_relative_path(item->parent, path);
        path[len++] = '\\';
    }
    memcpy(path + len, item->name, (item->name_len + 1)*sizeof(TCHAR));
    return len + item->name_len;
}


static dir_scan_callback_action_t for_each_recursion(
    TCHAR * root_path,
    dir_entry_t * items, 
    dir_scan_callback_action_t (*on_item_callback) (const TCHAR * path, dir_entry_t * fd, void * token),
    void * token
    )
{
    dir_entry_t * ptr;
    TCHAR * end;

    assert(on_item_callback);

    end = root_path + _tcslen(root_path);
    for (ptr = items; ptr; ptr = ptr->link)
    {
        _tcscpy(end, ptr->name);   
        // TODO: add skip mode
        if (E_DIR_ABORT == on_item_callback(root_path, ptr, token))
        {
            *end = 0;
            return E_DIR_ABORT;
        }
        if (ptr->is_folder)
        {
            _tcscat(end, _T("\\"));
            if (E_DIR_ABORT == for_each_recursion(root_path, ptr->items, on_item_callback, token))
            {
                *end = 0;
                return E_DIR_ABORT;
            }
        }
    }
    *end = 0;
    return E_DIR_CONTINUE;
}

void DIR_for_each(
    dir_directory_t * dir, 
    dir_scan_callback_action_t (*on_item_callback) (const TCHAR * path, dir_entry_t * fd, void * token),
    void * token
    )
{
    assert(on_item_callback);

    if (dir->index)
    {
        // Index is present: scan the index sequentially
        unsigned long i;
        dir_entry_t ** item = (dir_entry_t **)dir->index;
        TCHAR * end = dir->root_path + _tcslen(dir->root_path);
        for (i = 0; i < dir->items_count; i++)
        {
            DIR_get_root_relative_path(item[i], end);
            if (on_item_callback(dir->root_path, item[i], token) == E_DIR_ABORT)
            {
                break;
            }
        }
        *end = 0;
    }
    else
    {
        // Do recursion
        for_each_recursion(dir->root_path, dir->items, on_item_callback, token);
    }
}

/************************************************************************/
/*                            Sorting                                   */
/************************************************************************/

static void * new_index(
    dir_entry_t * items, 
    dir_entry_t ** index,
    int (*pfn_compare)(const dir_entry_t*, const dir_entry_t* ),
    int sort_through_flag
    )
{
    unsigned i, count = 0;
    dir_entry_t ** index_next;
    
    dir_entry_t * ptr;
    dir_entry_t ** vector = index;

    // list to vector 
    for (ptr = items; ptr != NULL; ptr = ptr->link)
    {
        *vector++ = ptr;
        count++;
    }

    index_next = index + count;
    if (!sort_through_flag) 
    {
        sort_ptrs((void**)index, count, (int (*)(const void*, const void*))pfn_compare);
    }
    for (i = 0; i < count; i++)
    {
        if (index[i]->is_folder)
        {
            index_next = new_index(index[i]->items, index_next, pfn_compare, sort_through_flag);
        }
    }
    return index_next;
}


static void * new_index_files_after_folder(
    dir_entry_t * items, 
    dir_entry_t ** index,
    dir_entry_t ** index_end,
    int (*pfn_compare)(const dir_entry_t*, const dir_entry_t*)
    )
{
    dir_entry_t *  entry;
    dir_entry_t ** index_bot = index;
    dir_entry_t ** index_top = index_end;
    for (entry = items; entry; entry = entry->link)
    {
        if (entry->is_folder) 
        {
            *--index_top = entry;
        }
        else
        {
            *index_bot++ = entry;
        }
    }
    sort_ptrs((void**)index,     index_bot - index,    (int (*)(const void*, const void*))pfn_compare);
    sort_ptrs((void**)index_top,  index_end - index_top, (int (*)(const void*, const void*))pfn_compare);
    while (index_top != index_end)
    {
        *index_bot =  *index_top++;
        index_bot = new_index_files_after_folder((*index_bot)->items, index_bot + 1, index_top, pfn_compare);
    }
    return index_bot;
}

dir_entry_t **  DIR_new_index(
    dir_directory_t * dir, 
    int (*pfn_compare)(const dir_entry_t*, const dir_entry_t* ), 
    dir_subdirs_sort_mode_t mode
)
{
    dir_entry_t ** index;
    index = malloc(dir->items_count * sizeof(void*));
    if (index) switch (mode)
    {
    case E_DIR_SORT_FILES_AFTER_FOLDER:
        new_index_files_after_folder(dir->items, index, index + dir->items_count, pfn_compare);
        break;
    case E_DIR_SORT_IN_FOLDERS:
        new_index(dir->items, index, pfn_compare, 0);
        break;
    case E_DIR_SORT_THROUGH:
        new_index(dir->items, index, pfn_compare, 1);
        sort_ptrs((void**)index, dir->items_count, (int (*)(const void*, const void*))pfn_compare);
        break;
    default:
        free(index);
        index = NULL;
    }
    return index;
}

dir_entry_t **  DIR_set_index(
    dir_directory_t * dir, 
    dir_entry_t **  index
)
{
    dir_entry_t **  old_index;
    old_index = dir->index;
    dir->index = index;
    return old_index;
}

/************************************************************************/
/*                        Sorting callbacks                             */
/************************************************************************/

int DIR_sort_names_descending(const dir_entry_t* pp1, const dir_entry_t* pp2)
{
    if (pp1->is_folder && ! pp2->is_folder)
    {
        return -1;
    }
    if (pp2->is_folder && ! pp1->is_folder)
    {
        return 1;
    }
    return StrCmpI(pp1->name, pp2->name);
}

int DIR_sort_size_descending(const dir_entry_t* pp1, const dir_entry_t* pp2)
{
    if (pp1->is_folder && ! pp2->is_folder)
    {
        return -1;
    }
    if (pp2->is_folder && ! pp1->is_folder)
    {
        return 1;
    }
    if (pp2->size > pp1->size) return 1;
    if (pp2->size < pp1->size) return -1;
    return 0;
}

int DIR_sort_time_descending(const dir_entry_t* pp1, const dir_entry_t* pp2)
{
    if (pp1->is_folder && ! pp2->is_folder)
    {
        return -1;
    }
    if (pp2->is_folder && ! pp1->is_folder)
    {
        return 1;
    }
    if (pp2->last_write_time > pp1->last_write_time) return 1;
    if (pp2->last_write_time < pp1->last_write_time) return -1;
    return 0;
}


int DIR_sort_path_descending(const dir_entry_t* pp1, const dir_entry_t* pp2)
{
    static TCHAR path[2][DIR_MAX_PATH];    // too big array for stack
    size_t len[2];
    DIR_get_root_relative_path(pp1, path[0]);
    DIR_get_root_relative_path(pp2, path[1]);
    len[0] = _tcslen(path[0]);
    len[1] = _tcslen(path[1]);
    if (len[0] < len[1] && StrCmpNI(path[0], path[1], (int)len[0]) == 0)
    {
        return -1;
    } 
    else if (len[1] < len[0] && StrCmpNI(path[0], path[1], (int)len[0]) == 0)
    {
        return 1;
    }
    return StrCmpI(path[0], path[1]);
}

/************************************************************************/
/*                          Utility functions                           */
/************************************************************************/

static int create_dir_if_not_exist(TCHAR * path, TCHAR * term)
{
    TCHAR terminator;
    BOOL result;
    long  attr;

    terminator = *term;
    *term = '\0';
    attr = GetFileAttributes(path);
    if (attr == ~0u )
    {
        result = CreateDirectory(path, NULL);
    }
    else
    {
        result = (attr & FILE_ATTRIBUTE_DIRECTORY);
    }
    *term = terminator;

    return result;
}


int DIR_force_directory(TCHAR * path)
{
    TCHAR * p;
    TCHAR term = '\0';
    size_t len;
    int result = 1;
    len = _tcslen(path);
    p = path + len;
    
    if (len > 1)
    {
        if (IS_SEPARATOR(p[-1]))
        {
            term = *--p;
            *p = '\0';
        }
    }

    while (--p > path)           // NOT >= - ignore first backslash!
    {
        if (IS_SEPARATOR(*p))
        {
            if (create_dir_if_not_exist(path, p))
            {
                break;
            }
        }
    }

    while (*++p)
    {
        if (IS_SEPARATOR(*p))
        {
            if (!create_dir_if_not_exist(path, p))
            {
                break;
            }
        }
    }

    result = create_dir_if_not_exist(path, p);
    *p++ = term;
    return result;
}

TCHAR * PATH_compact_path(TCHAR * compact_path, const TCHAR * path, int len)
{
    *compact_path = '\0';
    PathCompactPathEx(compact_path, path, len, 0/*flags not used*/);
    return compact_path;
}

unsigned long DIR_files_count(dir_entry_t * fd)
{
    unsigned long count = 0;
    for (fd = fd->items; fd; fd = fd->link)
    {
        count += !fd->is_folder;
    }
    return count;
}

#if DIR_ENABLE_DIR3
/** 
*   Wildcards '*' and '?' matching ('globbing') with template substitution
*   Example input:
*   pat:  ?cod*.wav 
*   pat2: ?ref*.wav (or ?ref**.w*a*v)
*   str:  acodsna.wav 
*   Output:
*   str2: arefsna.wav
*/
static int patirepl (const TCHAR *pat, const TCHAR *str, const TCHAR * pat2, TCHAR *str2)
{
#ifdef RECURSIVE_GLOB
    while(*pat2 != '?' && *pat2 != '*')
    {
        if (!(*str2++ = *pat2++)) return 1;
    }
    switch (*pat)
    {
    case '\0':
        // copy remaining template symbols, ignore '*' fail on '?'
        // TODO: should it fail on '?'
        for(;;) 
        {
            TCHAR c = *pat2++; 
            if(c=='?') return 0; 
            if(c!='*') *str2++=c; 
            if(!c) return !*str;
        }
    case '*' :
        return  patirepl(pat+1, str, pat2, str2) ||     // '*' does not match
                !!(*str2 = *str) &&                  // '*' consumes char, copy it to template
                patirepl(pat, str+1, pat2+(*pat2=='?'), str2+1); // advance '?' in template
    case '?' :
        return !!(*str2 = *str) && patirepl(pat+1, str+1, 
        pat2+(*pat2=='?'||(pat[1]!='*'&&pat[1]!='?')), 
        str2+1);
    default  :
        return (_totupper(*pat) == _totupper(*str)) && patirepl(pat+1, str+1, pat2, str2);
    }
#else
    const TCHAR * s;
    const TCHAR * p;
          TCHAR * s2;
    const TCHAR * p2;
    int star = 0;
loopStart:
    for (s = str, p = pat, s2 = str2, p2 = pat2; *s; ++s, ++p) 
    {
        while (*p2!='*' && *p2!='?')
        {
            *s2++ = *p2++;
            if (!p2[-1]) break;
        } 

        if (*p == '?' || *p == '*') 
        {
            pat2=p2;
            str2=s2;
        }
        switch (*p) 
        {
            case '?':
              *s2++ = *s;
              if (*p2=='?') p2++;
              break;
            case '*':
              star = 1;
              str = s, pat = p; 
              do { ++pat; } while (*pat == '*');
              if (!*pat) 
              {
                  while (0 != (*s2++ = *s++)) {/*no action*/}
                  goto exit;
              }
              pat2++;
              goto loopStart;
            default:
              if (_totupper(*s) != _totupper(*p))
              {
                  //if (!star) return 0;
                  if (!star) goto exit;//return 0;
                  *str2++=*str++;
                  if (*pat2=='?') pat2++;
                  goto loopStart;
              }
              break;
        }
    }
exit:
    {
        TCHAR c;
        if (star) s2--;
        do
        {
            c = *p2++; 
            if(c!='*'&&c!='?') *s2++=c; 
        } while(c);
    }
    return 1;
#endif
}


static dir_scan_callback_action_t dir3_callback(const TCHAR * path, dir_entry_t * fd, void * token)
{
    TDIR3_directory * dir = token;
    if (dir->on_item_callback)
    {
        int i;
        for (i = 0; i < 2; i++)
        {
            _tcscpy(dir->mirror_end[i], dir->root_end);
            if (dir->mirror_mask[i] && dir->mirror_mask[i][0]) 
                patirepl(dir->root_mask, dir->root_end, dir->mirror_mask[i], dir->mirror_end[i]);
        }
        return dir->on_item_callback(path, dir->mirror_begin[0], dir->mirror_begin[1], fd, dir->token);
    }
    return E_DIR_CONTINUE;
}

/*
    1. p\file <> q\pile
       [p\^file    ]  [q\^pile    pile]
    2. p\msk  <> q\pile
       [p\^*    msk]  [q\^pile    pile]
    3. p\msk  <> q\nsk
       [p\^*    msk]  [p\^*    nsk]
        
*/
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
)
{
    int i;
    int mirror_is_single_file = 0; // just to avoid warning
    int swap_root_mirror = 0;
    dir_scan_params_t  params;
    (void)token;
restart:
    memset(dir, 0, sizeof(*dir));

    setup_scan_params(&dir->dir, path_mask, &params);
    params.on_item_callback    = dir3_callback;//***
    params.token        = dir;

    dir->on_item_callback = on_item_callback;
    dir->root_end = params.path_end + _tcslen(params.path_end)*dir->dir.is_single_file;
    dir->root_mask = params.mask;
    for (i = 0; i < 2; i++)
    {
        if (mirror1)
        {
            _tcscpy(dir->mirror[i], mirror1);
            dir->mirror_begin[i] = dir->mirror[i];
            PATH_strip_quotes(dir->mirror[i]);
            if (!i)
            {
                mirror_is_single_file = DIR_is_file(dir->mirror[0]);
                if (dir->dir.is_single_file && !mirror_is_single_file)
                {
                    const TCHAR * t = mirror1;
                    mirror1 = path_mask;
                    path_mask = t;
                    swap_root_mirror = 1;
                    goto restart;
                }
            }
            dir->mirror_mask[i] = separate_path_mask(dir->mirror[i], sizeof(dir->mirror[i])/sizeof(TCHAR), 
                //dir->dir.is_single_file
                //DIR_is_file(dir->mirror[0])
                mirror_is_single_file
                );
        }
        dir->mirror_end[i] =  PATH_after_last_separator(dir->mirror[i]);
        mirror1 = mirror2;
    }
    dir->swap_root_mirror = swap_root_mirror;
    dir_scan(&dir->dir, &dir->dir.items, &params, NULL);
    *params.path_end = 0;                    // Cut any junk after root path
    dir->mirror_end[0][0] = 0;
    dir->mirror_end[1][0] = 0;
}

void DIR3_close(TDIR3_directory * dir)
{
    DIR_close(&dir->dir);
}


void DIR3_for_each(
    TDIR3_directory * dir, 
    dir_scan_callback_action_t (*on_item_callback) (
        const TCHAR * path, 
        const TCHAR * mirror1, 
        const TCHAR * mirror2, 
        dir_entry_t * fd, 
        void * token),
    void * token
)
{
    dir->token = token;
    dir->on_item_callback = on_item_callback;
    DIR_for_each(&dir->dir, dir3_callback, dir);
}

#endif

#ifdef sys_dirlist_test
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

#include <stdio.h>
#include <time.h>

static TCHAR pretty_path[1024];

static TCHAR * pretty_print_path(TCHAR * prettyPath, const TCHAR * path, int limit)
{
    int len;
    len = _tcslen(path);
    if (len > limit)
    {
        _tcscpy(prettyPath, "...");
        _tcscat(prettyPath, path + len - limit + 3);
    }
    else
    {
        _tcscpy(prettyPath, path);
    }
    return prettyPath;
}

/*
void myPrintFile(const TCHAR * path, dir_entry_t * fd, void * token)
{
    _tprintf("\n%-30s\t%I64d", pretty_print_path(pretty_path, path, 30), fd->Size);
}
void myPrintFolder(const TCHAR * path, dir_entry_t * fd, void * token)
{
    _tprintf("\n%-30s\t<DIR>", pretty_print_path(pretty_path, path, 30));
}
*/
void my_print_dir_item(const TCHAR * path, dir_entry_t * fd, void * token)
{
    static int count = 0;
    if (fd->is_folder)
    {
        _tprintf("\n%-30s\t<DIR>", pretty_print_path(pretty_path, path, 30));
    }
    else
    {
        _tprintf("\n%-30s\t%I64d", pretty_print_path(pretty_path, path, 30), fd->Size);
    }
    if (count++ > 5)
    {
        return E_DIR_ABORT;
    }
    return E_DIR_CONTINUE;
}

_tmain(int argc, TCHAR *argv[])
{
    TCHAR * path = argc == 2?argv[1]:"";
    static dir_directory_t dir;
    clock_t t;
    t = clock();
    DIR_open(&dir, path, my_print_dir_item, NULL);
    //DIR_open(&dir, path, NULL, NULL, NULL);

    //DIR_useIndex(&dir, DIR_callbackSortSizeDescending, 0);
    //DIR_useIndex2(&dir, DIR_callbackSortPathsDescending, 1);
    //DIR_useIndex2(&dir, DIR_callbackSortSizeDescending, 1);
    //DIR_set_index(&dir, DIR_new_index(&dir, DIR_sort_time_descending, E_DIR_SORT_FILES_AFTER_FOLDER));
    DIR_set_index(&dir, DIR_new_index(&dir, DIR_sort_size_descending, E_DIR_SORT_THROUGH));
    //DIR_set_index(&dir, DIR_new_index(&dir, DIR_sort_size_descending, E_DIR_SORT_IN_FOLDERS));
    
    //_tprintf("\n=======================\n%d items; size %I64d", DIR_items_count(&dir), DIR_totalSize(&dir));
    _tprintf("\n=======================\n%d items; size %I64d", dir.ulItemsCount, dir.llFilesSize);
    _tprintf("\nclocks %d\n=======================", clock() - t);
    DIR_for_each(&dir,my_print_dir_item, NULL);
    DIR_close(&dir);
}

#endif // sys_dirlist_test

#if defined sys_dirlist3_test && DIR_ENABLE_DIR3
#include <stdio.h>
#include <time.h>

static TCHAR pretty_path[3][1024];

static TCHAR * pretty_print_path(TCHAR * prettyPath, const TCHAR * path, size_t limit)
{
    size_t len;
    if (!path) return "{NULL}";
    len = _tcslen(path);
    if (len > limit)
    {
        _tcscpy(prettyPath, "...");
        _tcscat(prettyPath, path + len - limit + 3);
    }
    else
    {
        _tcscpy(prettyPath, path);
    }
    return prettyPath;
}


dir_scan_callback_action_t my_print_dir_item(const TCHAR * path, const TCHAR * pM1, const TCHAR * pM2, dir_entry_t * fd, void * token)
{
    if (fd->is_folder)
    {
        _tprintf("\n%-30s\t%-30s\t%-30s\t<DIR>", 
            pretty_print_path(pretty_path[0], path, 30),
            pretty_print_path(pretty_path[1], pM1, 30),
            pretty_print_path(pretty_path[2], pM2, 30)
            );
    }
    else
    {
        _tprintf("\n%-30s\t%-30s\t%-30s\t%I64d", 
            pretty_print_path(pretty_path[0], path, 30),
            pretty_print_path(pretty_path[1], pM1, 30),
            pretty_print_path(pretty_path[2], pM2, 30),
            fd->Size
            );
    }

    {
        static int count = 0;
        if (count++ > 5)
        {
            return E_DIR_ABORT;
        }
    }

    return E_DIR_CONTINUE;
}

_tmain(int argc, TCHAR *argv[])
{
    TCHAR * path  = argc >= 2?argv[1]:"";
    TCHAR * path1 = argc >= 3?argv[2]:"";
    TCHAR * path2 = argc >= 4?argv[3]:NULL;
    static TDIR3_directory dir;
    clock_t t;
    t = clock();
    DIR3_open(&dir, path, path1, path2, my_print_dir_item, NULL);
    //DIR_open(&dir, path, NULL, NULL, NULL);

    //DIR_useIndex(&dir, DIR_callbackSortSizeDescending, 0);
    //DIR_useIndex2(&dir, DIR_callbackSortPathsDescending, 1);
    //DIR_useIndex2(&dir, DIR_callbackSortSizeDescending, 1);
    //DIR_set_index(&dir, DIR_new_index(&dir, DIR_sort_time_descending, E_DIR_SORT_FILES_AFTER_FOLDER));
    //DIR_set_index(&dir, DIR_new_index(&dir, DIR_sort_size_descending, E_DIR_SORT_THROUGH));
    //DIR_set_index(&dir, DIR_new_index(&dir, DIR_sort_size_descending, E_DIR_SORT_IN_FOLDERS));
    
    //_tprintf("\n=======================\n%d items; size %I64d", DIR_items_count(&dir), DIR_totalSize(&dir));
    _tprintf("\n=======================\n%d items; size %I64d", dir.dir.ulItemsCount, dir.dir.llFilesSize);
    _tprintf("\nclocks %d\n=======================", clock() - t);
    //DIR_for_each(&dir,my_print_dir_item, NULL);
    DIR3_close(&dir);
}

#endif  //#if defined sys_dirlist3_test && DIR_ENABLE_DIR3
/************************************************************************/
/*                          Unused functions                            */
/************************************************************************/

/*

static TCHAR * remove_terminating_separator(TCHAR * path)
{
    size_t len;

    len = _tcslen(path);
    while (len > 1 && (path[len - 1] == '\\' || path[len - 1] == '/' ))
    {
        path[--len] = '\0';
    }
    return path;
}

int DIR_is_file (
    const TCHAR * pfn
)
{
    long    attr    = GetFileAttributes(pfn);
    return attr != ~0u && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

int DIR_is_directory (
    const TCHAR * pfn
)
{
    DWORD   attr = GetFileAttributes(pfn);
    return  attr != ~0u && (attr & FILE_ATTRIBUTE_DIRECTORY);
}



void DIR_total_size_callback(const TCHAR * path, dir_entry_t * fd, void * token)
{
    if (!fd->is_folder) *(__int64 *)token += fd->Size;
}
__int64 DIR_total_size(dir_directory_t * dir)
{
    __int64 size = 0;
    DIR_for_each(dir, DIR_total_size_callback, &size);
    return size;
}

void DIR_files_count_callback(const TCHAR * path, dir_entry_t * fd, void * token)
{
    if (!fd->is_folder) (*(unsigned long *)token)++;
}
unsigned long DIR_files_count(dir_directory_t * dir)
{
    unsigned long  count = 0;
    DIR_for_each(dir, DIR_files_count_callback, &count);
    return count;
}

void DIR_items_count_callback(const TCHAR * path, dir_entry_t * fd, void * token)
{
    (*(unsigned long *)token)++;
}
unsigned long DIR_items_count(dir_directory_t * dir)
{
    unsigned long  count = 0;
    DIR_for_each(dir, DIR_items_count_callback, &count);
    return count;
}

*/
