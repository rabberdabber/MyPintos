#include "filesys/fsutil.h"
#include <debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/disk.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "filesys/fat.h"

/* A directory. */
struct dir
{
	struct inode *inode; /* Backing store. */
	off_t pos;			 /* Current position. */
};

/* A single directory entry. */
struct dir_entry
{
	disk_sector_t inode_sector; /* Sector number of header. */
	char name[NAME_MAX + 1];	/* Null terminated file name. */
	bool in_use;				/* In use or free? */
};

struct inode_disk
{
	disk_sector_t start;  /* First data sector. */
	off_t length;		  /* File size in bytes. */
	unsigned magic;		  /* Magic number. */
	uint32_t unused[125]; /* Not used. */
};


/* List files in the root directory. */
void
fsutil_ls (char **argv UNUSED) {
	struct dir *dir;
	char name[NAME_MAX + 1];

	printf ("Files in the root directory:\n");
	dir = dir_open_root ();
	if (dir == NULL)
		PANIC ("root dir open failed");
	while (dir_readdir (dir, name))
		printf ("%s\n", name);
	printf ("End of listing.\n");
}

/* Prints the contents of file ARGV[1] to the system console as
 * hex and ASCII. */
void
fsutil_cat (char **argv) {
	const char *file_name = argv[1];

	struct file *file;
	char *buffer;

	printf ("Printing '%s' to the console...\n", file_name);
	file = filesys_open (file_name);
	if (file == NULL)
		PANIC ("%s: open failed", file_name);
	buffer = palloc_get_page (PAL_ASSERT);
	for (;;) {
		off_t pos = file_tell (file);
		off_t n = file_read (file, buffer, PGSIZE);
		if (n == 0)
			break;

		hex_dump (pos, buffer, n, true); 
	}
	palloc_free_page (buffer);
	file_close (file);
}

/* Deletes file ARGV[1]. */
void
fsutil_rm (char **argv) {
	const char *file_name = argv[1];

	printf ("Deleting '%s'...\n", file_name);
	if (!filesys_remove (file_name))
		PANIC ("%s: delete failed\n", file_name);
}

/* Copies from the "scratch" disk, hdc or hd1:0 to file ARGV[1]
 * in the file system.
 *
 * The current sector on the scratch disk must begin with the
 * string "PUT\0" followed by a 32-bit little-endian integer
 * indicating the file size in bytes.  Subsequent sectors hold
 * the file content.
 *
 * The first call to this function will read starting at the
 * beginning of the scratch disk.  Later calls advance across the
 * disk.  This disk position is independent of that used for
 * fsutil_get(), so all `put's should precede all `get's. */
