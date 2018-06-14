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

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>                // needed for StrCmpI
#else
//#include "msdirent.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>



#include <stdio.h>

#endif

#ifndef _WIN32
#define StrCmpI strcmp
#define StrCmpNI strncmp
#define CSLASH '/'
#define SSLASH "/"
#else
#define CSLASH '\\'
#define SSLASH "\\"
#endif

#if 0
/* public domain Simple, Minimalistic, making list of files and directories
 *	©2017 Yuichiro Nakada
 *
 * Basic usage:
 *	int num;
 *	LS_LIST *ls = ls_dir("dir/", LS_RECURSIVE|LS_RANDOM, &num);
 * */


#define LS_RECURSIVE	1
#define LS_RANDOM	2

typedef struct {
	int status;
	char d_name[PATH_MAX];
} LS_LIST;

int ls_count_dir(char *dir, int flag)
{
	DIR *dp;
	struct dirent *entry;
	struct stat statbuf;

	if (!(dp = opendir(dir))) {
		perror("opendir");
		return 0;
	}
	char *cpath = getcwd(0, 0);
	chdir(dir);

	int i=0;
	while ((entry = readdir(dp))) {
		if (!strcmp(".", entry->d_name) || !strcmp("..", entry->d_name)) continue;

		if (flag & LS_RECURSIVE) {
			stat(entry->d_name, &statbuf);
			if (S_ISDIR(statbuf.st_mode)) {
				char path[PATH_MAX];
				sprintf(path, "%s/%s", dir, entry->d_name);
				i += ls_count_dir(path, flag);
			}
		}

		i++;
	}

	closedir(dp);
	chdir(cpath);
	free(cpath);

	return i;
}
	
int ls_seek_dir(char *dir, LS_LIST *ls, int flag)
{
	DIR *dp;
	struct dirent *entry;
	struct stat statbuf;
	char buf[PATH_MAX];

	if (!(dp = opendir(dir))) {
		perror("opendir");
		printf("%s\n", dir);
		return 0;
	}
	char *cpath = getcwd(0, 0);
	chdir(dir);

	int i=0;
	while ((entry = readdir(dp))) {
		if (!strcmp(".", entry->d_name) || !strcmp("..", entry->d_name)) continue;

		sprintf(buf, "%s/%s", dir, entry->d_name);
		strcpy((ls+i)->d_name, buf);

		stat(entry->d_name, &statbuf);
		if (S_ISDIR(statbuf.st_mode)) {
			(ls+i)->status = 1;

			//if (flag & LS_RECURSIVE) i += ls_seek_dir(buf, ls+i+1, flag);
			if (flag & LS_RECURSIVE) i += ls_seek_dir(buf, ls+i, flag);
		} else {
			(ls+i)->status = 0;
			i++;
		}
		//i++;
	}

	closedir(dp);
	chdir(cpath);
	free(cpath);

	return i;
}

int ls_comp_func(const void *a, const void *b)
{
	return (strcmp((char*)(((LS_LIST*)a)->d_name), (char*)(((LS_LIST*)b)->d_name)));
}

LS_LIST *ls_dir(char *dir, int flag, int *num)
{
	int n = ls_count_dir(dir, flag);
	if (!n) {
		fprintf(stderr, "No file found [%s]!!\n", dir);
		return 0;
	}

	LS_LIST *ls = (LS_LIST *)calloc(n, sizeof(LS_LIST));
	if (!ls) {
		perror("calloc");
		return 0;
	}

	if (!ls_seek_dir(dir, ls, flag)) return 0;

	if (flag & LS_RANDOM) {
#ifdef RANDOM_H
		xor128_init(time(NULL));
#else
		srand(time(NULL));
#endif
		for (int i=0; i<n; i++) {
#ifdef RANDOM_H
			int a = xor128()%n;
#else
			int a = rand()%n;
#endif
			LS_LIST b = ls[i];
			ls[i] = ls[a];
			ls[a] = b;
		}
	} else {
		qsort(ls, n, sizeof(LS_LIST), ls_comp_func);
	}

	*num = n;
	return ls;
}

