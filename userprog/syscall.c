#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "userprog/process.h"
#include "threads/palloc.h"
#include "vm/file.h"

#define MAXFD 128

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void close_stdio(bool is_stdin);


/* An open file. */
struct file {
	struct inode *inode;        /* File's inode. */
	off_t pos;                  /* Current position. */
	bool deny_write;            /* Has file_deny_write() been called? */
};



void remove_fd_info(struct fd_info * fd_info_ptr);
void modify_fd_infos(int newfd);
struct fd_info * get_fd_info(int fd);
static pid_t fork_ (const char *thread_name,struct intr_frame * if_);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&file_lock);
}


static void is_valid_addr(void * addr){

	/* if not user virtual address */
	if(is_kernel_vaddr(addr)){
		exit(-1);
	}

	uint64_t *pte = pml4e_walk(thread_current ()->pml4,(const uint64_t) addr,0);

	if(!pte && !pml4_get_page(thread_current ()->pml4,(const void *)addr)){
		exit(-1);
	}
	

}

void halt (void){
	power_off();
}

 void exit (int status) {
	struct thread * t = thread_current();

	struct file * running_file = t->running_file;

	//lock_acquire(&file_lock);

	if(running_file){
		file_close(running_file);
	}

	//lock_release(&file_lock);

	t->exit_status = status;
	printf("%s: exit(%d)\n",t->name,status);

	/* do the unmapping */
	struct list_elem * e;
	struct pg_mapping * mapping = NULL;
	struct list *pg_mapping_lst = &thread_current ()->mapped_pg_lst;

	
	/* going to clean up the mapping */
	if(!list_empty(pg_mapping_lst))
	{
		for(e = list_begin(pg_mapping_lst);e != list_end(pg_mapping_lst); e = list_next(e))
		{
			mapping = list_entry(e,struct pg_mapping,map_elem);
			file_seek(mapping->file,mapping->offset);
			do_munmap(mapping->addr,false);
		}
	}


	/* wake up any waiting parent thread  and hold exit_status intact*/
	sema_up(&t->sema_wait);
	sema_down(&t->sema_wait_status);
	
	thread_exit();
}


 pid_t fork_ (const char *thread_name,struct intr_frame * if_){

	struct thread * t = thread_current ();

	if(!thread_name){
		exit(-1);
	}


	tid_t tid = process_fork(thread_name,if_);
	
	return tid;
}

