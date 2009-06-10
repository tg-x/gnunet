/*
     This file is part of GNUnet.
     (C) 2001, 2002, 2005, 2006 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

/**
 * @file util/disk.c
 * @brief disk IO convenience methods
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_common.h"
#include "gnunet_directories.h"
#include "gnunet_disk_lib.h"
#include "gnunet_scheduler_lib.h"
#include "gnunet_strings_lib.h"


#if LINUX || CYGWIN
#include <sys/vfs.h>
#else
#ifdef SOMEBSD
#include <sys/param.h>
#include <sys/mount.h>
#else
#ifdef OSX
#include <sys/param.h>
#include <sys/mount.h>
#else
#ifdef SOLARIS
#include <sys/types.h>
#include <sys/statvfs.h>
#else
#ifdef MINGW
#define  	_IFMT		0170000 /* type of file */
#define  	_IFLNK		0120000 /* symbolic link */
#define  S_ISLNK(m)	(((m)&_IFMT) == _IFLNK)
#else
#error PORT-ME: need to port statfs (how much space is left on the drive?)
#endif
#endif
#endif
#endif
#endif

#ifndef SOMEBSD
#ifndef WINDOWS
#ifndef OSX
#include <wordexp.h>
#endif
#endif
#endif

typedef struct
{
  unsigned long long total;
  int include_sym_links;
} GetFileSizeData;

static int
getSizeRec (void *ptr, const char *fn)
{
  GetFileSizeData *gfsd = ptr;
#ifdef HAVE_STAT64
  struct stat64 buf;
#else
  struct stat buf;
#endif

#ifdef HAVE_STAT64
  if (0 != STAT64 (fn, &buf))
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "stat64", fn);
      return GNUNET_SYSERR;
    }
#else
  if (0 != STAT (fn, &buf))
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "stat", fn);
      return GNUNET_SYSERR;
    }
#endif
  if ((!S_ISLNK (buf.st_mode)) || (gfsd->include_sym_links == GNUNET_YES))
    gfsd->total += buf.st_size;
  if ((S_ISDIR (buf.st_mode)) &&
      (0 == ACCESS (fn, X_OK)) &&
      ((!S_ISLNK (buf.st_mode)) || (gfsd->include_sym_links == GNUNET_YES)))
    {
      if (GNUNET_SYSERR == GNUNET_DISK_directory_scan (fn, &getSizeRec, gfsd))
        return GNUNET_SYSERR;
    }
  return GNUNET_OK;
}

/**
 * Get the size of the file (or directory)
 * of the given file (in bytes).
 *
 * @return GNUNET_SYSERR on error, GNUNET_OK on success
 */
int
GNUNET_DISK_file_size (const char *filename,
                       unsigned long long *size, int includeSymLinks)
{
  GetFileSizeData gfsd;
  int ret;

  GNUNET_assert (size != NULL);
  gfsd.total = 0;
  gfsd.include_sym_links = includeSymLinks;
  ret = getSizeRec (&gfsd, filename);
  *size = gfsd.total;
  return ret;
}

/**
 * Get the number of blocks that are left on the partition that
 * contains the given file (for normal users).
 *
 * @param part a file on the partition to check
 * @return -1 on errors, otherwise the number of free blocks
 */
long
GNUNET_DISK_get_blocks_available (const char *part)
{
#ifdef SOLARIS
  struct statvfs buf;

  if (0 != statvfs (part, &buf))
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "statfs", part);
      return -1;
    }
  return buf.f_bavail;
#elif MINGW
  DWORD dwDummy;
  DWORD dwBlocks;
  char szDrive[4];

  memcpy (szDrive, part, 3);
  szDrive[3] = 0;
  if (!GetDiskFreeSpace (szDrive, &dwDummy, &dwDummy, &dwBlocks, &dwDummy))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                     _("`%s' failed for drive `%s': %u\n"),
                     "GetDiskFreeSpace", szDrive, GetLastError ());

      return -1;
    }
  return dwBlocks;
#else
  struct statfs s;
  if (0 != statfs (part, &s))
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "statfs", part);
      return -1;
    }
  return s.f_bavail;
#endif
}

/**
 * Test if fil is a directory.
 *
 * @return GNUNET_YES if yes, GNUNET_NO if not, GNUNET_SYSERR if it
 *   does not exist
 */
