#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
struct lock file_lock;

struct file_
{
	struct file * file_addr;
	struct list_elem elem;
};

struct file
{
	struct inode *inode;        /* File's inode. */
	off_t pos;                  /* Current position. */
	bool deny_write;            /* Has file_deny_write() been called? */
};

void halt (void)
{
	shutdown_power_off();
}

void exit (int stat)
{
	printf("%s: exit(%d)\n",thread_current()->name, stat);
	thread_current()->exit_status = stat;
	while(!list_empty(&(thread_current()->fd)))	
		close(3);;
	thread_exit ();
}

pid_t exec (const char *cmd_line)
{
	if(!cmd_line)
		return -1;
	pid_t pid = process_execute(cmd_line);
	return pid;
}

int wait (pid_t pid)
{
	return process_wait(pid);
}

int read (int fd, void* buffer, unsigned size)
{
	if(!is_user_vaddr(buffer))
		exit(-1);

	lock_acquire(&file_lock);
	int i = 0;
	if (fd == 0) {
		char *buffer_c = (char *) buffer;
		for (i = 0; i < size; i++)
			buffer_c[i] = input_getc();
		lock_release(&file_lock);
		return size;
	}
	else if(fd == 1 || fd == 2)
	{
		lock_release(&file_lock);
		return -1;
	}
	else{
		if(list_empty(&(thread_current()->fd))) {
			lock_release(&file_lock);
			return -1;
		}
		struct list_elem* e = list_begin(&(thread_current()->fd));
		struct file_* f = NULL;
		for(int j = 3; j != fd; j++) {
			e = e->next;
		}
		f = list_entry(e, struct file_, elem);

		if (f->file_addr == NULL) {
			lock_release(&file_lock);
			exit(-1);
		}

		lock_release(&file_lock);
		return file_read(f->file_addr, buffer, size);
	}

	lock_release(&file_lock);
	return i;
}

int write (int fd, const void *buffer, unsigned size)
{
	if(!is_user_vaddr(buffer))
		exit(-1);

	lock_acquire(&file_lock);

	if (fd == 1) {
		putbuf(buffer, size);
		lock_release(&file_lock);
		return size;
	}
	else if (fd >= 3) {
		if(list_empty(&(thread_current()->fd))) {
			lock_release(&file_lock);
			exit(-1);
		}
		struct list_elem* e = list_begin(&(thread_current()->fd));
		struct file_* f = NULL;
		for(int i = 3; i != fd; i++) {
			e = e->next;
		}
		f = list_entry(e, struct file_, elem);
		if(f->file_addr->deny_write)
		{
			file_deny_write(f->file_addr);
		}
		if (f->file_addr == NULL) {
			lock_release(&file_lock);
			exit(-1);
		}

		lock_release(&file_lock);
		return file_write(f->file_addr, buffer, size);
	}

	lock_release(&file_lock);
	return -1;
}




bool create (const char *file, unsigned init_size) {
	if (file == NULL)
		exit(-1);
	return filesys_create(file, init_size);
}

bool remove (const char *file) {
	if (file == NULL)
		exit(-1);
	return filesys_remove(file);
}

int open(const char *file) {
	if (file == NULL)
		exit(-1);
	if(!is_user_vaddr(file))
		exit(-1);

	lock_acquire(&file_lock);

	struct file* fp = filesys_open(file);

	if (fp == NULL) {
		lock_release(&file_lock);
		return -1;
	}
	else {
		struct file_* temp = (struct file_*)malloc(sizeof(struct file_));
		if(!temp)
		{
			lock_release(&file_lock);
			return -1;
		}
		struct list_elem* e;
		for(e = list_begin(&(thread_current()->fd)); e != NULL; e=e->next)
		{
			struct file_* f;
			f = list_entry(e, struct file_, elem);
			if(strcmp(thread_current()->name, file) == 0)
				file_deny_write(fp);
		}
		temp->file_addr = fp;
		list_push_back(&(thread_current()->fd), &temp->elem);
		int cnt = (int) list_size(&(thread_current()->fd)) + 2;


		lock_release(&file_lock);
		return cnt;
	}

}

