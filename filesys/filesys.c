#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "filesys/page_cache.h"
#include "filesys/fat.h"

#define MAX_CACHE_ENTRY 64

/* The disk that contains the file system. */
struct disk *filesys_disk;

/* The metadata of buffer cache */
struct page_cache_meta * cache_meta;

/* The buffer cache */
struct page_cache * cache;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	cache_init();
	fat_init ();

	if (format)
		do_format ();

	fat_open ();
	

#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();

	/* flushes all entries and destroys the hash table */
	cache_flush(NULL);
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) {
	struct dir *dir = dir_open_root ();
	cluster_t clst;
	bool is_empty_slot = true;
	bool create_chain = true;
	bool inode_creat = true;
	bool dir_ad = true;
	
	bool success = (dir != NULL
			&& (is_empty_slot = fat_empty_slot(&clst))
			&& (inode_creat = inode_create (cluster_to_sector(clst), initial_size))
			&& (dir_ad = dir_add (dir, name,cluster_to_sector(clst))));


	if (!success && is_empty_slot){
		fat_remove_chain(clst,0);	
	}
		
	dir_close (dir);

	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {
	struct dir *dir = dir_open_root ();
	struct inode *inode = NULL;

	if (dir != NULL)
		dir_lookup (dir, name, &inode);
	dir_close (dir);

	return file_open (inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	return success;
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}
