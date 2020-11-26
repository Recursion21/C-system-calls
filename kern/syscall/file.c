#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>
#include <proc.h>

/*
 * Add your file-related functions here ...
 */

// int file_bootstrap() {
//     char ln[12] = "open_lock";
//     open_lock = lock_create(ln);
//     if (open_lock == NULL) {
//         return ENOMEM;
//     }
//     return 0;
// }

// sets retval to fd that is returned
int sys_open(userptr_t filename, int flags, mode_t mode, int *retval) {
    // variables needed: open file entry. vnode. stat struct. result
    int result;
    char *k_buf;
    size_t len;
    k_buf = (char *) kmalloc(sizeof(char) * PATH_MAX);

    // copy string into memory buffer and sanity check using copyinstr
    // if errored, return result
    result = copyinstr(filename, k_buf, PATH_MAX, &len);
    if (result) {
        kfree(k_buf);
        return result;
    };

    result = k_open(k_buf, flags, mode, retval);
    kfree(k_buf);

    return result;
}

int k_open(char *filename, int flags, mode_t mode, int *retval) {
    // Using vfs_open, initialise the vnode vn
    struct vnode *vn;
    int result;
    result = vfs_open(filename, flags, mode, &vn);
    if (result) {
        return result;
    }

    // critical region for index searching, opening a file and creating of struct
    // lock_acquire(open_lock);

    // search the open file table for a free slot
    int ofe_index;
    for (ofe_index = 0; ofe_index < OF_TABLE_SIZE; ofe_index++) {
        if (of_table[ofe_index] == NULL) {
            break;
        } 
    }
    if (ofe_index == OF_TABLE_SIZE) {
        // lock_release(open_lock);
        return ENFILE;
    }

    // allocate space for the free slot found in the open file table
    of_table[ofe_index] = kmalloc(sizeof(struct open_file_entry));
    // memory not successfully allocated
    if (of_table[ofe_index] == NULL) {
        vfs_close(vn);
        // lock_release(open_lock);
        return ENOMEM;
    }

    // allocate all attributes in open file table entry
    of_table[ofe_index]->f_offset = 0;
    of_table[ofe_index]->vn = vn;
    of_table[ofe_index]->flags = flags;
    of_table[ofe_index]->ref_count += 1;
    of_table[ofe_index]->f_lock = lock_create(filename);

    // create a pointer to the open file entry in the open file table
    struct open_file_entry **of_entry = &of_table[ofe_index];

    // search the per-process file table for a free slot
    int fd_index;
    for (fd_index = 0; fd_index < OPEN_MAX; fd_index++) {
        if (curproc->fd_table[fd_index] == NULL) {
            break;
        }
    }
    if (fd_index == OPEN_MAX) {
        kfree(*of_entry);
        vfs_close(vn);
        // lock_release(open_lock);
        return EMFILE;
    }

    // assign memory for the open file entry pointer in the file descriptor table. (array of pointers)
    curproc->fd_table[fd_index] = kmalloc(sizeof(struct open_file_entry*));
    if (curproc->fd_table[fd_index] == NULL) {
        kfree(*of_entry);
        vfs_close(vn);
        // lock_release(open_lock);
        return ENOMEM;
    }

    // make the file descriptor pointer reference the correct open file entry
    curproc->fd_table[fd_index] = of_entry;
    
    // lock_release(open_lock);

    // set return value to file descriptor slot found in process's file descriptor table
    *retval = fd_index;

    return 0;
}

int sys_close(int fd) {
    struct open_file_entry *of_entry = *(curproc->fd_table[fd]);
    // could be a shared resource among threads. acquire lock
    lock_acquire(of_entry->f_lock);

    // check valid fd and contents of fd slot
    if (fd >= OPEN_MAX || fd < 0 || curproc->fd_table[fd] == NULL) {
        return EBADF;
    }

    // set slot in file descriptor table to null
    curproc->fd_table[fd] = NULL;
    // remove reference count from the current process fd table entry
    of_entry->ref_count--;
    // if reference count is greater than zero, don't do anything and release lock
    if (of_entry->ref_count > 0) {
        lock_release(of_entry->f_lock);
    }
    // if reference count is zero, remove entry from the open file table
    else if (of_entry->ref_count == 0) {
        vfs_close(of_entry->vn);
        lock_release(of_entry->f_lock);
        lock_destroy(of_entry->f_lock);
        kfree(of_entry);
        of_entry = NULL;
    }
    return 0;
}