int exec(const char * file){
	char * fn_copy = palloc_get_page (0);

	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file, PGSIZE);
    process_exec(fn_copy);

	exit(-1);// if process_exec bypassed

	return -1;
}

 int wait (pid_t pid){

	int status = process_wait(pid);
	return status;

}


 int write (int fd, const void *buffer, unsigned length){

	if(fd < 0){
		return -1;
	}

	int written_sofar = 0;
	struct thread *t = thread_current ();
	struct file * file_p;
	struct fd_info * fd_info_ptr;
	struct fd_info * fd_info_ptr_1;
	struct fd_info * info_ptr;
	info_ptr = get_fd_info(fd);

	if(fd == STDIN_FILENO || !info_ptr || !buffer){
		exit(-1);
	}

	fd_info_ptr = get_fd_info(STDOUT_FILENO);

	if(fd == STDOUT_FILENO){
		if(fd_info_ptr && fd_info_ptr->stdio_shutdown){
			return 0; // stdout closed
		}
	}

	fd_info_ptr_1 = get_fd_info(fd_info_ptr->fd_same_file);
	

	/* if fd is STDOUT or stdout dup2 */
	if((fd == STDOUT_FILENO && info_ptr->fd_same_file == -1) || info_ptr->fd_same_file == STDOUT_FILENO){
			putbuf(buffer,length);
			return length;
	}

	else{
		//lock_acquire(&file_lock);
		if(fd != STDOUT_FILENO && !t->fd_table[info_ptr->converted_fd_num]){
			lock_release(&file_lock);
			exit(-1);
		}

		if(fd == STDOUT_FILENO){
			file_p = t->fd_table[fd_info_ptr->fd_same_file];
		}
		else{
			file_p = t->fd_table[info_ptr->converted_fd_num];
		}
		
		written_sofar = file_write(file_p,buffer,length);

		//lock_release(&file_lock);
	}

	return written_sofar;
}

 bool create (const char *file, unsigned initial_size){

	if(!file){
		exit(-1);
	}
	
	//lock_acquire(&file_lock);
	bool created = filesys_create(file,initial_size);
	//lock_release(&file_lock);

	return created;
}

 bool remove (const char *file){

	if(!file){
		exit(-1);
	}
	//lock_acquire(&file_lock);
	bool removed = filesys_remove(file);
	//lock_release(&file_lock);

	return removed;
}

 int read (int fd, void *buffer, unsigned length){

	struct thread * t = thread_current ();
	struct fd_info * fd_info_ptr;
	struct fd_info * info_ptr = get_fd_info(fd);
	struct file * file_p;

	if(!info_ptr){
		return -1;
	}

	if(fd == STDOUT_FILENO || !info_ptr || !buffer){
		exit(-1);
	}

	fd_info_ptr = get_fd_info(STDIN_FILENO);

	if(fd == STDIN_FILENO){
		if(fd_info_ptr && fd_info_ptr->stdio_shutdown){
			return 0; // stdin closed
		}
	}

	// is stdin
	int read_sofar = 0;
	
	/* if fd is STDIN or STDIN dup2 */
	if((fd == STDIN_FILENO && info_ptr->fd_same_file == -1) || info_ptr->fd_same_file == STDIN_FILENO){

	 /* while(read_sofar < length){
			((char *)buffer)[read_sofar] = input_getc();
			read_sofar++;
		}*/
		read_sofar = input_getc();

	}

	else {
		//lock_acquire(&file_lock);
		if(fd != STDIN_FILENO && !t->fd_table[info_ptr->converted_fd_num]){
			lock_release(&file_lock);
			exit(-1);
		}

		if(fd == STDIN_FILENO){
			file_p = t->fd_table[fd_info_ptr->fd_same_file];
		}
		else{
			file_p = t->fd_table[info_ptr->converted_fd_num];
		}
		
		read_sofar = file_read(file_p,buffer,length);
		//printf("read so far:%d\n",read_sofar);

		//lock_release(&file_lock);
	}
	
	return read_sofar;
}

 int open (const char *file){

	struct thread * t = thread_current ();
	struct list * fd_list_ptr = t->fd_list_ptr;

	if(!file){
		exit(-1);
	}

//	lock_acquire(&file_lock);


	struct file * open_file = filesys_open(file);

//	lock_release(&file_lock);


	if(!open_file || t->maxfd > MAXFD){
		return -1;
	}

	int fd = t->nextfd;

	t->fd_table[t->nextfd++] = open_file;

	while(t->fd_table[t->nextfd]){
		t->nextfd++;
	}

	if(t->nextfd > t->maxfd){
		t->maxfd = t->nextfd;
	}

	struct fd_info *fd_info_ptr = malloc(sizeof(struct fd_info));
	fd_info_ptr->fd_num = fd_info_ptr->converted_fd_num =  fd;
	fd_info_ptr->fd_same_file = -1;
	list_push_front(fd_list_ptr, &fd_info_ptr->fd_elem);	
	return fd;

}

 int filesize (int fd){

	struct thread * t = thread_current ();
	struct fd_info * info_ptr = get_fd_info(fd);

	if(fd < 0){
		return -1;
	}

	/* no such open fd */
	if(!info_ptr){
		exit(-1);
	}

//	lock_acquire(&file_lock);
	off_t size = file_length(t->fd_table[info_ptr->converted_fd_num]);
//	lock_release(&file_lock);

	return size;
}

 void seek (int fd, unsigned position){

	struct thread * t = thread_current ();
	struct fd_info * info_ptr = get_fd_info(fd);


	if(!info_ptr){
		return;
	}

//	lock_acquire(&file_lock);

	if(t->fd_table[info_ptr->converted_fd_num]){
		file_seek(t->fd_table[info_ptr->converted_fd_num],position);
	}

//	lock_release(&file_lock);
}

 unsigned tell (int fd){
	struct thread * t = thread_current ();
	struct fd_info * info_ptr = get_fd_info(fd);
	off_t pos = 0;


	if(!info_ptr){
		exit(-1);
	}

//	lock_acquire(&file_lock);

	if(t->fd_table[info_ptr->converted_fd_num])
		pos = file_tell(t->fd_table[info_ptr->converted_fd_num]);
		
//	lock_release(&file_lock);

	return pos;

}

 void remove_fd_info(struct fd_info * fd_info_ptr){
	 struct thread * t = thread_current ();
	 struct list * fd_list_ptr = t->fd_list_ptr;
			
	/* remove the fd_info from the fd list */
	if(list_front(fd_list_ptr) == &fd_info_ptr->fd_elem)
	{
		list_pop_front(fd_list_ptr);
	}
	else if(list_back(fd_list_ptr) == &fd_info_ptr->fd_elem)
	{
		list_pop_back(fd_list_ptr);
	}
	else
	{
		list_remove(&fd_info_ptr->fd_elem);
	}

			
 }

 void modify_fd_infos(int newfd){

	 	struct thread * t = thread_current ();
		struct list * fd_list_ptr = t->fd_list_ptr;
		struct list_elem * e;
		struct fd_info * fd_info_ptr;
		struct fd_info * newfd_info_ptr = get_fd_info(newfd);
		struct file * tmp_file = NULL;
		int index = 0;

	 	/* scan the fd list */
		for(e = list_begin(fd_list_ptr); e != list_end(fd_list_ptr); e = list_next(e))
		{
			fd_info_ptr = list_entry(e,struct fd_info,fd_elem);

			if(newfd == fd_info_ptr->fd_num)
			{
				remove_fd_info(fd_info_ptr);
			}

			/* Find the first neighbor file descriptor of fd 
			   And set it as the new fd source */
			if(newfd == fd_info_ptr->fd_same_file && tmp_file == NULL){
				tmp_file = t->fd_table[newfd_info_ptr->converted_fd_num];

				index = fd_info_ptr->fd_num;

				t->fd_table[fd_info_ptr->converted_fd_num] = tmp_file;
				if(newfd > STDOUT_FILENO){
					fd_info_ptr->fd_same_file = -1; // source
				}
			
			}
		}

		/* found neighbor */
		if(tmp_file || newfd == STDIN_FILENO || newfd == STDOUT_FILENO){

			for(e = list_begin(fd_list_ptr); e != list_end(fd_list_ptr); e = list_next(e))
			{
				/* Set the fd_info of other neighbors */
				if(newfd == fd_info_ptr->fd_same_file)
				{
					t->fd_table[fd_info_ptr->converted_fd_num] = tmp_file;
					fd_info_ptr->fd_same_file = index;
				}
			}

		}

		
 }
 void close (int fd){

	struct thread * t = thread_current ();
	struct file * file_p;
	struct list_elem * e;
	struct fd_info * fd_info_ptr = get_fd_info(fd);
	struct file * tmp_file = NULL;
	int index = 0;

	if(!fd_info_ptr){
		exit(-1);
	}

	if(fd == STDIN_FILENO){
		close_stdio(true);
		return;
	}
	else if(fd == STDOUT_FILENO){
		close_stdio(false);
		return;
	}


	//lock_acquire(&file_lock);

	/* if fd available */
	if(t->fd_table[fd_info_ptr->converted_fd_num]){
		file_p = t->fd_table[fd_info_ptr->converted_fd_num];
		
		modify_fd_infos(fd);
		t->fd_table[fd_info_ptr->converted_fd_num] = NULL;
		t->nextfd = fd_info_ptr->converted_fd_num;
	}
	//lock_release(&file_lock);
}

