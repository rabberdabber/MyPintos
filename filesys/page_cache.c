/* page_cache.c: Implementation of Page Cache (Buffer Cache). */

#include "filesys/page_cache.h"
#include "devices/timer.h"

enum disk_ops {
	WRITE,
	READ
};
uint64_t cache_hash(const struct hash_elem *e,void * aux);
bool cache_less(const struct hash_elem *a,const struct hash_elem *b,void * aux);


uint64_t 
cache_hash(const struct hash_elem *e,void * aux){
	struct cache_entry * entry = hash_entry(e,struct cache_entry,elem);
	uint64_t hash_val = hash_bytes((const void *) &entry->sector_no,sizeof(disk_sector_t));
	return hash_val;	
}

bool 
cache_less(const struct hash_elem *a,const struct hash_elem *b,void * aux){
	struct cache_entry *entry_a = hash_entry(a, struct cache_entry, elem);
	struct cache_entry *entry_b = hash_entry(b,struct cache_entry,elem);

	return entry_a->sector_no < entry_b->sector_no;
}

/* Initializes the buffer cache hash table */
void
cache_init(){
	buffer_cache = (struct hash *) malloc(sizeof(struct hash));
	if(!buffer_cache){
		PANIC("buffer cache allocation failed\n");
	}

	hash_init(buffer_cache,cache_hash,cache_less,NULL);
	lock_init(&buffer_cache_lock);
}

static struct cache_entry * 
cache_entry_insert(disk_sector_t _sector){
	ASSERT(_sector < disk_size(disk_get(0,1)));

	struct cache_entry * entry = malloc(sizeof(struct cache_entry));
	if(entry == NULL){
		PANIC("could not allocate a cache entry");
	}

	lock_init(&entry->cache_entry_lock);
	entry->sector_no = _sector;

	hash_insert(buffer_cache,&entry->elem);

	//ASSERT(hash_size(buffer_cache) <= MAX_CACHE_ENTRY_SIZE);
	return entry;
}

/* look up the disk data in the cache */
struct cache_entry * 
cache_lookup(disk_sector_t sector_no){

	struct hash_elem * e;
	struct cache_entry * entry = NULL;
	struct cache_entry similar_entry;
	similar_entry.sector_no = sector_no;

	if(!hash_empty(buffer_cache) && (e = hash_find(buffer_cache,&similar_entry.elem)) != NULL)
		entry = hash_entry(e,struct cache_entry,elem);

	if(entry != NULL)
		entry->time_tick = timer_ticks();
	
		
	return entry;
}

static
cache_destroy_entry(struct hash_elem *e,void * aux){
	ASSERT(e);

	struct cache_entry * entry = hash_entry(e,struct cache_entry,elem);
	free(entry);
}

/* flushes the cache entry entry or else it flushes all entries that are dirty */
void 
cache_flush(struct cache_entry * entry){
	
	struct disk * filesys_disk = disk_get(0,1);
	if(entry != NULL && entry->dirty){	
		disk_write(filesys_disk,entry->sector_no,entry->disk_data);
		entry->dirty = false;
	}

	/* flushes all hash table entries that are dirty and destroys the buffer cache*/
	else if(entry == NULL){
		struct hash_iterator i;
		struct cache_entry * entry;

		lock_acquire(&buffer_cache_lock);		

		hash_first(&i,buffer_cache);
		while(hash_next(&i)){
			entry = hash_entry(hash_cur(&i),struct cache_entry,elem);

			if(entry->dirty){
				disk_write(filesys_disk,entry->sector_no,entry->disk_data);
			}
			
		}

		hash_destroy(buffer_cache,cache_destroy_entry);
		free(buffer_cache);
		lock_release(&buffer_cache_lock);
	}
}

void 
cache_select_and_evict(){
	struct hash_iterator i;
	struct cache_entry * entry;
	struct cache_entry * victim;
	size_t least_time = SIZE_MAX;

	/* going to use LRU */
	hash_first(&i,buffer_cache);

	while(hash_next(&i)){
		entry = hash_entry(hash_cur(&i),struct cache_entry,elem);

		if(entry->time_tick < least_time){
			least_time = entry->time_tick;
			victim = entry;
		}
		
	}
	
	cache_flush(victim);
	hash_delete(buffer_cache,&victim->elem);
	free(victim);
}


/* find slot in the hash table for the given sector no if it already exists 
return the entry or create new entry and change the new_entry to &true  */
static 
struct cache_entry * cache_entry_find_slot(disk_sector_t sector_no,enum disk_ops ops){
	
	struct cache_entry * entry = NULL;
	struct disk * filesys_disk = disk_get(0,1);
	ASSERT(sector_no < disk_size(filesys_disk));

	if((entry = cache_lookup(sector_no)) == NULL){
		/*if((hash_size(buffer_cache)) >= MAX_CACHE_ENTRY_SIZE){
			cache_select_and_evict();
		}*/

		entry = cache_entry_insert(sector_no);

		/* if we want to read from the cache we need to read from disk */
		if(ops == READ)
		{
			/* read from the disk at sector sector_no */
			disk_read(filesys_disk, sector_no, entry->disk_data);
			entry->dirty = false;
		}
	}

	return entry;
}

bool cache_read(disk_sector_t sector_no,void * buffer,int sector_ofs,off_t size){
	lock_acquire(&buffer_cache_lock);
	if(sector_no >= disk_size(disk_get(0,1))){
		PANIC("cache read from invalid sector\n");
	}

	struct cache_entry * entry = cache_entry_find_slot(sector_no,READ);
	//lock_acquire(&entry->cache_entry_lock);
	memcpy(buffer,entry->disk_data + sector_ofs,size);
	//lock_release(&entry->cache_entry_lock);
	lock_release(&buffer_cache_lock);
	return true;
}

bool cache_write(disk_sector_t sector_no,void * buffer,int sector_ofs,off_t size){
	lock_acquire(&buffer_cache_lock);
	if (sector_no >= disk_size(disk_get(0, 1)))
	{
		PANIC("cache write to invalid sector\n");
	}

	struct cache_entry * entry = cache_entry_find_slot(sector_no,WRITE);
	//lock_acquire(&entry->cache_entry_lock);
	memcpy(entry->disk_data+sector_ofs,buffer,size);
	//lock_release(&entry->cache_entry_lock);
	
	/* set dirty bit */
	if(size > 0 && !entry->dirty){
		entry->dirty = true;
	}
	lock_release(&buffer_cache_lock);
	return true;
}

/* YOU HAVE TO IMPLEMENT IT */
void cache_write_behind();
void cache_read_ahead();