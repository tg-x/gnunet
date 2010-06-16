/*
     This file is part of GNUnet.
     (C) 2009 Christian Grothoff (and other contributing authors)

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
 * @file fs/fs_file_information.c
 * @brief  Manage information for publishing directory hierarchies
 * @author Christian Grothoff
 *
 * TODO:
 * - serialization/deserialization (& deserialization API)
 * - metadata filename clean up code
 * - metadata/ksk generation for directories from contained files
 */
#include "platform.h"
#include <extractor.h>
#include "gnunet_fs_service.h"
#include "fs.h"
#include "fs_tree.h"


/**
 * Add meta data that libextractor finds to our meta data
 * container.
 *
 * @param cls closure, our meta data container
 * @param plugin_name name of the plugin that produced this value;
 *        special values can be used (i.e. '&lt;zlib&gt;' for zlib being
 *        used in the main libextractor library and yielding
 *        meta data).
 * @param type libextractor-type describing the meta data
 * @param format basic format information about data
 * @param data_mime_type mime-type of data (not of the original file);
 *        can be NULL (if mime-type is not known)
 * @param data actual meta-data found
 * @param data_len number of bytes in data
 * @return always 0 to continue extracting
 */
static int
add_to_md(void *cls,
	  const char *plugin_name,
	  enum EXTRACTOR_MetaType type,
	  enum EXTRACTOR_MetaFormat format,
	  const char *data_mime_type,
	  const char *data,
	  size_t data_len)
{
  struct GNUNET_CONTAINER_MetaData *md = cls;
  (void) GNUNET_CONTAINER_meta_data_insert (md,
					    plugin_name,
					    type,
					    format,
					    data_mime_type,
					    data,
					    data_len);
  return 0;
}


/**
 * Extract meta-data from a file.
 *
 * @return GNUNET_SYSERR on error, otherwise the number
 *   of meta-data items obtained
 */
int
GNUNET_FS_meta_data_extract_from_file (struct GNUNET_CONTAINER_MetaData
				       *md, const char *filename,
				       struct EXTRACTOR_PluginList *
				       extractors)
{
  int old;

  if (filename == NULL)
    return GNUNET_SYSERR;
  if (extractors == NULL)
    return 0;
  old = GNUNET_CONTAINER_meta_data_iterate (md, NULL, NULL);
  GNUNET_assert (old >= 0);
  EXTRACTOR_extract (extractors, 
		     filename,
		     NULL, 0,
		     &add_to_md,
		     md);
  return (GNUNET_CONTAINER_meta_data_iterate (md, NULL, NULL) - old);
}



/**
 * Obtain the name under which this file information
 * structure is stored on disk.  Only works for top-level
 * file information structures.
 *
 * @param s structure to get the filename for
 * @return NULL on error, otherwise filename that
 *         can be passed to "GNUNET_FS_file_information_recover"
 *         to read this fi-struct from disk.
 */
const char *
GNUNET_FS_file_information_get_id (struct GNUNET_FS_FileInformation *s)
{
  if (NULL != s->dir)
    return NULL;
  return s->serialization;
}


/**
 * Create an entry for a file in a publish-structure.
 *
 * @param h handle to the file sharing subsystem
 * @param client_info initial value for the client-info value for this entry
 * @param filename name of the file or directory to publish
 * @param keywords under which keywords should this file be available
 *         directly; can be NULL
 * @param meta metadata for the file
 * @param do_index GNUNET_YES for index, GNUNET_NO for insertion,
 *                GNUNET_SYSERR for simulation
 * @param anonymity what is the desired anonymity level for sharing?
 * @param priority what is the priority for OUR node to
 *   keep this file available?  Use 0 for maximum anonymity and
 *   minimum reliability...
 * @param expirationTime when should this content expire?
 * @return publish structure entry for the file
 */
struct GNUNET_FS_FileInformation *
GNUNET_FS_file_information_create_from_file (struct GNUNET_FS_Handle *h,
					     void *client_info,
					     const char *filename,
					     const struct GNUNET_FS_Uri *keywords,
					     const struct GNUNET_CONTAINER_MetaData *meta,
					     int do_index,
					     uint32_t anonymity,
					     uint32_t priority,
					     struct GNUNET_TIME_Absolute expirationTime)
{
  struct FileInfo *fi;
  struct stat sbuf;
  struct GNUNET_FS_FileInformation *ret;
  const char *fn;
  const char *ss;

