#ifndef FILESYS_PAGE_CACHE_H
#define FILESYS_PAGE_CACHE_H
#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/synch.h"

#define MAX_CACHE_ENTRY_SIZE 64

struct cache_entry{
	uint8_t disk_data[DISK_SECTOR_SIZE];
	disk_sector_t sector_no;
	bool valid;
	bool dirty;
	struct lock cache_entry_lock;
    struct hash_elem elem;
    size_t time_tick;

};

static struct hash * buffer_cache;
static struct lock buffer_cache_lock;

void cache_init();
struct cache_entry * cache_lookup(disk_sector_t sector_no);
void cache_flush(struct cache_entry * entry); /* flushes all if entry is NULL */
void cache_select_and_evict();
bool cache_read(disk_sector_t sector_no,void * buffer,int sector_ofs,off_t size);
bool cache_write(disk_sector_t sector_no,void * buffer,int sector_ofs,off_t size);
void cache_write_behind();
void cache_read_ahead();
void cache_term();

#endif
