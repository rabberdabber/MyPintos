/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/mmu.h"
#include "vm/file.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}


static struct pg_mapping *
find_mapping(void * addr){

	struct list_elem * e;
	struct pg_mapping * mapping = NULL;
	struct list pg_mapping_lst = thread_current ()->mapped_pg_lst;
	

	for(e = list_begin(&pg_mapping_lst);e != list_end(&pg_mapping_lst); e = list_next(e)){
		mapping = list_entry(e,struct pg_mapping,map_elem);

		if(mapping->addr == addr){
			break;
		}
	}

	return mapping;
}

static struct pg_mapping *
find_head_mapping(void * addr){
	struct list_elem * e;
	struct pg_mapping * mapping = NULL;
	struct list pg_mapping_lst = thread_current ()->mapped_pg_lst;
	

	for(e = list_begin(&pg_mapping_lst);e != list_end(&pg_mapping_lst); e = list_next(e)){
		mapping = list_entry(e,struct pg_mapping,map_elem);

		char * mapping_addr = mapping->addr;
		if(mapping->addr <= addr && (mapping_addr + (mapping->num_of_pgs * PGSIZE) ) >= addr){
			break;
		}
	}

	return mapping;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;

}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	
	return true;
}


/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	struct pg_mapping * mapping;


	if(page->frame){
		free(page->frame);
	}
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

}

/* Do the munmap */
void
do_munmap (void *addr,bool remove_from_hashtbl) {
	struct pg_mapping * mapping;
	struct list * pg_lst = &thread_current ()->mapped_pg_lst;


	if(!(mapping = find_mapping(addr))){
		//printf("addr is not mapped\n");
		return;
	}

	if(list_front(pg_lst) == &mapping->map_elem)
	{
		list_pop_front(pg_lst);
	}
	else if(list_back(pg_lst) == &mapping->map_elem){
		list_pop_back(pg_lst);
	}
	else{
		list_remove(&mapping->map_elem);
	}
	
	bool changed = false;
	//printf("going to munmap\n");
	/* change the file in the disk if dirty */
	for(int i = 0;i <= mapping->num_of_pgs;i++){
		if(pml4_is_dirty(thread_current ()->pml4,addr + (i * PGSIZE))){
			changed = true;
			break;
		}
	}	


	int i = 0;
	int zero_bytes = mapping->zero_bytes;
	file_seek(mapping->file,mapping->offset);

	for(i = 0;i <= mapping->num_of_pgs;i++){
		struct page * page = spt_find_page(&thread_current ()->spt,addr + (i * PGSIZE));
	
		if(changed && i < mapping->num_of_pgs){
			file_write(mapping->file,addr + (i * PGSIZE),PGSIZE);
		}
		else if(changed){
			file_write(mapping->file,addr + (i * PGSIZE),PGSIZE - zero_bytes);
		}

		if(remove_from_hashtbl){
			/* remove the page from spt and destroy it */
			hash_delete(thread_current ()->spt.hash_table,&page->elem);
			destroy(page);
		}
		
	
	}

}