  if (0 != STAT (filename, &sbuf))
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING,
				"stat",
				filename);
      return NULL;
    }
  fi = GNUNET_FS_make_file_reader_context_ (filename);
  if (fi == NULL)
    {
      GNUNET_break (0);
      return NULL;
    }
  ret = GNUNET_FS_file_information_create_from_reader (h,
						       client_info,
						       sbuf.st_size,
						       &GNUNET_FS_data_reader_file_,
						       fi,
						       keywords,
						       meta,
						       do_index,
						       anonymity,
						       priority,
						       expirationTime);
  ret->h = h;
  ret->filename = GNUNET_strdup (filename);
  fn = filename;
  while (NULL != (ss = strstr (fn,
			       DIR_SEPARATOR_STR)))
    fn = ss + 1;
  GNUNET_CONTAINER_meta_data_insert (ret->meta,
				     "<gnunet>",
				     EXTRACTOR_METATYPE_FILENAME,
				     EXTRACTOR_METAFORMAT_C_STRING,
				     "text/plain",
				     fn,
				     strlen (fn) + 1);
  return ret;
}


/**
 * Create an entry for a file in a publish-structure.
 *
 * @param h handle to the file sharing subsystem
 * @param client_info initial value for the client-info value for this entry
 * @param length length of the file
 * @param data data for the file (should not be used afterwards by
 *        the caller; callee will "free")
 * @param keywords under which keywords should this file be available
 *         directly; can be NULL
 * @param meta metadata for the file
 * @param do_index GNUNET_YES for index, GNUNET_NO for insertion,
 *                GNUNET_SYSERR for simulation
 * @param anonymity what is the desired anonymity level for sharing?
 * @param priority what is the priority for OUR node to
 *   keep this file available?  Use 0 for maximum anonymity and
 *   minimum reliability...
 * @param expirationTime when should this content expire?
 * @return publish structure entry for the file
 */
struct GNUNET_FS_FileInformation *
GNUNET_FS_file_information_create_from_data (struct GNUNET_FS_Handle *h,
					     void *client_info,
					     uint64_t length,
					     void *data,
					     const struct GNUNET_FS_Uri *keywords,
					     const struct GNUNET_CONTAINER_MetaData *meta,
					     int do_index,
					     uint32_t anonymity,
					     uint32_t priority,
					     struct GNUNET_TIME_Absolute expirationTime)
{
  if (GNUNET_YES == do_index)        
    {
      GNUNET_break (0);
      return NULL;
    }
  return GNUNET_FS_file_information_create_from_reader (h,
							client_info,
							length,
							&GNUNET_FS_data_reader_copy_,
							data,
							keywords,
							meta,
							do_index,
							anonymity,
							priority,
							expirationTime);
}


/**
 * Create an entry for a file in a publish-structure.
 *
 * @param h handle to the file sharing subsystem
 * @param client_info initial value for the client-info value for this entry
 * @param length length of the file
 * @param reader function that can be used to obtain the data for the file 
 * @param reader_cls closure for "reader"
 * @param keywords under which keywords should this file be available
 *         directly; can be NULL
 * @param meta metadata for the file
 * @param do_index GNUNET_YES for index, GNUNET_NO for insertion,
 *                GNUNET_SYSERR for simulation
 * @param anonymity what is the desired anonymity level for sharing?
 * @param priority what is the priority for OUR node to
 *   keep this file available?  Use 0 for maximum anonymity and
 *   minimum reliability...
 * @param expirationTime when should this content expire?
 * @return publish structure entry for the file
 */
struct GNUNET_FS_FileInformation *
GNUNET_FS_file_information_create_from_reader (struct GNUNET_FS_Handle *h,
					       void *client_info,
					       uint64_t length,
					       GNUNET_FS_DataReader reader,
					       void *reader_cls,
					       const struct GNUNET_FS_Uri *keywords,
					       const struct GNUNET_CONTAINER_MetaData *meta,
					       int do_index,
					       uint32_t anonymity,
					       uint32_t priority,
					       struct GNUNET_TIME_Absolute expirationTime)
{
  struct GNUNET_FS_FileInformation *ret;

