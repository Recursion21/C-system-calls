/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>
#include <synch.h>

#define OF_TABLE_SIZE __OPEN_MAX

/*
 * Put your function declarations and data types here ...
 */
struct open_file_entry {
    struct vnode *vn;               // reference to underlying file vnode representation
    off_t f_offset;                 // current offset of the file pointer
    unsigned int ref_count;         // times this open file 
    int flags;                      // how does this file opened
    struct lock * f_lock;           // lock for atomic operation;
};

// array of pointers to open file entries
struct open_file_entry *of_table[OF_TABLE_SIZE];

// wrapper functions
int k_open(char *filename, int flags, mode_t mode, int *retval);

int sys_open(userptr_t filename, int flags, mode_t mode, int *retval);
int sys_close(int fd);
int sys_dup2(int oldfd, int newfd, int *retval);
int sys_read(int fd_index, userptr_t user_buf, size_t user_size, int *retval);
int sys_write(int fd_index, const userptr_t user_buf, size_t user_size, int *retval);
int sys_lseek(int fd_index, uint64_t pos, int whence, off_t *retval64);
int file_bootstrap(void);

// struct lock *open_lock;

#endif /* _FILE_H_ */