#include <ctype.h>
char *findExt(char *path)
{
	static char ext[10];
	char *e = &ext[9];
	*e-- = 0;
	int len = strlen(path)-1;
	for (int i=len; i>len-9; i--) {
		if (path[i] == '.' ) break;
		*e-- = tolower(path[i]);
	}
	return e+1;
}
#endif

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
static void merge_sort_kernel(void* a[], void* s[], size_t n, int (*fn_comp)(const void *, const void *))
{
    size_t m = n >> 1;
    size_t i, j, k;
    void** b= a + m;
    if (n <= 16) 
    {
        for (i = 1; i < n; i++) 
        {
            void* tmp = a[i];
            for (j = i-1; (ptrdiff_t)j >= 0 && fn_comp(tmp, a[j]) < 0; j--) a[j+1] = a[j];
            a[j+1] = tmp;
        }
    }
    else
    {
        memcpy(s, a, m*sizeof(int));
        merge_sort_kernel(s, a, m, fn_comp);
        merge_sort_kernel(b, a, n - m, fn_comp);
        for (i = 0, j = m, k = 0; i < m && j < n;)
        {
            a[k++] = fn_comp(s[i] , a[j])<0 ? s[i++] : a[j++];
        }
        while (i < m) a[k++] = s[i++];
    }
}

/**
*   string sorting
*/
static void sort_ptrs(void* a[], size_t n, int (*fn_comp)(const void *, const void *))
{
    void**s = malloc(n/2*sizeof(void*));
    if (s)
    {
        merge_sort_kernel(a, s, n, fn_comp);
        free(s);
    }
}

/************************************************************************/
/*                WIN32_FIND_DATA support functions                     */
/************************************************************************/

#ifdef _WIN32

/**
*   Return file size
*/
static __int64 fd_size (const WIN32_FIND_DATA * fd)
{
    ULARGE_INTEGER  size;
    size.LowPart = fd->nFileSizeLow; 
    size.HighPart = fd->nFileSizeHigh; 
    return size.QuadPart;
}

/**
*   Return file last write time 
*/
static __int64 fd_last_write_time (const WIN32_FIND_DATA * fd)
{
    return *(__int64*)&fd->ftLastWriteTime.dwLowDateTime;
}

static __int64 fd_creation_time (const WIN32_FIND_DATA * fd)
{
    return *(__int64*)&fd->ftCreationTime.dwLowDateTime;
}