  if ( (GNUNET_YES == do_index) &&
       (reader != &GNUNET_FS_data_reader_file_) )
    {
      GNUNET_break (0);
      return NULL;
    }
  ret = GNUNET_malloc (sizeof (struct GNUNET_FS_FileInformation));
  ret->h = h;
  ret->client_info = client_info;  
  ret->meta = GNUNET_CONTAINER_meta_data_duplicate (meta);
  if (ret->meta == NULL)
    ret->meta = GNUNET_CONTAINER_meta_data_create ();
  ret->keywords = (keywords == NULL) ? NULL : GNUNET_FS_uri_dup (keywords);
  ret->expirationTime = expirationTime;
  ret->data.file.reader = reader; 
  ret->data.file.reader_cls = reader_cls;
  ret->data.file.do_index = do_index;
  ret->data.file.file_size = length;
  ret->anonymity = anonymity;
  ret->priority = priority;
  return ret;
}


/**
 * Closure for "dir_scan_cb".
 */
struct DirScanCls 
{
  /**
   * Metadata extractors to use.
   */
  struct EXTRACTOR_PluginList *extractors;

  /**
   * Master context.
   */ 
  struct GNUNET_FS_Handle *h;

  /**
   * Function to call on each directory entry.
   */
  GNUNET_FS_FileProcessor proc;
  
  /**
   * Closure for proc.
   */
  void *proc_cls;

  /**
   * Scanner to use for subdirectories.
   */
  GNUNET_FS_DirectoryScanner scanner;

  /**
   * Closure for scanner.
   */
  void *scanner_cls;

  /**
   * Set to an error message (if any).
   */
  char *emsg; 

  /**
   * Should files be indexed?
   */ 
  int do_index;

  /**
   * Desired anonymity level.
   */
  uint32_t anonymity;

  /**
   * Desired publishing priority.
   */
  uint32_t priority;

  /**
   * Expiration time for publication.
   */
  struct GNUNET_TIME_Absolute expiration;
};


/**
 * Function called on each entry in a file to
 * cause default-publishing.
 * @param cls closure (struct DirScanCls)
 * @param filename name of the file to be published
 * @return GNUNET_OK on success, GNUNET_SYSERR to abort
 */
static int
dir_scan_cb (void *cls,
	     const char *filename)
{
  struct DirScanCls *dsc = cls;  
  struct stat sbuf;
  struct GNUNET_FS_FileInformation *fi;
  struct GNUNET_FS_Uri *ksk_uri;
  struct GNUNET_FS_Uri *keywords;
  struct GNUNET_CONTAINER_MetaData *meta;

  if (0 != STAT (filename, &sbuf))
    {
      GNUNET_asprintf (&dsc->emsg,
		       _("`%s' failed on file `%s': %s"),
		       "stat",
		       filename,
		       STRERROR (errno));
      return GNUNET_SYSERR;
    }
  if (S_ISDIR (sbuf.st_mode))
    {
      fi = GNUNET_FS_file_information_create_from_directory (dsc->h,
							     NULL,
							     filename,
							     dsc->scanner,
							     dsc->scanner_cls,
							     dsc->do_index,
							     dsc->anonymity,
							     dsc->priority,
							     dsc->expiration,
							     &dsc->emsg);
      if (NULL == fi)
	{
	  GNUNET_assert (NULL != dsc->emsg);
	  return GNUNET_SYSERR;
	}
    }
  else
    {
      meta = GNUNET_CONTAINER_meta_data_create ();
      GNUNET_FS_meta_data_extract_from_file (meta,
					     filename,
					     dsc->extractors);
      // FIXME: remove path from filename in metadata!
      keywords = GNUNET_FS_uri_ksk_create_from_meta_data (meta);
      ksk_uri = GNUNET_FS_uri_ksk_canonicalize (keywords);
      fi = GNUNET_FS_file_information_create_from_file (dsc->h,
							NULL,
							filename,
							ksk_uri,
							meta,
							dsc->do_index,
							dsc->anonymity,
							dsc->priority,
							dsc->expiration);
      GNUNET_CONTAINER_meta_data_destroy (meta);
      GNUNET_FS_uri_destroy (keywords);
      GNUNET_FS_uri_destroy (ksk_uri);
    }
  dsc->proc (dsc->proc_cls,
	     filename,
	     fi);
  return GNUNET_OK;
}