int
GNUNET_DISK_directory_test (const char *fil)
{
  struct stat filestat;
  int ret;

  ret = STAT (fil, &filestat);
  if (ret != 0)
    {
      if (errno != ENOENT)
        {
          GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "stat", fil);
          return GNUNET_SYSERR;
        }
      return GNUNET_NO;
    }
  if (!S_ISDIR (filestat.st_mode))
    return GNUNET_NO;
  if (ACCESS (fil, R_OK | X_OK) < 0)
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "access", fil);
      return GNUNET_SYSERR;
    }
  return GNUNET_YES;
}

/**
 * Check that fil corresponds to a filename
 * (of a file that exists and that is not a directory).
 * @returns GNUNET_YES if yes, GNUNET_NO if not a file, GNUNET_SYSERR if something
 * else (will print an error message in that case, too).
 */
int
GNUNET_DISK_file_test (const char *fil)
{
  struct stat filestat;
  int ret;
  char *rdir;

  rdir = GNUNET_STRINGS_filename_expand (fil);
  if (rdir == NULL)
    return GNUNET_SYSERR;

  ret = STAT (rdir, &filestat);
  if (ret != 0)
    {
      if (errno != ENOENT)
        {
          GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "stat", rdir);
          GNUNET_free (rdir);
          return GNUNET_SYSERR;
        }
      GNUNET_free (rdir);
      return GNUNET_NO;
    }
  if (!S_ISREG (filestat.st_mode))
    {
      GNUNET_free (rdir);
      return GNUNET_NO;
    }
  if (ACCESS (rdir, R_OK) < 0)
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "access", rdir);
      GNUNET_free (rdir);
      return GNUNET_SYSERR;
    }
  GNUNET_free (rdir);
  return GNUNET_YES;
}

/**
 * Implementation of "mkdir -p"
 * @param dir the directory to create
 * @returns GNUNET_OK on success, GNUNET_SYSERR on failure
 */
int
GNUNET_DISK_directory_create (const char *dir)
{
  char *rdir;
  int len;
  int pos;
  int ret = GNUNET_OK;

  rdir = GNUNET_STRINGS_filename_expand (dir);
  if (rdir == NULL)
    return GNUNET_SYSERR;

  len = strlen (rdir);
#ifndef MINGW
  pos = 1;                      /* skip heading '/' */
#else
  /* Local or Network path? */
  if (strncmp (rdir, "\\\\", 2) == 0)
    {
      pos = 2;
      while (rdir[pos])
        {
          if (rdir[pos] == '\\')
            {
              pos++;
              break;
            }
          pos++;
        }
    }
  else
    {
      pos = 3;                  /* strlen("C:\\") */
    }
#endif
  while (pos <= len)
    {
      if ((rdir[pos] == DIR_SEPARATOR) || (pos == len))
        {
          rdir[pos] = '\0';
          ret = GNUNET_DISK_directory_test (rdir);
          if (ret == GNUNET_SYSERR)
            {
              GNUNET_free (rdir);
              return GNUNET_SYSERR;
            }
          if (ret == GNUNET_NO)
            {
#ifndef MINGW
              ret = mkdir (rdir, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);  /* 755 */
#else
              ret = mkdir (rdir);
#endif
              if ((ret != 0) && (errno != EEXIST))
                {
                  GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_ERROR, "mkdir",
                                            rdir);
                  GNUNET_free (rdir);
                  return GNUNET_SYSERR;
                }
            }
          rdir[pos] = DIR_SEPARATOR;
        }
      pos++;
    }
  GNUNET_free (rdir);
  return GNUNET_OK;
}


/**
 * Create the directory structure for storing
 * a file.
 *
 * @param filename name of a file in the directory
 * @returns GNUNET_OK on success,
 *          GNUNET_SYSERR on failure,
 *          GNUNET_NO if the directory
 *          exists but is not writeable for us
 */