/**
*   Return 1 if file_name is a file, 0 if it is directory or not exist
*/
int DIR_is_file (const TCHAR * file_name)
{
    long attr = GetFileAttributes(file_name);
    return attr != ~0u && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

/**
*   Return 1 if dir_name is a directory, 0 if it is file or not exist
*/
int DIR_is_directory (const TCHAR * dir_name)
{
    long attr = GetFileAttributes(dir_name);
    return attr != ~0u && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static dir_entry_t * new_dir_entry(WIN32_FIND_DATA * fd, dir_entry_t * parent)
{
    dir_entry_t * entry;
    size_t name_lenght = _tcslen(fd->cFileName);
    entry = calloc(1, sizeof(*entry) + name_lenght*sizeof(TCHAR));
    if (entry)
    {
        entry->last_write_time = fd_last_write_time(fd);
        entry->creation_time   = fd_creation_time(fd);
        entry->size = fd_size(fd);
        entry->is_folder = fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        entry->is_readonly = fd->dwFileAttributes & FILE_ATTRIBUTE_READONLY;
        entry->attributes = fd->dwFileAttributes;
        entry->name_len = name_lenght;
        entry->parent = parent;
        memcpy(entry->name, fd->cFileName, (name_lenght + 1)*sizeof(TCHAR));
    }
    return entry;
}

#else

/**
*   Return file size
*/
static __int64 fd_size (const struct stat * fd)
{
    return fd->st_size;
}

/**
*   Return file last write time 
*/
static __int64 fd_last_write_time (const struct stat * fd)
{
    return *(__int64*)&fd->st_mtime;
}

static __int64 fd_creation_time (const struct stat * fd)
{
    return *(__int64*)&fd->st_ctime;
}

/**
*   Return 1 if file_name is a file, 0 if it is directory or not exist
*/
int DIR_is_file (const TCHAR * file_name)
{
    struct stat buffer;
    if (!stat(file_name, &buffer))
    {
        return S_ISREG(buffer.st_mode);
    }
    return 0;
}

/**
*   Return 1 if dir_name is a directory, 0 if it is file or not exist
*/
int DIR_is_directory (const TCHAR * dir_name)
{
    struct stat buffer;
    if (!stat(dir_name, &buffer))
    {
        return S_ISDIR(buffer.st_mode);
    }
    return 0;
}

static dir_entry_t * new_dir_entry(const struct stat * fd, dir_entry_t * parent, const char * name)
{
    dir_entry_t * entry;
    size_t name_lenght = strlen(name);
    entry = calloc(1, sizeof(*entry) + name_lenght*sizeof(char));
    if (entry)
    {
        entry->last_write_time = fd_last_write_time(fd);
        entry->creation_time   = fd_creation_time(fd);
        entry->size = fd_size(fd);
        entry->is_folder = S_ISDIR(fd->st_mode);
        entry->is_readonly = 0;
        entry->attributes = fd->st_mode;
        entry->name_len = name_lenght;
        entry->parent = parent;
        memcpy(entry->name, name, (name_lenght + 1)*sizeof(char));
    }
    return entry;
}

//typedef struct stat WIN32_FIND_DATA;
#define WIN32_FIND_DATA struct stat 
#endif

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
            path[len    ] = CSLASH;
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
int PATH_mask_match (const TCHAR *glob, const TCHAR *str)
{
#ifdef RECURSIVE_GLOB
      switch (*glob)
      {
      case '\0':    return !*str;
      case '*' :    return PATH_mask_match(glob+1, str) || *str && PATH_mask_match(glob, str+1);
      case '?' :    return *str && PATH_mask_match(glob+1, str+1);
      default  :    return (_totupper(*glob) == _totupper(*str)) && PATH_mask_match(glob+1, str+1);
      }
#else
    const TCHAR * s, * p;
    int star = 0;

loopStart:
    for (s = str, p = glob; *s; ++s, ++p) 
    {
        switch (*p) 
        {
            case '?':
                break;
            case '*':
                star = 1;
                str = s, glob = p;
                do { ++glob; } while (*glob == '*');
                if (!*glob) return 1;
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

static int glob_end(const TCHAR *glob)
{
    return (!*glob || *glob == '|');
}
static const TCHAR * glob_next(const TCHAR *glob)
{
    while (!glob_end(glob))
    {
        glob++;
    }
    return (*glob) ? (glob+1) : NULL ;
}

int PATH_multimask_match (const TCHAR *glob, const TCHAR *str)
{
    const TCHAR * s, * p;
    int star;
    const TCHAR *str0 = str;

start_next_glob:
    str = str0;
    star = 0;
    if (!glob)
    {
        return 0;
    }

loopStart:
    for (s = str, p = glob; *s; ++s, ++p) 
    {
        switch (*p) 
        {
        case '?':
            break;
        case '*':
            star = 1;
            str = s, glob = p;
            do { ++glob; } while (*glob == '*');
            if (glob_end(glob)) 
            {
                return 1;
            }
            goto loopStart;
        default:
            if (_totupper(*s) != _totupper(*p))
            {
                if (!star) 
                {
                    glob = glob_next(glob);
                    goto start_next_glob;
                }
                str++;
                goto loopStart;
            }
            break;
        }
    }
    while (*p == '*') ++p;
    if (glob_end(p))
    {
        return 1;
    }

    glob = glob_next(glob);
    goto start_next_glob;
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

int dir_add(dir_directory_t * dir, dir_scan_params_t * params, dir_entry_t * parent, dir_entry_t  *** tail, dir_entry_t * entry)
{
    int code = E_DIR_CONTINUE;
    if (entry)
    {
        if (params->on_item_callback)
        {
            memcpy(params->path_end, entry->name, (entry->name_len + 1)*sizeof(TCHAR));            // "path\" += "directory" or "path\" += "filename"

            code = params->on_item_callback(params->path, entry, params->token);
            if (code == E_DIR_SKIP)
            {
                free(entry);
                return code;
            }
            if (code == E_DIR_ABORT)
            {
                free(entry);
                return code;
            }
        }

        **tail = entry;
        *tail = &entry->link;

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
    return code;
}

int is_dots_name(const TCHAR * p)
{
    return (p[0] == '.' && (p[1] == 0 || (p[1] == '.' && p[2] == 0)));
}

static dir_scan_callback_action_t dir_scan(dir_directory_t * dir, dir_entry_t ** items, dir_scan_params_t * params, dir_entry_t * parent)
{
    dir_entry_t        * entry = NULL;   // to make compiler happy
    dir_entry_t       ** tail = items;
    dir_scan_callback_action_t code;
#ifdef _WIN32
    HANDLE              ffh;
    WIN32_FIND_DATA     fd;
#define IS_DIR(fd) (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
#else
    DIR *dp;
    struct dirent *_entry;
#define IS_DIR(fd) (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    int len = strlen(params->path);
#endif

    *items = NULL;

#ifdef _WIN32

    // Fill list of entries in this folder
    if (INVALID_HANDLE_VALUE == (ffh = FindFirstFile(params->path, &fd)))
#else
//printf("zzzzzzz %s %d\n", params->path, len);
    if (len > 2 && params->path[len-1] == '*' && params->path[len-2] == '/')
    {
        params->path[len-2] = 0;
    }
    if (!len) params->path[0] = '.', params->path[1] = 0;;
//printf("%s\n", params->path);
        
    if (!(dp = opendir(params->path)))
#endif
    {
        return E_DIR_CONTINUE;
    }

#ifdef _WIN32
    do
    {
        if (is_dots_name(fd.cFileName))
        {
            continue;
        }
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && 
            params->mask[0] && !PATH_mask_match(params->mask, fd.cFileName)) 
        {
            continue;
        }

        if (E_DIR_ABORT == dir_add(dir, params, parent, &tail, new_dir_entry(&fd, parent)))
            break;
    } while (FindNextFile(ffh, &fd));
    FindClose(ffh);
#else
    while (_entry = readdir(dp))
    {
        struct stat fd;
        stat(_entry->d_name, &fd);
        
        if (is_dots_name(_entry->d_name))
        {
            continue;
        }
        if (!S_ISDIR(fd.st_mode) && 
            params->mask[0] && !PATH_mask_match(params->mask, _entry->d_name)) 
        {
            continue;
        }

        if (E_DIR_ABORT == dir_add(dir, params, parent, &tail, new_dir_entry(&fd, parent, _entry->d_name)))
        {
            break;
        }
        
    } 
    closedir(dp);
#endif

    // Initialize sub-folder entries recursively, call callbacks
    for (entry = *items; entry; entry = entry->link)
    {
        if (entry->is_folder)
        {
            memcpy(params->path_end , entry->name, (entry->name_len + 1)*sizeof(TCHAR));        // "path\" += "directory" or "path\" += "filename"
            _tcscpy(params->path_end + entry->name_len, _T(SSLASH)_T("*"));                   // Append mask
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
#ifdef _WIN32
        _tcscpy(par->path_end, _T("*"));
#endif
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
        path[len++] = CSLASH;
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
        int action;
        _tcscpy(end, ptr->name);   

        action = on_item_callback(root_path, ptr, token);

        if (E_DIR_ABORT == action)
        {
            *end = 0;
            return E_DIR_ABORT;
        }

        if (ptr->is_folder && E_DIR_SKIP != action)
        {
            _tcscat(end, _T(SSLASH));
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

int DIR_for_each_in_folder(
    TCHAR * path, 
    dir_entry_t * fd, 
    dir_scan_callback_action_t (*on_item_callback) (const TCHAR * path, dir_entry_t * fd, void * token),
    void * token
    )
{
    int status;
    TCHAR * end = path + _tcslen(path);
    _tcscat(end, _T(SSLASH));
    assert(on_item_callback);

    status = for_each_recursion(path, fd->items, on_item_callback, token);
    *end = 0;
    return status;
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
    index = malloc((dir->items_count+1) * sizeof(void*));
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
    if (index) index[dir->items_count] = NULL;
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

dir_entry_t ** DIR_rev_index(dir_directory_t * dir, dir_entry_t ** index)
{
    unsigned i;
#define SWAP(datatype, a, b) { datatype _ = a; a = b; b = _; }
    for (i = 0; i < dir->items_count/2; i++)
    {
        SWAP(dir_entry_t *, index[i], index[dir->items_count-1-i]);
    }
    return index;
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
    /*static */TCHAR path[2][DIR_MAX_PATH];    // too big array for stack
    size_t len[2];
    if (!pp2 && !pp1) return 0;
    if (!pp2) return -1;
    if (!pp1) return 1;
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
#ifdef _WIN32
    BOOL result;
    long  attr;

    TCHAR terminator = *term;
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
#else
    int mode = 0;// TODO: default mode
    struct stat            st;
    int             status = 0;
    TCHAR terminator = *term;
    *term = '\0';

    if (stat(path, &st) != 0)
    {
        /* Directory does not exist. EEXIST for race condition */
        if (mkdir(path, mode) != 0 /*&& errno != EEXIST*/)
            status = -1;
    }
    else if (!S_ISDIR(st.st_mode))
    {
//        errno = ENOTDIR;
        status = -1;
    }
    *term = terminator;

    return(status);
#endif
}


static TCHAR * tchar_clone(const TCHAR * src)
{
    int len = _tcslen(src) + 1;
    TCHAR * dst = (TCHAR *)malloc(len*sizeof(TCHAR));
    if (dst) memcpy(dst, src, len*sizeof(TCHAR));
    return dst;
}

int DIR_force_directory(TCHAR * _path)
{
    TCHAR * p;
    TCHAR term = '\0';
    size_t len;
    int result = 1;
    TCHAR * path = tchar_clone(_path);
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
    free(path);
    return result;
}

/**
*   Change "looooooong/path" to "loo.../path"
*/
TCHAR * PATH_compact_path(TCHAR * compact_path, const TCHAR * path, size_t limit)
{
#if 1
    size_t len, i;
    if (!path) return _T("{NULL}");
    len = _tcslen(path);
    limit--; // terminator
    if (len > limit)
    {
        for (i = 0; i < limit/4; i++)
        {
            int c = path[i];
            compact_path[i] = (TCHAR)c;
            if (c == '\\' || c == '/') {i++; break;}
        }
        _tcscpy(compact_path + i, _T("..."));
        _tcscat (compact_path, path + len - limit + i + 3);
    }
    else
    {
        _tcscpy(compact_path, path);
    }
#else
    *compact_path = '\0';
    PathCompactPathEx(compact_path, path, limit, 0/*flags not used*/);
#endif
    return compact_path;
}




unsigned long DIR_files_count(dir_entry_t * fd, int recursive)
{
    unsigned long count = 0;
    for (fd = fd->items; fd; fd = fd->link)
    {
        count += !fd->is_folder;
        if (fd->is_folder && recursive)
        {
            count += DIR_files_count(fd, recursive);
        }
    }
    return count;
}


/************************************************************************/
/*                File mask list support functions                      */
/************************************************************************/


void DIR_file_mask_list_close(file_mask_t * list)
{
    file_mask_t * next;
    for (; list != NULL; list = next)
    {
        next = list->link;
        free(list);
    }
}

static int file_mask_list_append_mask(file_mask_t ** list, const TCHAR * mask, int is_include)
{
    file_mask_t * item = malloc(sizeof(file_mask_t) + _tcslen(mask)*sizeof(TCHAR));
    if (item)
    {
        item->is_include = is_include;
        _tcscpy(item->mask, mask);
        item->link = *list;
        *list = item;
        return 1;
    }
    return 0;
}

int DIR_file_mask_list_add_include_mask(file_mask_t ** list, const TCHAR * mask)
{
    return file_mask_list_append_mask(list, mask, 1);
}

int DIR_file_mask_list_add_exclude_mask(file_mask_t ** list, const TCHAR * mask)
{
    return file_mask_list_append_mask(list, mask, 0);
}


int DIR_file_mask_list_match_name(file_mask_t * list, const TCHAR * file_name)
{
    int orr_match = 0;
    int have_incl = 0;
    for (; list != NULL; list = list->link)
    {
        // If no include mask specified, assume that any file name match.
        have_incl |= list->is_include;
        if (PATH_mask_match(list->mask, file_name))
        {
            if (!list->is_include)
            {
                return 0;
            }
            orr_match = 1;
        }
    }
    return orr_match || !have_incl;
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
dir_scan_callback_action_t my_print_dir_item(const TCHAR * path, dir_entry_t * fd, void * token)
{
    static int count = 0;
    if (fd->is_folder)
    {
        _tprintf("\n%-30s\t<DIR>", pretty_print_path(pretty_path, path, 30));
    }
    else
    {
        _tprintf("\n%-30s\t%I64d", pretty_print_path(pretty_path, path, 30), fd->size);
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
    _tprintf("\n=======================\n%d items; size %I64d", dir.items_count, dir.files_size);
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
