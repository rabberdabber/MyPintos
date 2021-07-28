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

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* An open file. */
struct file {
	struct inode *inode;        /* File's inode. */
	off_t pos;                  /* Current position. */
	bool deny_write;            /* Has file_deny_write() been called? */
};


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

	// if not user virtual address
	if(is_kernel_vaddr(addr)){
		exit(-1);
	}

	uint64_t *pte = pml4e_walk(thread_current ()->pml4,(const uint64_t) addr,0);

	if(!pte || !pml4_get_page(thread_current ()->pml4,(const void *)addr)){
		exit(-1);
	}


	
}


void halt (void){
	power_off();
}

 void exit (int status) {

	struct thread * t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n",t->name,status);

	/* wake up any waiting parent thread  and hold exit_status intact*/
	if(t->waiting_for_me){
		sema_up(&t->sema_wait);
		sema_down(&t->sema_wait_status);
	}

	
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


	int written_sofar = 0;
	struct thread *t = thread_current ();
	struct file * file_p;

	if(fd == STDIN_FILENO || t->next_fd <= fd || !buffer){
		exit(-1);
	}

	if(fd == STDOUT_FILENO){
			putbuf(buffer,length);
			return length;
	}

	else{
		lock_acquire(&file_lock);
		if(!t->fd_table[fd]){
			lock_release(&file_lock);
			exit(-1);
		}

		file_p = t->fd_table[fd];
		written_sofar = file_write(t->fd_table[fd],buffer,length);
		printf("written so far is %d while length is %d and file deny write is %d\n",written_sofar,length,t->fd_table[fd]->deny_write);
		lock_release(&file_lock);
	}

	return written_sofar;
}

 bool create (const char *file, unsigned initial_size){

	if(!file){
		exit(-1);
	}
	
	lock_acquire(&file_lock);
	bool created = filesys_create(file,initial_size);
	lock_release(&file_lock);

	return created;
}

 bool remove (const char *file){

	if(!file){
		exit(-1);
	}
	lock_acquire(&file_lock);
	bool removed = filesys_remove(file);
	lock_release(&file_lock);

	return removed;
}

 int read (int fd, void *buffer, unsigned length){

	struct thread * t = thread_current ();

	if(fd == STDOUT_FILENO || t->next_fd <= fd || !buffer){
		exit(-1);
	}

	// is stdin
	int read_sofar = 0;

	if(fd == STDIN_FILENO){

	/*	while(read_sofar < length){
			((char *)buffer)[read_sofar] = input_getc();
			read_sofar++;
		}*/
		read_sofar = input_getc();

	}

	else {
		lock_acquire(&file_lock);
		if(!t->fd_table[fd]){
			lock_release(&file_lock);
			exit(-1);
		}
		read_sofar = file_read(t->fd_table[fd],buffer,length);
		lock_release(&file_lock);
	}
	
	return read_sofar;
}

 int open (const char *file){

	struct thread * t = thread_current ();

	if(!file){
		exit(-1);
	}

	lock_acquire(&file_lock);


	struct file * open_file = filesys_open(file);

	lock_release(&file_lock);


	if(!open_file){
		return -1;
	}

	struct inode *inode = file_get_inode(open_file);
	int deny_write_cnt = inode_get_deny_write_cnt(inode);

	/* if the file is denied writing privileges */
	if(deny_write_cnt > 0){
		open_file->deny_write = true;
	}

	if(deny_write_cnt == 0){
		open_file->deny_write = false;
	}

	t->fd_table[t->next_fd] = open_file;

	return t->next_fd++;

}

 int filesize (int fd){

	struct thread * t = thread_current ();

	/* no such open fd */
	if(t->next_fd <= fd){
		exit(-1);
	}

	lock_acquire(&file_lock);
	off_t size = file_length(t->fd_table[fd]);
	lock_release(&file_lock);

	return size;
}

 void seek (int fd, unsigned position){

	struct thread * t = thread_current ();

	if(t->next_fd <= fd){
		exit(-1);
	}

	lock_acquire(&file_lock);

	if(t->fd_table[fd]){
		file_seek(t->fd_table[fd],position);
	}

	lock_release(&file_lock);
}

 unsigned tell (int fd){
	struct thread * t = thread_current ();
	off_t pos = 0;

	if(t->next_fd <= fd){
		exit(-1);
	}

	lock_acquire(&file_lock);

	if(t->fd_table[fd])
		pos = file_tell(t->fd_table[fd]);
		
	lock_release(&file_lock);

	return pos;

}

 void close (int fd){

	struct thread * t = thread_current ();
	struct file * file_p;

	if(t->next_fd <= fd){
		exit(-1);
	}

	lock_acquire(&file_lock);
	/* if fd available */
	if(t->fd_table[fd]){
		file_p = t->fd_table[fd];
		struct inode *inode = file_get_inode(file_p);
		int deny_write_cnt = inode_get_deny_write_cnt(inode);

		if(deny_write_cnt > 0){
			file_deny_write(t->fd_table[fd]);
		}

		file_close(t->fd_table[fd]);
		t->fd_table[fd] = NULL;
	}
	lock_release(&file_lock);

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
		break;
	
	default:
		break;
	}

	//thread_exit ();
}