int
GNUNET_DISK_directory_create_for_file (const char *dir)
{
  char *rdir;
  int len;
  int ret;

  rdir = GNUNET_STRINGS_filename_expand (dir);
  if (rdir == NULL)
    return GNUNET_SYSERR;
  len = strlen (rdir);
  while ((len > 0) && (rdir[len] != DIR_SEPARATOR))
    len--;
  rdir[len] = '\0';
  ret = GNUNET_DISK_directory_create (rdir);
  if ((ret == GNUNET_OK) && (0 != ACCESS (rdir, W_OK)))
    ret = GNUNET_NO;
  GNUNET_free (rdir);
  return ret;
}

/**
 * Read the contents of a binary file into a buffer.
 * @param fileName the name of the file, not freed,
 *        must already be expanded!
 * @param len the maximum number of bytes to read
 * @param result the buffer to write the result to
 * @return the number of bytes read on success, -1 on failure
 */
int
GNUNET_DISK_file_read (const char *fileName, int len, void *result)
{
  /* open file, must exist, open read only */
  int handle;
  int size;

  GNUNET_assert (fileName != NULL);
  GNUNET_assert (len > 0);
  if (len == 0)
    return 0;
  GNUNET_assert (result != NULL);
  handle = GNUNET_DISK_file_open (fileName, O_RDONLY, S_IRUSR);
  if (handle < 0)
    return -1;
  size = READ (handle, result, len);
  GNUNET_DISK_file_close (fileName, handle);
  return size;
}


/**
 * Convert string to value ('755' for chmod-call)
 */
static int
atoo (const char *s)
{
  int n = 0;

  while (('0' <= *s) && (*s < '8'))
    {
      n <<= 3;
      n += *s++ - '0';
    }
  return n;
}

/**
 * Write a buffer to a file.
 * @param fileName the name of the file, NOT freed!
 * @param buffer the data to write
 * @param n number of bytes to write
 * @param mode permissions to set on the file
 * @return GNUNET_OK on success, GNUNET_SYSERR on error
 */
int
GNUNET_DISK_file_write (const char *fileName,
                        const void *buffer, unsigned int n, const char *mode)
{
  int handle;
  char *fn;

  /* open file, open with 600, create if not
     present, otherwise overwrite */
  GNUNET_assert (fileName != NULL);
  fn = GNUNET_STRINGS_filename_expand (fileName);
  handle = GNUNET_DISK_file_open (fn, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
  if (handle == -1)
    {
      GNUNET_free (fn);
      return GNUNET_SYSERR;
    }
  GNUNET_assert ((n == 0) || (buffer != NULL));
  /* write the buffer take length from the beginning */
  if (n != WRITE (handle, buffer, n))
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "write", fn);
      GNUNET_DISK_file_close (fn, handle);
      GNUNET_free (fn);
      return GNUNET_SYSERR;
    }
  GNUNET_DISK_file_close (fn, handle);
  if (0 != CHMOD (fn, atoo (mode)))
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "chmod", fn);
    }
  GNUNET_free (fn);
  return GNUNET_OK;
}

/**
 * Scan a directory for files. The name of the directory
 * must be expanded first (!).
 * @param dirName the name of the directory
 * @param callback the method to call for each file,
 *        can be NULL, in that case, we only count
 * @param data argument to pass to callback
 * @return the number of files found, GNUNET_SYSERR on error or
 *         ieration aborted by callback returning GNUNET_SYSERR
 */
