/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

struct bitmap * bitmap;
/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1);
	disk_sector_t swap_size = disk_size(swap_disk);
	
	/* use bitmap for swapping */
	bitmap = bitmap_create(swap_size);
	
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->bitmap_index = -1;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	//printf("going to swap in \n");
	struct anon_page *anon_page = &page->anon;

	/* the page has been swapped out */
	if(anon_page->bitmap_index != -1){
		printf("going to read from disk\n");
		disk_sector_t index = anon_page->bitmap_index;

		for(int i = 0;i < 8;i++){
			disk_read(swap_disk,index + i,((char *)(page->va)) + i * DISK_SECTOR_SIZE);
			/* no more using index of bitmap for now */
			bitmap_set(bitmap,index,false);
		}
		anon_page->bitmap_index = -1;
		printf("finished reading from disk\n");
	}
	

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	disk_sector_t index = bitmap_scan(bitmap,0,8,false);

	if(index == BITMAP_ERROR){
		printf("couldn't find any bitmap position");
		return false;
	}

	for(int i = 0;i < 8;i++){
		disk_write(swap_disk,index + i,((char *)(page->va)) + i * DISK_SECTOR_SIZE);
		bitmap_set(bitmap,index,true);
	}

	/* bitmap at index is being used */
	anon_page->bitmap_index = index;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if(page->frame){
		free(page->frame);
	}
	
}