/**
 * Simple, useful default implementation of a directory scanner
 * (GNUNET_FS_DirectoryScanner).  This implementation expects to get a
 * UNIX filename, will publish all files in the directory except hidden
 * files (those starting with a ".").  Metadata will be extracted
 * using GNU libextractor; the specific list of plugins should be
 * specified in "cls", passing NULL will disable (!)  metadata
 * extraction.  Keywords will be derived from the metadata and be
 * subject to default canonicalization.  This is strictly a
 * convenience function.
 *
 * @param cls must be of type "struct EXTRACTOR_Extractor*"
 * @param h handle to the file sharing subsystem
 * @param dirname name of the directory to scan
 * @param do_index should files be indexed or inserted
 * @param anonymity desired anonymity level
 * @param priority priority for publishing
 * @param expirationTime expiration for publication
 * @param proc function called on each entry
 * @param proc_cls closure for proc
 * @param emsg where to store an error message (on errors)
 * @return GNUNET_OK on success
 */
int
GNUNET_FS_directory_scanner_default (void *cls,
				     struct GNUNET_FS_Handle *h,
				     const char *dirname,
				     int do_index,
				     uint32_t anonymity,
				     uint32_t priority,
				     struct GNUNET_TIME_Absolute expirationTime,
				     GNUNET_FS_FileProcessor proc,
				     void *proc_cls,
				     char **emsg)
{
  struct EXTRACTOR_PluginList *ex = cls;
  struct DirScanCls dsc;

  dsc.h = h;
  dsc.extractors = ex;
  dsc.proc = proc;
  dsc.proc_cls = proc_cls;
  dsc.scanner = &GNUNET_FS_directory_scanner_default;
  dsc.scanner_cls = cls;
  dsc.do_index = do_index;
  dsc.anonymity = anonymity;
  dsc.priority = priority;
  dsc.expiration = expirationTime;
  if (-1 == GNUNET_DISK_directory_scan (dirname,
					&dir_scan_cb,
					&dsc))
    {
      GNUNET_assert (NULL != dsc.emsg);
      *emsg = dsc.emsg;
      return GNUNET_SYSERR;
    }
  return GNUNET_OK;
}


/**
 * Closure for dirproc function.
 */
struct EntryProcCls
{
  /**
   * Linked list of directory entries that is being
   * created.
   */
  struct GNUNET_FS_FileInformation *entries;

};


/**
 * Function that processes a directory entry that
 * was obtained from the scanner.
 * @param cls our closure
 * @param filename name of the file (unused, why there???)
 * @param fi information for publishing the file
 */
static void
dirproc (void *cls,
	 const char *filename,
	 struct GNUNET_FS_FileInformation *fi)
{
  struct EntryProcCls *dc = cls;

  GNUNET_assert (fi->next == NULL);
  GNUNET_assert (fi->dir == NULL);
  fi->next = dc->entries;
  dc->entries = fi;
}


/**
 * Create a publish-structure from an existing file hierarchy, inferring
 * and organizing keywords and metadata as much as possible.  This
 * function primarily performs the recursive build and re-organizes
 * keywords and metadata; for automatically getting metadata
 * extraction, scanning of directories and creation of the respective
 * GNUNET_FS_FileInformation entries the default scanner should be
 * passed (GNUNET_FS_directory_scanner_default).  This is strictly a
 * convenience function.
 *
 * @param h handle to the file sharing subsystem
 * @param client_info initial value for the client-info value for this entry
 * @param filename name of the top-level file or directory
 * @param scanner function used to get a list of files in a directory
 * @param scanner_cls closure for scanner
 * @param do_index should files in the hierarchy be indexed?
 * @param anonymity what is the desired anonymity level for sharing?
 * @param priority what is the priority for OUR node to
 *   keep this file available?  Use 0 for maximum anonymity and
 *   minimum reliability...
 * @param expirationTime when should this content expire?
 * @param emsg where to store an error message
 * @return publish structure entry for the directory, NULL on error
 */