int
GNUNET_DISK_directory_scan (const char *dirName,
                            GNUNET_FileNameCallback callback, void *data)
{
  DIR *dinfo;
  struct dirent *finfo;
  struct stat istat;
  int count = 0;
  char *name;
  char *dname;
  unsigned int name_len;
  unsigned int n_size;

  GNUNET_assert (dirName != NULL);
  dname = GNUNET_STRINGS_filename_expand (dirName);
  while ((strlen (dname) > 0) && (dname[strlen (dname) - 1] == DIR_SEPARATOR))
    dname[strlen (dname) - 1] = '\0';
  if (0 != STAT (dname, &istat))
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "stat", dname);
      GNUNET_free (dname);
      return GNUNET_SYSERR;
    }
  if (!S_ISDIR (istat.st_mode))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                  _("Expected `%s' to be a directory!\n"), dirName);
      GNUNET_free (dname);
      return GNUNET_SYSERR;
    }
  errno = 0;
  dinfo = OPENDIR (dname);
  if ((errno == EACCES) || (dinfo == NULL))
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "opendir", dname);
      if (dinfo != NULL)
        closedir (dinfo);
      GNUNET_free (dname);
      return GNUNET_SYSERR;
    }
  name_len = 256;
  n_size = strlen (dname) + name_len + 2;
  name = GNUNET_malloc (n_size);
  while ((finfo = readdir (dinfo)) != NULL)
    {
      if ((0 == strcmp (finfo->d_name, ".")) ||
          (0 == strcmp (finfo->d_name, "..")))
        continue;
      if (callback != NULL)
        {
          if (name_len < strlen (finfo->d_name))
            {
              GNUNET_free (name);
              name_len = strlen (finfo->d_name);
              n_size = strlen (dname) + name_len + 2;
              name = GNUNET_malloc (n_size);
            }
          /* dname can end in "/" only if dname == "/";
             if dname does not end in "/", we need to add
             a "/" (otherwise, we must not!) */
          GNUNET_snprintf (name,
                           n_size,
                           "%s%s%s",
                           dname,
                           (strcmp (dname, DIR_SEPARATOR_STR) ==
                            0) ? "" : DIR_SEPARATOR_STR, finfo->d_name);
          if (GNUNET_OK != callback (data, name))
            {
              closedir (dinfo);
              GNUNET_free (name);
              GNUNET_free (dname);
              return GNUNET_SYSERR;
            }
        }
      count++;
    }
  closedir (dinfo);
  GNUNET_free (name);
  GNUNET_free (dname);
  return count;
}


/**
 * Opaque handle used for iterating over a directory.
 */
struct GNUNET_DISK_DirectoryIterator
{
  /**
   * Our scheduler.
   */
  struct GNUNET_SCHEDULER_Handle *sched;

  /**
   * Function to call on directory entries.
   */
  GNUNET_DISK_DirectoryIteratorCallback callback;

  /**
   * Closure for callback.
   */
  void *callback_cls;

  /**
   * Reference to directory.
   */
  DIR *directory;

  /**
   * Directory name.
   */
  char *dirname;

  /**
   * Next filename to process.
   */
  char *next_name;

  /**
   * Our priority.
   */
  enum GNUNET_SCHEDULER_Priority priority;

};


/**
 * Task used by the directory iterator.
 */
static void
directory_iterator_task (void *cls,
                         const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_DISK_DirectoryIterator *iter = cls;
  char *name;

  name = iter->next_name;
  GNUNET_assert (name != NULL);
  iter->next_name = NULL;
  iter->callback (iter->callback_cls, iter, name, iter->dirname);
  GNUNET_free (name);
}


/**
 * This function must be called during the DiskIteratorCallback
 * (exactly once) to schedule the task to process the next
 * filename in the directory (if there is one).
 *
 * @param iter opaque handle for the iterator
 * @param can set to GNUNET_YES to terminate the iteration early
 * @return GNUNET_YES if iteration will continue,
 *         GNUNET_NO if this was the last entry (and iteration is complete),
 *         GNUNET_SYSERR if abort was YES
 */
int
GNUNET_DISK_directory_iterator_next (struct GNUNET_DISK_DirectoryIterator
                                     *iter, int can)
{
  struct dirent *finfo;

  GNUNET_assert (iter->next_name == NULL);
  if (can == GNUNET_YES)
    {
      closedir (iter->directory);
      GNUNET_free (iter->dirname);
      GNUNET_free (iter);
      return GNUNET_SYSERR;
    }
  while (NULL != (finfo = readdir (iter->directory)))
    {
      if ((0 == strcmp (finfo->d_name, ".")) ||
          (0 == strcmp (finfo->d_name, "..")))
        continue;
      GNUNET_asprintf (&iter->next_name,
                       "%s%s%s",
                       iter->dirname, DIR_SEPARATOR_STR, finfo->d_name);
      break;
    }
  if (finfo == NULL)
    {
      GNUNET_DISK_directory_iterator_next (iter, GNUNET_YES);
      return GNUNET_NO;
    }
  GNUNET_SCHEDULER_add_after (iter->sched,
                              GNUNET_YES,
                              iter->priority,
                              GNUNET_SCHEDULER_NO_PREREQUISITE_TASK,
                              &directory_iterator_task, iter);
  return GNUNET_YES;
}