void
fsutil_put (char **argv) {
	static disk_sector_t sector = 0;

	const char *file_name = argv[1];
	struct disk *src;
	struct file *dst;
	off_t size;
	void *buffer;

	printf ("Putting '%s' into the file system...\n", file_name);

	/* Allocate buffer. */
	buffer = malloc (DISK_SECTOR_SIZE);
	if (buffer == NULL)
		PANIC ("couldn't allocate buffer");

	/* Open source disk and read file size. */
	src = disk_get (1, 0);
	if (src == NULL)
		PANIC ("couldn't open source disk (hdc or hd1:0)");

	/* Read file size. */
	disk_read (src, sector++, buffer);
	if (memcmp (buffer, "PUT", 4))
		PANIC ("%s: missing PUT signature on scratch disk", file_name);
	size = ((int32_t *) buffer)[1];
	if (size < 0)
		PANIC ("%s: invalid file size %d", file_name, size);
	
	/* Create destination file. */
	if (!filesys_create (file_name, size))
		PANIC ("%s: create failed", file_name);
	dst = filesys_open (file_name);
	if (dst == NULL)
		PANIC ("%s: open failed", file_name);

	/*char disk_1_after[DISK_SECTOR_SIZE];
	cache_read(cluster_to_sector(2), disk_1_after,0,DISK_SECTOR_SIZE);

	printf("before writing\n\n\n");
	struct dir_entry *e;
	size_t ofs;
	bool matched = false;
	for (ofs = 0; (e = (struct dir_entry *)(disk_1_after + ofs)) && ofs < DISK_SECTOR_SIZE;
		 ofs += sizeof(struct dir_entry)){
		if (e->in_use && !strcmp(file_name, e->name))
		{
			matched = true;
		}
	}

	ASSERT(matched);*/
	/* Do copy. */
	
	while (size > 0) {
		int chunk_size = size > DISK_SECTOR_SIZE ? DISK_SECTOR_SIZE : size;
		disk_read (src, sector++, buffer);
		if (file_write (dst, buffer, chunk_size) != chunk_size)
			PANIC ("%s: write failed with %"PROTd" bytes unwritten",
					file_name, size);

		
		size -= chunk_size;
	}

	/*char disk_2_after[DISK_SECTOR_SIZE];
	cache_read(cluster_to_sector(2),disk_2_after,0,DISK_SECTOR_SIZE);
	struct dir_entry * entry1,* entry2;
	entry1 = disk_1_after;
	entry2 = disk_2_after;
	printf("entry 1 is %d and entry 2 is %d\n",entry1->in_use,entry2->in_use);
	printf("entry 1 is %d and entry 2 is %d\n", entry1->inode_sector, entry2->inode_sector);
	printf("entry 1 is %s and entry 2 is %s\n", entry1->name, entry2->name);

	for(int i = 0;i < DISK_SECTOR_SIZE;i++){
		if(disk_1_after[i] != disk_2_after[i]){
			printf("the directory infos corrupted at %d\n",i);
			
		}
	}
	exit(-1);*/
	/*struct inode_disk * inode = (struct inode_disk *)disk_1;
	printf("start sector is %d\n",inode->start);
	printf("length is %d\n",inode->length);*/
	
	/*struct dir * dir = dir_open_root();*/
	/*printf("after writing\n\n\n");
	matched = false;
	for (ofs = 0; (e = (struct dir_entry *)(disk_1_after + ofs)) && ofs < DISK_SECTOR_SIZE;
		 ofs += sizeof(struct dir_entry))
		
		if (e->in_use && !strcmp(file_name,e->name))
		{
			matched = true;
			
		}

	
	ASSERT(matched);
	printf("passed \n");*/

	/* Finish up. */
	file_close (dst);
	free (buffer);
}

/* Copies file FILE_NAME from the file system to the scratch disk.
 *
 * The current sector on the scratch disk will receive "GET\0"
 * followed by the file's size in bytes as a 32-bit,
 * little-endian integer.  Subsequent sectors receive the file's
 * data.
 *
 * The first call to this function will write starting at the
 * beginning of the scratch disk.  Later calls advance across the
 * disk.  This disk position is independent of that used for
 * fsutil_put(), so all `put's should precede all `get's. */
void
fsutil_get (char **argv) {
	static disk_sector_t sector = 0;

	const char *file_name = argv[1];
	void *buffer;
	struct file *src;
	struct disk *dst;
	off_t size;

	printf ("Getting '%s' from the file system...\n", file_name);

	/* Allocate buffer. */
	buffer = malloc (DISK_SECTOR_SIZE);
	if (buffer == NULL)
		PANIC ("couldn't allocate buffer");

	/* Open source file. */
	src = filesys_open (file_name);
	if (src == NULL)
		PANIC ("%s: open failed", file_name);
	size = file_length (src);

	/* Open target disk. */
	dst = disk_get (1, 0);
	if (dst == NULL)
		PANIC ("couldn't open target disk (hdc or hd1:0)");

	/* Write size to sector 0. */
	memset (buffer, 0, DISK_SECTOR_SIZE);
	memcpy (buffer, "GET", 4);
	((int32_t *) buffer)[1] = size;
	disk_write (dst, sector++, buffer);

	/* Do copy. */
	while (size > 0) {
		int chunk_size = size > DISK_SECTOR_SIZE ? DISK_SECTOR_SIZE : size;
		if (sector >= disk_size (dst))
			PANIC ("%s: out of space on scratch disk", file_name);
		if (file_read (src, buffer, chunk_size) != chunk_size)
			PANIC ("%s: read failed with %"PROTd" bytes unread", file_name, size);
		memset (buffer + chunk_size, 0, DISK_SECTOR_SIZE - chunk_size);
		disk_write (dst, sector++, buffer);
		size -= chunk_size;
	}

	/* Finish up. */
	file_close (src);
	free (buffer);
}