struct GNUNET_FS_FileInformation *
GNUNET_FS_file_information_create_from_directory (struct GNUNET_FS_Handle *h,
						  void *client_info,
						  const char *filename,
						  GNUNET_FS_DirectoryScanner scanner,
						  void *scanner_cls,
						  int do_index,
						  uint32_t anonymity,
						  uint32_t priority,
						  struct GNUNET_TIME_Absolute expirationTime,
						  char **emsg)
{
  struct GNUNET_FS_FileInformation *ret;
  struct EntryProcCls dc;
  struct GNUNET_FS_Uri *ksk;
  struct GNUNET_CONTAINER_MetaData *meta;
  const char *fn;
  const char *ss;

  dc.entries = NULL;
  meta = GNUNET_CONTAINER_meta_data_create ();
  GNUNET_FS_meta_data_make_directory (meta);
  scanner (scanner_cls,
	   h,
	   filename,
	   do_index,
	   anonymity,
	   priority,
	   expirationTime,
	   &dirproc,
	   &dc,
	   emsg);
  ksk = NULL; // FIXME...
  // FIXME: create meta!
  ret = GNUNET_FS_file_information_create_empty_directory (h,
							   client_info,
							   ksk,
							   meta,
							   anonymity,
							   priority,
							   expirationTime);
  GNUNET_CONTAINER_meta_data_destroy (meta);
  ret->data.dir.entries = dc.entries;
  while (dc.entries != NULL)
    {
      dc.entries->dir = ret;
      dc.entries = dc.entries->next;
    }
  fn = filename;
  while (NULL != (ss = strstr (fn,
			       DIR_SEPARATOR_STR)))
    fn = ss + 1;
  GNUNET_CONTAINER_meta_data_insert (ret->meta,
				     "<gnunet>",
				     EXTRACTOR_METATYPE_FILENAME,
				     EXTRACTOR_METAFORMAT_C_STRING,
				     "text/plain",
				     fn,
				     strlen (fn) + 1);
  ret->filename = GNUNET_strdup (filename);
  return ret;
}


/**
 * Test if a given entry represents a directory.
 *
 * @param ent check if this FI represents a directory
 * @return GNUNET_YES if so, GNUNET_NO if not
 */
int
GNUNET_FS_file_information_is_directory (struct GNUNET_FS_FileInformation *ent)
{
  return ent->is_directory;
}


/**
 * Create an entry for an empty directory in a publish-structure.
 * This function should be used by applications for which the
 * use of "GNUNET_FS_file_information_create_from_directory"
 * is not appropriate.
 *
 * @param h handle to the file sharing subsystem
 * @param client_info initial value for the client-info value for this entry
 * @param meta metadata for the directory
 * @param keywords under which keywords should this directory be available
 *         directly; can be NULL
 * @param anonymity what is the desired anonymity level for sharing?
 * @param priority what is the priority for OUR node to
 *   keep this file available?  Use 0 for maximum anonymity and
 *   minimum reliability...
 * @param expirationTime when should this content expire?
 * @return publish structure entry for the directory , NULL on error
 */
struct GNUNET_FS_FileInformation *
GNUNET_FS_file_information_create_empty_directory (struct GNUNET_FS_Handle *h,
						   void *client_info,
						   const struct GNUNET_FS_Uri *keywords,
						   const struct GNUNET_CONTAINER_MetaData *meta,
						   uint32_t anonymity,
						   uint32_t priority,
						   struct GNUNET_TIME_Absolute expirationTime)
{
  struct GNUNET_FS_FileInformation *ret;

  ret = GNUNET_malloc (sizeof (struct GNUNET_FS_FileInformation));
  ret->h = h;
  ret->client_info = client_info;
  ret->meta = GNUNET_CONTAINER_meta_data_duplicate (meta);
  ret->keywords = GNUNET_FS_uri_dup (keywords);
  ret->expirationTime = expirationTime;
  ret->is_directory = GNUNET_YES;
  ret->anonymity = anonymity;
  ret->priority = priority;
  return ret;
}


/**
 * Add an entry to a directory in a publish-structure.  Clients
 * should never modify publish structures that were passed to
 * "GNUNET_FS_publish_start" already.
 *
 * @param dir the directory
 * @param ent the entry to add; the entry must not have been
 *            added to any other directory at this point and 
 *            must not include "dir" in its structure
 * @return GNUNET_OK on success, GNUNET_SYSERR on error
 */
int
GNUNET_FS_file_information_add (struct GNUNET_FS_FileInformation *dir,
				struct GNUNET_FS_FileInformation *ent)
{
  if ( (ent->dir != NULL) ||
       (ent->next != NULL) ||
       (! dir->is_directory) )
    {
      GNUNET_break (0);
      return GNUNET_SYSERR;
    }
  ent->dir = dir;
  ent->next = dir->data.dir.entries;
  dir->data.dir.entries = ent;
  dir->data.dir.dir_size = 0;
  return GNUNET_OK;
}