/**
 * Scan a directory for files using the scheduler to run a task for
 * each entry.  The name of the directory must be expanded first (!).
 * If a scheduler does not need to be used, GNUNET_DISK_directory_scan
 * may provide a simpler API.
 *
 * @param sched scheduler to use
 * @param prio priority to use
 * @param dirName the name of the directory
 * @param callback the method to call for each file
 * @param callback_cls closure for callback
 */
void
GNUNET_DISK_directory_iterator_start (struct GNUNET_SCHEDULER_Handle *sched,
                                      enum GNUNET_SCHEDULER_Priority prio,
                                      const char *dirName,
                                      GNUNET_DISK_DirectoryIteratorCallback
                                      callback, void *callback_cls)
{
  struct GNUNET_DISK_DirectoryIterator *di;

  di = GNUNET_malloc (sizeof (struct GNUNET_DISK_DirectoryIterator));
  di->sched = sched;
  di->callback = callback;
  di->callback_cls = callback_cls;
  di->directory = OPENDIR (dirName);
  di->dirname = GNUNET_strdup (dirName);
  di->priority = prio;
  GNUNET_DISK_directory_iterator_next (di, GNUNET_NO);
}


static int
remove_helper (void *unused, const char *fn)
{
  GNUNET_DISK_directory_remove (fn);
  return GNUNET_OK;
}

/**
 * Remove all files in a directory (rm -rf). Call with
 * caution.
 *
 *
 * @param fileName the file to remove
 * @return GNUNET_OK on success, GNUNET_SYSERR on error
 */
int
GNUNET_DISK_directory_remove (const char *fileName)
{
  struct stat istat;

  if (0 != LSTAT (fileName, &istat))
    return GNUNET_NO;           /* file may not exist... */
  if (UNLINK (fileName) == 0)
    return GNUNET_OK;
  if ((errno != EISDIR) &&
      /* EISDIR is not sufficient in all cases, e.g.
         sticky /tmp directory may result in EPERM on BSD.
         So we also explicitly check "isDirectory" */
      (GNUNET_YES != GNUNET_DISK_directory_test (fileName)))
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "rmdir", fileName);
      return GNUNET_SYSERR;
    }
  if (GNUNET_SYSERR ==
      GNUNET_DISK_directory_scan (fileName, remove_helper, NULL))
    return GNUNET_SYSERR;
  if (0 != RMDIR (fileName))
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "rmdir", fileName);
      return GNUNET_SYSERR;
    }
  return GNUNET_OK;
}

void
GNUNET_DISK_file_close (const char *filename, int fd)
{
  if (0 != CLOSE (fd))
    GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "close", filename);
}

int
GNUNET_DISK_file_open (const char *filename, int oflag, ...)
{
  char *fn;
  int mode;
  int ret;
#ifdef MINGW
  char szFile[_MAX_PATH + 1];
  long lRet;

  if ((lRet = plibc_conv_to_win_path (filename, szFile)) != ERROR_SUCCESS)
    {
      errno = ENOENT;
      SetLastError (lRet);
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING,
                                "plibc_conv_to_win_path", filename);
      return -1;
    }
  fn = GNUNET_strdup (szFile);
#else
  fn = GNUNET_STRINGS_filename_expand (filename);
#endif
  if (oflag & O_CREAT)
    {
      va_list arg;
      va_start (arg, oflag);
      mode = va_arg (arg, int);
      va_end (arg);
    }
  else
    {
      mode = 0;
    }
#ifdef MINGW
  /* set binary mode */
  oflag |= O_BINARY;
#endif
  ret = OPEN (fn, oflag, mode);
  if (ret == -1)
    GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "open", fn);
  GNUNET_free (fn);
  return ret;
}

#define COPY_BLK_SIZE 65536

/**
 * Copy a file.
 * @return GNUNET_OK on success, GNUNET_SYSERR on error
 */