int filesize (int fd) {
	if(list_empty(&(thread_current()->fd)))
		return -1;

	struct list_elem* e = list_begin(&(thread_current()->fd));
	struct file_* f = NULL;
	for(int i = 3; i < fd; i++) {
		e = e->next;
	}
	f = list_entry(e, struct file_, elem);

	if (f->file_addr == NULL)
		exit(-1);

	return file_length(f->file_addr);
}

void seek (int fd, unsigned position) {
	if(list_empty(&(thread_current()->fd)))
		return;

	struct list_elem* e = list_begin(&(thread_current()->fd));
	struct file_* f = NULL;
	for(int i = 3; i < fd; i++) {
		e = e->next;
	}
	f = list_entry(e, struct file_, elem);

	if (f->file_addr == NULL)
		exit(-1);

	file_seek(f->file_addr, position);
}

unsigned tell (int fd) {
	if(list_empty(&(thread_current()->fd)))
		return -1;

	struct list_elem* e = list_begin(&(thread_current()->fd));
	struct file_ *f;
	for(int i = 3; i < fd; i++) {
		e = e->next;
	}
	f = list_entry(e, struct file_, elem);

	if (f->file_addr == NULL)
		exit(-1);

	return file_tell(f->file_addr);
}

void close (int fd) {
	if(list_empty(&(thread_current()->fd)))
		return;

	struct list_elem* e = list_begin(&(thread_current()->fd));
	struct file_* f = NULL;
	for(int i = 3; i < fd; i++) {
		e = e->next;
	}

	f = list_entry(e, struct file_, elem);
	if (f->file_addr != NULL)
	{
		file_close(f->file_addr);
		list_remove(&f->elem);
		free(f);
	}
}

	void
syscall_init (void) 
{
	lock_init(&file_lock);
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

	static void
syscall_handler (struct intr_frame *f) 
{
	switch (*(uint32_t *)(f->esp)){
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			if (!is_user_vaddr(f->esp + 4))
				exit(-1);
			exit(*(uint32_t *)(f->esp + 4));
			break;
		case SYS_EXEC:
			if (!is_user_vaddr(f->esp + 4))
				exit(-1);
			f->eax = exec((const char *)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_WAIT:
			if (!is_user_vaddr(f->esp + 4))
				exit(-1);
			f->eax = wait((pid_t)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_CREATE:
			if (!is_user_vaddr(f->esp + 16) || !is_user_vaddr(f->esp + 20))
				exit(-1);
			f->eax = create((const char *)*(uint32_t *)(f->esp + 16), (unsigned)*(uint32_t *)(f->esp + 20));
			break;
		case SYS_REMOVE:
			if (!is_user_vaddr(f->esp + 4))
				exit(-1);
			f->eax = remove((const char *)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_OPEN:
			if (!is_user_vaddr(f->esp + 4))
				exit(-1);
			f->eax = open((const char *)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_FILESIZE:
			if (!is_user_vaddr(f->esp + 4))
				exit(-1);
			f->eax = filesize((const char *)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_READ:
			if (!is_user_vaddr(f->esp + 20) || !is_user_vaddr(f->esp + 24) || !is_user_vaddr(f->esp + 28))
				exit(-1);
			f->eax = read((int)*(uint32_t *)(f->esp + 20), (void *)*(uint32_t *)(f->esp + 24), (unsigned)*(uint32_t *)(f->esp + 28));
			break;
		case SYS_WRITE:
			if (!is_user_vaddr(f->esp + 20) || !is_user_vaddr(f->esp + 24) || !is_user_vaddr(f->esp + 28))
				exit(-1);
			f->eax = write((int)*(uint32_t *)(f->esp + 20), (void *)*(uint32_t *)(f->esp + 24), (unsigned)*(uint32_t *)(f->esp + 28));
			break;
		case SYS_SEEK:
			if (!is_user_vaddr(f->esp + 16) || !is_user_vaddr(f->esp + 20))
				exit(-1);
			seek((const char *)*(uint32_t *)(f->esp + 16), (unsigned)*(uint32_t *)(f->esp + 20));
			break;
		case SYS_TELL:
			if (!is_user_vaddr(f->esp + 4))
				exit(-1);
			f->eax = tell((const char *)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_CLOSE:
			if (!is_user_vaddr(f->esp + 4))
				exit(-1);
			close((const char *)*(uint32_t *)(f->esp + 4));
			break;
	} 
}