/**
 * Inspect a file or directory in a publish-structure.  Clients
 * should never modify publish structures that were passed to
 * "GNUNET_FS_publish_start" already.  When called on a directory,
 * this function will FIRST call "proc" with information about
 * the directory itself and then for each of the files in the
 * directory (but not for files in subdirectories).  When called
 * on a file, "proc" will be called exactly once (with information
 * about the specific file).
 *
 * @param dir the directory
 * @param proc function to call on each entry
 * @param proc_cls closure for proc
 */
void
GNUNET_FS_file_information_inspect (struct GNUNET_FS_FileInformation *dir,
				    GNUNET_FS_FileInformationProcessor proc,
				    void *proc_cls)
{
  struct GNUNET_FS_FileInformation *pos;
  int no;

  no = GNUNET_NO;
  if (GNUNET_OK !=
      proc (proc_cls, 
	    dir,
	    (dir->is_directory) ? dir->data.dir.dir_size : dir->data.file.file_size,
	    dir->meta,
	    &dir->keywords,
	    &dir->anonymity,
	    &dir->priority,
	    (dir->is_directory) ? &no : &dir->data.file.do_index,
	    &dir->expirationTime,
	    &dir->client_info))
    return;
  if (! dir->is_directory)
    return;
  pos = dir->data.dir.entries;
  while (pos != NULL)
    {
      no = GNUNET_NO;
      if (GNUNET_OK != 
	  proc (proc_cls, 
		pos,
		(pos->is_directory) ? pos->data.dir.dir_size : pos->data.file.file_size,
		pos->meta,
		&pos->keywords,
		&pos->anonymity,
		&pos->priority,
		(dir->is_directory) ? &no : &dir->data.file.do_index,
		&pos->expirationTime,
		&pos->client_info))
	break;
      pos = pos->next;
    }
}


/**
 * Destroy publish-structure.  Clients should never destroy publish
 * structures that were passed to "GNUNET_FS_publish_start" already.
 *
 * @param fi structure to destroy
 * @param cleaner function to call on each entry in the structure
 *        (useful to clean up client_info); can be NULL; return
 *        values are ignored
 * @param cleaner_cls closure for cleaner
 */
void
GNUNET_FS_file_information_destroy (struct GNUNET_FS_FileInformation *fi,
				    GNUNET_FS_FileInformationProcessor cleaner,
				    void *cleaner_cls)
{
  struct GNUNET_FS_FileInformation *pos;
  int no;

  no = GNUNET_NO;
  if (fi->is_directory)
    {
      /* clean up directory */
      while (NULL != (pos = fi->data.dir.entries))
	{
	  fi->data.dir.entries = pos->next;
	  GNUNET_FS_file_information_destroy (pos, cleaner, cleaner_cls);
	}
      /* clean up client-info */
      if (NULL != cleaner)
	cleaner (cleaner_cls, 
		 fi,
		 fi->data.dir.dir_size,
		 fi->meta,
		 &fi->keywords,
		 &fi->anonymity,
		 &fi->priority,
		 &no,
		 &fi->expirationTime,
		 &fi->client_info);
      GNUNET_free_non_null (fi->data.dir.dir_data);
    }
  else
    {
      /* call clean-up function of the reader */
      if (fi->data.file.reader != NULL)
	fi->data.file.reader (fi->data.file.reader_cls, 0, 0, 
			      NULL, NULL);
      /* clean up client-info */
      if (NULL != cleaner)
	cleaner (cleaner_cls, 
		 fi,
		 fi->data.file.file_size,
		 fi->meta,
		 &fi->keywords,
		 &fi->anonymity,
		 &fi->priority,
		 &fi->data.file.do_index,
		 &fi->expirationTime,
		 &fi->client_info);
    }
  GNUNET_free_non_null (fi->filename);
  GNUNET_free_non_null (fi->emsg);
  GNUNET_free_non_null (fi->chk_uri);
  /* clean up serialization */
  if ( (NULL != fi->serialization) &&
       (0 != UNLINK (fi->serialization)) )
    GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING,
			      "unlink",
			      fi->serialization);
  if (NULL != fi->keywords)
    GNUNET_FS_uri_destroy (fi->keywords);
  if (NULL != fi->meta)
    GNUNET_CONTAINER_meta_data_destroy (fi->meta);
  GNUNET_free_non_null (fi->serialization);
  if (fi->te != NULL)
    {
      GNUNET_FS_tree_encoder_finish (fi->te,
				     NULL, NULL);
      fi->te = NULL;
    }
  GNUNET_free (fi);
}


/* end of fs_file_information.c */