int
GNUNET_DISK_file_copy (const char *src, const char *dst)
{
  char *buf;
  unsigned long long pos;
  unsigned long long size;
  unsigned long long len;
  int in;
  int out;

  if (GNUNET_OK != GNUNET_DISK_file_size (src, &size, GNUNET_YES))
    return GNUNET_SYSERR;
  pos = 0;
  in = GNUNET_DISK_file_open (src, O_RDONLY | O_LARGEFILE);
  if (in == -1)
    return GNUNET_SYSERR;
  out = GNUNET_DISK_file_open (dst,
                               O_LARGEFILE | O_WRONLY | O_CREAT | O_EXCL,
                               S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (out == -1)
    {
      GNUNET_DISK_file_close (src, in);
      return GNUNET_SYSERR;
    }
  buf = GNUNET_malloc (COPY_BLK_SIZE);
  while (pos < size)
    {
      len = COPY_BLK_SIZE;
      if (len > size - pos)
        len = size - pos;
      if (len != READ (in, buf, len))
        goto FAIL;
      if (len != WRITE (out, buf, len))
        goto FAIL;
      pos += len;
    }
  GNUNET_free (buf);
  GNUNET_DISK_file_close (src, in);
  GNUNET_DISK_file_close (dst, out);
  return GNUNET_OK;
FAIL:
  GNUNET_free (buf);
  GNUNET_DISK_file_close (src, in);
  GNUNET_DISK_file_close (dst, out);
  return GNUNET_SYSERR;
}


/**
 * @brief Removes special characters as ':' from a filename.
 * @param fn the filename to canonicalize
 */
void
GNUNET_DISK_filename_canonicalize (char *fn)
{
  char *idx;
  char c;

  idx = fn;
  while (*idx)
    {
      c = *idx;

      if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
          c == '"' || c == '<' || c == '>' || c == '|')
        {
          *idx = '_';
        }

      idx++;
    }
}



/**
 * @brief Change owner of a file
 */
int
GNUNET_DISK_file_change_owner (const char *filename, const char *user)
{
#ifndef MINGW
  struct passwd *pws;

  pws = getpwnam (user);
  if (pws == NULL)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  _("Cannot obtain information about user `%s': %s\n"),
                  user, STRERROR (errno));
      return GNUNET_SYSERR;
    }
  if (0 != chown (filename, pws->pw_uid, pws->pw_gid))
    GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING, "chown", filename);
#endif
  return GNUNET_OK;
}


/**
 * Construct full path to a file inside of the private
 * directory used by GNUnet.  Also creates the corresponding
 * directory.  If the resulting name is supposed to be
 * a directory, end the last argument in '/' (or pass
 * DIR_SEPARATOR_STR as the last argument before NULL).
 *
 * @param serviceName name of the service
 * @param varargs is NULL-terminated list of
 *                path components to append to the
 *                private directory name.
 * @return the constructed filename
 */
char *
GNUNET_DISK_get_home_filename (struct GNUNET_CONFIGURATION_Handle *cfg,
                               const char *serviceName, ...)
{
  const char *c;
  char *pfx;
  char *ret;
  va_list ap;
  unsigned int needed;

  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_filename (cfg,
                                               serviceName, "HOME", &pfx))
    return NULL;
  if (pfx == NULL)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                  _("No `%s' specified for service `%s' in configuration.\n"),
                  "HOME", serviceName);
      return NULL;
    }
  needed = strlen (pfx) + 2;
  if ((pfx[strlen (pfx) - 1] != '/') && (pfx[strlen (pfx) - 1] != '\\'))
    needed++;
  va_start (ap, serviceName);
  while (1)
    {
      c = va_arg (ap, const char *);
      if (c == NULL)
        break;
      needed += strlen (c);
      if ((c[strlen (c) - 1] != '/') && (c[strlen (c) - 1] != '\\'))
        needed++;
    }
  va_end (ap);
  ret = GNUNET_malloc (needed);
  strcpy (ret, pfx);
  GNUNET_free (pfx);
  va_start (ap, serviceName);
  while (1)
    {
      c = va_arg (ap, const char *);
      if (c == NULL)
        break;
      if ((c[strlen (c) - 1] != '/') && (c[strlen (c) - 1] != '\\'))
        strcat (ret, DIR_SEPARATOR_STR);
      strcat (ret, c);
    }
  va_end (ap);
  if ((ret[strlen (ret) - 1] != '/') && (ret[strlen (ret) - 1] != '\\'))
    GNUNET_DISK_directory_create_for_file (ret);
  else
    GNUNET_DISK_directory_create (ret);
  return ret;
}



/* end of disk.c */