void close_stdio(bool is_stdin){
	struct fd_info * fd_info_ptr;
	if(is_stdin){
		fd_info_ptr = get_fd_info(STDIN_FILENO);
		fd_info_ptr->stdio_shutdown = true;
	}
	else{
		fd_info_ptr = get_fd_info(STDOUT_FILENO);
		fd_info_ptr->stdio_shutdown = true;
	}
	
}


struct fd_info * get_fd_info(int fd){
	struct list_elem * e;
	struct fd_info * fd_info_ptr;
	struct thread * t = thread_current ();
	struct list * fd_list_ptr = t->fd_list_ptr;
	int converted_fd = fd;


	for(e = list_begin(fd_list_ptr); e != list_end(fd_list_ptr); e = list_next(e)){
		fd_info_ptr = list_entry(e,struct fd_info,fd_elem);

		if(fd_info_ptr->fd_num == fd)
			return fd_info_ptr;
	}

	return NULL;
}

static void dup2_logger(int oldfd,int newfd){
	struct thread * t = thread_current ();
	struct list * fd_list_ptr = t->fd_list_ptr;
	printf("dup2(oldfd:%d,newfd:%d\n",oldfd,newfd);
	struct list_elem * e;
	struct fd_info * fd_info_ptr;

	printf("printing fd_list\n");
	for (e = list_begin(fd_list_ptr); e != list_end(fd_list_ptr); e = list_next(e))
	{
		fd_info_ptr = list_entry(e, struct fd_info, fd_elem);

		printf("fd_info_ptr->fd_num:%d\n",fd_info_ptr->fd_num);
		printf("fd_info_ptr->fd_same_file:%d\n",fd_info_ptr->fd_same_file);
		printf("fd_info_ptr->converted_fd_num:%d\n",fd_info_ptr->converted_fd_num);
		printf("fd_info_ptr->stdio_shutdown:%d\n",fd_info_ptr->stdio_shutdown);

	}


}

