/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include <stdint.h>
#define MAX_STK_PG_SIZE ((1 << 20)/PGSIZE)
uint64_t spt_hash(const struct hash_elem *e, void *aux);

bool spt_less(const struct hash_elem *a, const struct hash_elem *b, void *aux);


/* Initializes the virtual memory subsystem by invoking each subsystem's
* intialize codes. */
void vm_init(void)
{
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page * page;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
		page = (struct page *) malloc(sizeof(struct page));

		switch (VM_TYPE(type))
		{
	
			case VM_ANON:
				uninit_new(page,upage,init,type,aux,anon_initializer);
				break;
			case VM_FILE:
				uninit_new(page,upage,init,type,aux,file_backed_initializer);
				break;
		
			default:
				break;
		}
	
		page->writable = writable;
		spt_insert_page(spt,page);	
		return true;
	}

	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	struct page search_page;
	struct hash_elem * e;
	/* TODO: Fill this function. */
	search_page.va = pg_round_down(va);
	if((e = hash_find(spt->hash_table,&search_page.elem)) != NULL)
		page = hash_entry(e,struct page,elem);

	return page;
}


/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	struct hash_elem *e;
	
	/* if not already in the hash table */
	if((e = hash_find(spt->hash_table,&page->elem)) == NULL){
		/* need some kind of synchronization */
		hash_insert(spt->hash_table,&page->elem);
		succ = true;
	}

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	frame = (struct frame *) malloc(sizeof(struct frame));

	if(!frame){
		printf("vm_get_frame: malloc failed\n");
		return NULL;
	}

	frame->kva = palloc_get_page(PAL_USER);
	frame->page = NULL;

	if(!frame->kva){
		PANIC("todo");
	}


	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	void * current_sp = thread_current ()->rsp;
	void * updated_sp = pg_round_down(addr);
	int alloc_pg_size = pg_no(current_sp)-pg_no(updated_sp);
	int curr_stk_pg_size = pg_no(USER_STACK)-pg_no(current_sp);

	/* if the stack size would be above 1 MB */
	if(curr_stk_pg_size + alloc_pg_size > (MAX_STK_PG_SIZE-1)){
		printf("error: stack size could not exceed 1MB\n");
		return;
	}

	while(alloc_pg_size){
		current_sp -= PGSIZE;
		if(!vm_alloc_page(VM_ANON | VM_MARKER_0,current_sp,true)){
			printf("could not extend stack page %p\n",current_sp);
			thread_current ()->rsp = current_sp + PGSIZE;
			vm_claim_page(current_sp + PGSIZE);
			return;
		}
		alloc_pg_size--;
	}

	/* claim only the stack pointer page for now */
	vm_claim_page(current_sp);
	thread_current ()->rsp = current_sp;
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if(is_kernel_vaddr(addr)){
		return false;
	}
	page = spt_find_page(spt,addr);
	
	if(!page){
		vm_stack_growth(addr);
		return thread_current ()->rsp == pg_round_down(addr);
	}
	
	if(!page){
		printf("could not find the page with va:%p\n",addr);
		return false;
	}
	
	if(write && !not_present)
		return false;
	
	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current ()->spt,va);

	if(!page){
		return false;
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();
	struct thread *t = thread_current ();


	ASSERT(page && frame);
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */

	/* check if va is mapped */
	if(pml4_get_page(t->pml4,page->va) != NULL ||
	   !pml4_set_page(t->pml4,page->va,frame->kva,page->writable)){
		return false;

	}

	return swap_in(page,frame->kva);
}

uint64_t 
spt_hash(const struct hash_elem *e,void * aux){
	struct page * page = hash_entry(e,struct page,elem);
	uint64_t hash_val = hash_bytes(&page->va,sizeof(page->va));
	return hash_val;	
}

bool 
spt_less(const struct hash_elem *a,const struct hash_elem *b,void * aux){
	struct page *page_a = hash_entry(a, struct page, elem);
	struct page *page_b = hash_entry(b,struct page,elem);

	return page_a->va < page_b->va;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	spt->hash_table = (struct hash *) malloc(sizeof(struct hash));
	if(!spt->hash_table){
		printf("could not allocate hash table\n");
		return;
	}
	hash_init(spt->hash_table,spt_hash,spt_less,NULL);
}

/* Copy supplemental page table from src to dst */

bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {

	struct hash_iterator i;
	struct page * page;
	struct lazyLoadInfo * aux,*aux_src;
	void *src_kva,*dst_kva;
	
	hash_first(&i,src->hash_table);
	while(hash_next(&i))
	{
		page = hash_entry(hash_cur(&i),struct page,elem);

		switch(VM_TYPE(page->operations->type))
		{
			case VM_UNINIT:
				aux = (struct lazyLoadInfo *) malloc(sizeof(struct lazyLoadInfo));
				aux_src = (struct lazyLoadInfo *)page->uninit.aux;
				memcpy(aux,aux_src,sizeof(struct lazyLoadInfo));
				aux->file_to_load = file_duplicate(aux_src->file_to_load); // duplicate the file
				vm_alloc_page_with_initializer(page->uninit.type,page->va,page->writable,page->uninit.init,aux);
				break;

			case VM_ANON:

				if(!vm_alloc_page(page->operations->type,page->va,page->writable)){
					return false;
				}
				struct page * child_page = spt_find_page(&thread_current ()->spt,page->va);

				if(!child_page){
					printf("could not find page in the hashtable\n");
					return false;
				}

				if(!vm_claim_page(child_page)){
					printf("could not claim page\n");
					return false;
				}

				src_kva = page->frame->kva;
				dst_kva = child_page->frame->kva;

				
				memcpy(dst_kva,src_kva,PGSIZE);
				break;

			default:
				break;
		}
		

	}

	return true;
		
}


void 
spt_destroy_page(struct hash_elem *e,void * aux){
	ASSERT(e);
	struct page * page = hash_entry(e,struct page,elem);
	destroy(page);
	free(page);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

	//hash_destroy(spt->hash_table,spt_destroy_page);
	//free(spt->hash_table);
	
}
