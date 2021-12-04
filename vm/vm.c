/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include <stdint.h>
#include "devices/timer.h"
#define MAX_STK_PG_SIZE ((1 << 20)/PGSIZE)
uint64_t spt_hash(const struct hash_elem *e, void *aux);

bool spt_less(const struct hash_elem *a, const struct hash_elem *b, void *aux);

static struct lock eviction_lock;
static struct list frame_lst;
/* Initializes the virtual memory subsystem by invoking each subsystem's
* intialize codes. */
void vm_init(void)
{
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	//pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	lock_init(&eviction_lock);
	list_init(&frame_lst);
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

		if(!page){
			return false;
		}

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
		page->time = 0; /* access time  */	
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

	if(page){
		/* set the access time */
		page->time = timer_ticks ();
	}
	
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

	 /* LRU policy for eviction */
	 struct list * frame_lst_ptr = &frame_lst;
	 struct list_elem * e;
	 struct frame * frame;
	 size_t least_time = SIZE_MAX;
	
	 /* find the least recently used frame */
	 for(e = list_begin(frame_lst_ptr); e != list_end(frame_lst_ptr); e = list_next(e))
	 {
		
		frame = list_entry(e,struct frame,frame_elem);
		
		if(frame->page && frame->page->time < least_time){
			victim = frame;
			least_time = frame->page->time;
		}
	 }

	 ASSERT(victim);
	 /* remove the frame from frame list */
	 if(list_front(frame_lst_ptr) == &victim->frame_elem)
	 {
		list_pop_front(frame_lst_ptr);
	 }
	 else if(list_back(frame_lst_ptr) == &victim->frame_elem)
	 {
		list_pop_back(frame_lst_ptr);
	 }
	 else
	 {
		list_remove(&victim->frame_elem);
	 }
	
	
	 return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {

	struct frame *victim  = vm_get_victim ();
	
	/* unlink the frame and page */
	struct page * page = victim->page;

	ASSERT(page && page->operations->swap_out);

	swap_out(page);

	victim->page = NULL;
	page->frame = NULL;
	memset(victim->kva,0,PGSIZE);
	pml4_clear_page(thread_current ()->pml4,page->va);
	pml4_set_dirty(thread_current ()->pml4,page->va,false);

	return victim;
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

	/* try to evict some frame */
	if(!frame->kva){
		struct frame * evicted_frame = vm_evict_frame ();
		
		if(!evicted_frame){
			printf("couldn't evict any frames \n");
			return NULL;
		}


		free(frame);		
		return evicted_frame;
	}

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	void * curr_stk_bottom = thread_current ()->stk_bottom;
	void * new_stk_bottom = pg_round_down(addr);
	
	int alloc_pg_size = pg_no(curr_stk_bottom)-pg_no(new_stk_bottom);
	int curr_stk_pg_size = pg_no(USER_STACK)-pg_no(new_stk_bottom);

	/* if the stack size would be above 1 MB */
	if(curr_stk_pg_size + alloc_pg_size > (MAX_STK_PG_SIZE-1)){
		return;
	}

	while(alloc_pg_size){
		curr_stk_bottom -= PGSIZE;
		if(!vm_alloc_page(VM_ANON | VM_MARKER_0,curr_stk_bottom,true)){
			return;
		}
		
		alloc_pg_size--;
	}

	if(alloc_pg_size){
		vm_claim_page(curr_stk_bottom);
	}
	thread_current ()->stk_bottom = curr_stk_bottom;
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
	uintptr_t rsp_bottom = pg_round_down(thread_current ()->rsp);
	
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if(is_kernel_vaddr(addr)){
		return false;
	}

	page = spt_find_page(spt,addr);

	/* try to grow the stack first */
	if(write && !page && addr < USER_STACK && addr > thread_current ()->rsp - PGSIZE)
	{
		void * curr_stk_bottom = thread_current ()->stk_bottom;
		void * new_stk_bottom;
		vm_stack_growth(addr);

		new_stk_bottom = thread_current ()->stk_bottom;
		return curr_stk_bottom != new_stk_bottom;
	}
	
	else if (!page)
	{	
		return false;
	}
	
	if(write && !not_present){
		return false;
	}
	
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
		printf("vm_claim_page: page is not found in spt\n");
		return false;
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	lock_acquire(&eviction_lock);
	struct frame *frame = vm_get_frame ();
	struct thread *t = thread_current ();


	ASSERT(page && frame);
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */

	/* Insert the frame to list */
	list_push_back(&frame_lst,&frame->frame_elem);

	/* check if va is mapped */
	if(pml4_get_page(t->pml4,page->va) != NULL ||
	   !pml4_set_page(t->pml4,page->va,frame->kva,page->writable)){
		printf("va is already in pml4 mapping\n");
		lock_release(&eviction_lock);
		return false;

	}

	lock_release(&eviction_lock);
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

				if(!vm_claim_page(child_page->va)){
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

	if(spt->hash_table)
	{
		hash_destroy(spt->hash_table,spt_destroy_page);
		free(spt->hash_table);
	}
	
}