int dup2(int oldfd, int newfd){
	struct thread * t = thread_current ();
	struct list * fd_list_ptr = t->fd_list_ptr;
	struct file * new_file;
	struct list_elem * e;
	struct fd_info * fd_info_ptr;
	struct fd_info * oldfd_info_ptr;

	/* initialize the newfd info ptr */
	struct fd_info * newfd_info_ptr =  malloc(sizeof(struct fd_info));
	newfd_info_ptr->fd_num =  newfd_info_ptr->converted_fd_num =  newfd;
	newfd_info_ptr->stdio_shutdown = false;
	newfd_info_ptr->fd_same_file = oldfd;

	if(newfd >= MAXFD){
		newfd_info_ptr->converted_fd_num = t->nextfd;
		t->nextfd++;
	}

	/* if not valid fds */
	if(oldfd < 0 || newfd < 0 || t->maxfd >= MAXFD){
		return -1;
	}


	if(newfd == t->nextfd){
		t->nextfd++;
		/* find next available slot */
		while(t->fd_table[t->nextfd]){
			t->nextfd++;
		}
	}

	oldfd_info_ptr = get_fd_info(oldfd);

	if(!oldfd_info_ptr){
		return -1;
	}

	/* close stdin or stdout if newfd is stdin or stdout */
	if(newfd == STDOUT_FILENO &&  oldfd_info_ptr->fd_same_file != STDOUT_FILENO){
		close_stdio(false);
	}

	/* open stdout */
	else if(newfd == STDOUT_FILENO){
		struct fd_info * ptr = get_fd_info(STDOUT_FILENO);
		ptr->stdio_shutdown = false;
	}

	if(newfd == STDIN_FILENO && oldfd_info_ptr->fd_same_file != STDIN_FILENO){
		close_stdio(true);
	}

	else if(newfd == STDIN_FILENO){
		struct fd_info * ptr = get_fd_info(STDIN_FILENO);
		ptr->stdio_shutdown = false;
	}

	/* oldfd is a valid fd */
	if(t->fd_table[oldfd_info_ptr->converted_fd_num] || oldfd_info_ptr->fd_same_file == STDOUT_FILENO 
	|| oldfd_info_ptr->fd_same_file == STDIN_FILENO || oldfd <= STDOUT_FILENO){

		/* Do nothing */
		if(oldfd == newfd){
			return newfd;
		}

		if(oldfd_info_ptr->fd_same_file == STDOUT_FILENO || oldfd_info_ptr->fd_same_file == STDIN_FILENO){
			int fd_num = oldfd_info_ptr->fd_same_file;
			fd_info_ptr = get_fd_info(fd_num);
			fd_info_ptr->fd_same_file = -1;
			return newfd;
		}

		for (e = list_begin(fd_list_ptr); e != list_end(fd_list_ptr); e = list_next(e))
		{
			fd_info_ptr = list_entry(e, struct fd_info, fd_elem);

			// newfd was being used
			if (fd_info_ptr->fd_num == newfd)
			{

				// Is fd source
				if (fd_info_ptr->fd_same_file == -1)
				{
					modify_fd_infos(newfd);
					list_push_back(fd_list_ptr, &fd_info_ptr->fd_elem);
				}
				
				
				t->fd_table[fd_info_ptr->converted_fd_num] = t->fd_table[oldfd_info_ptr->converted_fd_num];
				
	
				fd_info_ptr->fd_same_file = oldfd;
				return newfd;
			}
		}

		t->fd_table[newfd_info_ptr->converted_fd_num] = t->fd_table[oldfd_info_ptr->converted_fd_num];
		list_push_front(fd_list_ptr, &newfd_info_ptr->fd_elem);

	}

	/* Not a valid fd */
	else{
		return -1;
	}

	return newfd;
}

static bool 
lazy_load_page(struct page * page,void * aux){
	ASSERT(page && aux);
	struct lazyLoadInfo * info = (struct lazyLoadInfo *)aux;
	ASSERT(info->read_bytes <= PGSIZE && info->zero_bytes <= PGSIZE);

	off_t unread_bytes = info->read_bytes;
	off_t tmp;

	//lock_acquire(&file_lock);
	file_seek(info->file_to_load,info->curr_offset);
	
	while(unread_bytes){
		tmp =  file_read(info->file_to_load,page->va + (info->read_bytes - unread_bytes),unread_bytes);
		unread_bytes -= tmp;
	
		if(tmp == 0){
			break;
		}
	}
	
	memset(page->va + (info->read_bytes - unread_bytes) ,0,info->zero_bytes);
	//close(info->file_to_load);
	free(info);
	pml4_set_dirty(thread_current ()->pml4,page->va,false);
	//lock_release(&file_lock);
	return true;
}