int sys_dup2(int oldfd, int newfd, int *retval) {
    int res;
    // check file descriptors provided are inside fd table range
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
        return EBADF;
    }

        // If oldfd is a valid file descriptor, and newfd has the same value
    // as oldfd, then dup2() does nothing, and returns newfd.
    if (oldfd == newfd) {
        *retval = newfd;
        return 0;
    }

    // check if old file descriptor exists 
    if (curproc->fd_table[oldfd] == NULL){
        return EBADF;
    }

    // pointers to the open file entries pointed to by newfd and old in fd_table
    struct open_file_entry *old_ofe = *(curproc->fd_table[oldfd]);
    
    // CRITICAL REGION START
    // The steps of closing and reusing the file descriptor newfd are
    // performed atomically.
    lock_acquire(old_ofe->f_lock);
    // check existence of open file descriptor on newfd. If found, close
    if (curproc->fd_table[newfd] != NULL) {
        res = sys_close(newfd);
        if (res) {
            lock_release(old_ofe->f_lock);
            return res;
        }
    }
    curproc->fd_table[newfd] = curproc->fd_table[oldfd];
    
    // create a file descriptor copy for the new file descriptor.
    // ref count of open file entry is incremented and is the same for both
    old_ofe->ref_count++;
    
    lock_release(old_ofe->f_lock);

    *retval = newfd;
    return 0;
}

int sys_read(int fd_index, userptr_t user_buf, size_t user_size, int *retval) {

    // if fd out of bounds/actually exists, return invalid fd error
    if (fd_index < 0 || fd_index >= OPEN_MAX || curproc->fd_table[fd_index] == NULL) {
        return EBADF;
    }

    // get the pointer to the open file struct
    struct open_file_entry *read_fileptr = *(curproc->fd_table[fd_index]);

    // check open flags to see if file can be read
    if ((read_fileptr->flags & O_ACCMODE) == O_WRONLY) {
        return EBADF;
    }

    int result;
    struct iovec read_iov;
    struct uio read_uio;
    int file_offset = read_fileptr->f_offset;
    struct vnode *read_vn = read_fileptr->vn;

    // initialise the read_iov, read_uio structs 
    uio_uinit(&read_iov, &read_uio, user_buf, user_size, file_offset, UIO_READ);

    // atomic file read
    lock_acquire(read_fileptr->f_lock);
    
    // read data from vnode to iovec buffer with uio metadata
    result = VOP_READ(read_vn, &read_uio);
    if (result) {
        lock_release(read_fileptr->f_lock);
        return result;
    }

    lock_release(read_fileptr->f_lock);

    // update return value, i.e. number of bytes read
    *retval = user_size - read_uio.uio_resid;

    // update file pointer in file
    read_fileptr->f_offset = read_uio.uio_offset;

    return 0;
}

int sys_write(int fd_index, const userptr_t user_buf, size_t user_size, int *retval) {

    // check if fd is valid
    if (fd_index < 0 || fd_index >= OPEN_MAX || curproc->fd_table[fd_index] == NULL) {
        return EBADF;
    }

    struct open_file_entry *write_fileptr = *(curproc->fd_table[fd_index]);

    // check flags to see if able to write
    if ((write_fileptr->flags & O_ACCMODE) == O_RDONLY) {
        return EBADF;
    }

    int result;
    struct iovec write_iov;
    struct uio write_uio;
    int file_offset = write_fileptr->f_offset;
    struct vnode *write_vn = write_fileptr->vn;

    // initialise the read_iov, read_uio structs 
    uio_uinit(&write_iov, &write_uio, user_buf, user_size, file_offset, UIO_WRITE);

    // atomic write to file
    lock_acquire(write_fileptr->f_lock);

    // write data from vnode to iovec buffer with uio metadata
    result = VOP_WRITE(write_vn, &write_uio);
    if (result) {
        lock_release(write_fileptr->f_lock);
        return result;
    }

    lock_release(write_fileptr->f_lock);
    
    // set return value 
    *retval = user_size - write_uio.uio_resid;

    write_fileptr->f_offset = write_uio.uio_offset;

    return 0;
}

int sys_lseek(int fd_index, uint64_t pos, int whence, off_t *retval64) {
    
    // check if fd is legit
    if (fd_index < 0 || fd_index >= OPEN_MAX || curproc->fd_table[fd_index] == NULL) {
        return EBADF;
    }

    struct open_file_entry *of_entry = *(curproc->fd_table[fd_index]);
    struct vnode *vn = of_entry->vn;
    struct stat stat_buf;
    // get file size
    int err = VOP_STAT(vn, &stat_buf);
    if (err) {
        return err;
    }

    // check if fd refers to an object that is seekable
    if (!VOP_ISSEEKABLE(vn)) {
        return ESPIPE;
    }
    lock_acquire(of_entry->f_lock);
    // cases based on whence
    switch (whence) {
        case SEEK_SET:
            *retval64 = pos;
            break;
        case SEEK_CUR:
            *retval64 = of_entry->f_offset + pos;
            break;
        case SEEK_END:
            *retval64 = stat_buf.st_size + pos;
            break;
        default:
            // invalid whence
            lock_release(of_entry->f_lock);
            return EINVAL;
    }

    // check if resulting file offset would be negative
    if (*retval64 < 0) {
        lock_release(of_entry->f_lock);
        return EINVAL;
    }

    lock_release(of_entry->f_lock);
    // update new file pointer offset
    of_entry->f_offset = *retval64;

    return 0;
}