void *mmap (void *addr, size_t length, int writable, int fd, off_t offset){
		
	/* if fd is console or addr is NULL or not page aligned or length = 0 reject it */
	if(fd <= STDOUT_FILENO || !addr || !((uint64_t)addr + length)|| pg_ofs(addr) != 0 
	|| length == 0 || pg_ofs(offset) || is_kernel_vaddr(addr) 
	|| is_kernel_vaddr((uint64_t)addr + length)){
		return NULL;
	}
	
	struct file *tmp,*file;
	tmp = thread_current ()->fd_table[fd]; 

	if(tmp){
		file = file_reopen(tmp);
		int file_len = file_length(file);

		if(file_len == 0){
			return NULL;
		}
	}
	else{
		return NULL;
	}

	/* try allocate consecutive pages for the file */
	int num_of_pgs = (length >> PGBITS);
	int curr_ofs = offset;
	struct pg_mapping * mapping = (struct pg_mapping *) malloc(sizeof(struct pg_mapping));

	mapping->offset = offset;
	mapping->num_of_pgs = num_of_pgs;
	mapping->fd = fd;
	mapping->file = file;
	mapping->addr = addr;

	for(int i = 0; i <= num_of_pgs;i++){
		struct lazyLoadInfo * aux = (struct lazyLoadInfo *) malloc(sizeof(struct lazyLoadInfo));

		aux->file_to_load = file;
		aux->curr_offset = curr_ofs;

		if(i < num_of_pgs){
			aux->zero_bytes = 0;
			aux->read_bytes = PGSIZE;
		}
		else{
			aux->read_bytes = pg_ofs(length);
			mapping->zero_bytes = aux->zero_bytes = PGSIZE - aux->read_bytes;

			/* does not stick out */
			if(aux->read_bytes == 0){
				/* the last page is pseudo */
				mapping->zero_bytes = 0;
				mapping->num_of_pgs -= 1; 
				break;
			}
				
		}
		
		if(!vm_alloc_page_with_initializer(VM_FILE,addr + (PGSIZE * i),writable,lazy_load_page,aux))
		{
			return NULL;
		}

		curr_ofs += PGSIZE;
	}
	
	list_push_back(&thread_current ()->mapped_pg_lst,&mapping->map_elem);
	return addr;
}


void munmap (void *addr){
	do_munmap(addr,true);	
}


/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {

	switch (f->R.rax)
	{

	case SYS_HALT:
		halt();
		break;

	case SYS_EXIT:
		exit(f->R.rdi);
		break;

	case SYS_FORK:
		is_valid_addr(f->R.rdi);
		f->R.rax = fork_(f->R.rdi,f);
		break;

	case SYS_EXEC:
		is_valid_addr(f->R.rdi);
		f->R.rax = exec(f->R.rdi);
		break;

	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;

	case SYS_WRITE:
		is_valid_addr(f->R.rsi);
		is_valid_addr(f->R.rsi + f->R.rdx - 1);
		f->R.rax = write(f->R.rdi,f->R.rsi,f->R.rdx);
		break;

	case SYS_CREATE:
		is_valid_addr(f->R.rdi);
		f->R.rax = create(f->R.rdi,f->R.rsi);
		break;

	case SYS_REMOVE:
		is_valid_addr(f->R.rdi);
		f->R.rax = remove(f->R.rdi);
		break;

	case SYS_READ:
		is_valid_addr(f->R.rsi);
		is_valid_addr(f->R.rsi + f->R.rdx - 1);

		/* make sure the page exists and it is writable */
		#ifdef VM
			struct page * page = spt_find_page(&thread_current ()->spt,f->R.rsi);
			if(!page || !page->writable){
				exit(-1);
			}
		#endif
		
		f->R.rax = read(f->R.rdi,f->R.rsi,f->R.rdx);
		break;

	case SYS_OPEN:
		is_valid_addr(f->R.rdi);
		f->R.rax = open(f->R.rdi);
		break;
	
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;

	case SYS_SEEK:
		seek(f->R.rdi,f->R.rsi);
		break;

	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;

	case SYS_CLOSE:
		close(f->R.rdi);
		break;

	case SYS_DUP2:
		f->R.rax = dup2(f->R.rdi,f->R.rsi);
		break;

	case SYS_MMAP:
		f->R.rax = mmap(f->R.rdi,f->R.rsi,f->R.rdx,f->R.r10,f->R.r8);
		break;

	case SYS_MUNMAP:
		munmap(f->R.rdi);
	default:
		break;
	}

	//thread_exit ();
}
