/* disk.c: SimpleFS disk emulator */

#include "sfs/disk.h"
#include "sfs/fs.h"
#include "sfs/logging.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef DEBUG_LOGS
#define LOG_ERROR(msg) perror(msg)
#define LOG_INFO(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#endif

/* Internal Prototyes */

bool    disk_sanity_check(Disk *disk, size_t blocknum, const char *data);

/* External Functions */

/**
 *
 * Opens disk at specified path with the specified number of blocks by doing
 * the following:
 *
 *  1. Allocates Disk structure and sets appropriate attributes.
 *
 *  2. Opens file descriptor to specified path.
 *
 *  3. Truncates file to desired file size (blocks * BLOCK_SIZE).
 *
 * @param       path        Path to disk image to create.
 * @param       blocks      Number of blocks to allocate for disk image.
 *
 * @return      Pointer to newly allocated and configured Disk structure (NULL
 *              on failure).
 **/
Disk *	disk_open(const char *path, size_t blocks) {
    // malloc disk struct
    Disk *disk = malloc(sizeof(Disk));
    if (disk==NULL) {
        error("malloc disk struct error");
        return NULL;
    }

    // open fd
    int fd = open(path, O_RDWR | O_CREAT , 0666);
    if (fd<0) {
        error("open disk file error");
        free(disk);
        return NULL;
    }

    // truncate
    struct stat f_stat;
    if (fstat(fd,&f_stat)==-1) {
        error("fstat error");
        return NULL;
    }
    off_t file_size=f_stat.st_size;
    off_t desire_file_size=blocks*BLOCK_SIZE;
    debug("file_size %lld desire_file_size %lld",file_size,desire_file_size);
    if (file_size!=desire_file_size) {
         if (ftruncate(fd,desire_file_size)!=0){
            error("truncate file error");
            close(fd);
            free(disk);
            return NULL;
        }
    }
    
    // init disk params
    disk->blocks=blocks;
    disk->fd=fd;
    disk->reads=0;
    disk->writes=0;

    return disk;
}

/**
 * Close disk structure by doing the following:
 *
 *  1. Close disk file descriptor.
 *
 *  2. Report number of disk reads and writes.
 *
 *  3. Releasing disk structure memory.
 *
 * @param       disk        Pointer to Disk structure.
 */
void	disk_close(Disk *disk) {
    // close fd
    if (close(disk->fd)==-1){
        error("release fd fail.");
    }

    // report
    printf("%zu disk block reads\n",disk->reads);
    printf("%zu disk block writes\n",disk->writes);

    // realease
    free(disk);
}

/**
 * Read data from disk at specified block into data buffer by doing the
 * following:
 *
 *  1. Performing sanity check.
 *
 *  2. Seeking to specified block.
 *
 *  3. Reading from block to data buffer (must be BLOCK_SIZE).
 *
 * @param       disk        Pointer to Disk structure.
 * @param       block       Block number to perform operation on.
 * @param       data        Data buffer.
 *
 * @return      Number of bytes read.
 *              (BLOCK_SIZE on success, DISK_FAILURE on failure).
 **/
ssize_t disk_read(Disk *disk, size_t block, char *data) {
    // sanity check
    if (!disk_sanity_check(disk, block, data)){
        error("check disk sanity fail");
        return DISK_FAILURE;
    }
    // to specified block
    off_t loc = block*BLOCK_SIZE;
    // read from block
    if (pread(disk->fd,data,BLOCK_SIZE,loc)<0){
        error("read superblock error");
        return DISK_FAILURE;
    }
    disk->reads++;
    return 0;
}

/**
 * Write data to disk at specified block from data buffer by doing the
 * following:
 *
 *  1. Performing sanity check.
 *
 *  2. Seeking to specified block.
 *
 *  3. Writing data buffer (must be BLOCK_SIZE) to disk block.
 *
 * @param       disk        Pointer to Disk structure.
 * @param       block       Block number to perform operation on.
 * @param       data        Data buffer.
 *
 * @return      Number of bytes written.
 *              (BLOCK_SIZE on success, DISK_FAILURE on failure).
 **/
ssize_t disk_write(Disk *disk, size_t block, char *data) {
    // sanity check
    if (!disk_sanity_check(disk, block, data)){
        error("check disk sanity fail");
        return DISK_FAILURE;
    }

    // seek off
    off_t loc = block*BLOCK_SIZE;

    // write
    if (pwrite(disk->fd, data, BLOCK_SIZE, loc)<0) {
        error("error write to disk, block %zu off_t %llu",block,loc);
        return DISK_FAILURE;
    }

    disk->writes++;

    return BLOCK_SIZE;
}

/* Internal Functions */
/** Perform sanity check before read or write operation: 1. Check for valid disk. 2. Check for valid block.
 *
 *  3. Check for valid data.
 *
 * @param       disk        Pointer to Disk structure.
 * @param       block       Block number to perform operation on.
 * @param       data        Data buffer.
 *
 * @return      Whether or not it is safe to perform a read/write operation
 *              (true for safe, false for unsafe).
 **/
bool    disk_sanity_check(Disk *disk, size_t block, const char *data) {
    debug("block num %ld",block);
    // check disk
    if (NULL==disk) {
        error("check sanity fail: disk null");
        return false;
    }
    // check fd
    if (disk->fd==-1) {
        error("check sanity fail: fd -1");
        return false;
    }
    // check for block
    if (block>=disk->blocks) {
        error("check sanity fail: block out of range");
        return false;
    }
    // check for data buffer
    if (data==NULL) {
        error("check sanity fail: data buffer null");
        return false;
    }
    return true; 
} 

/* vim: set expandtab sts=4 sw=4 ts=8 ft=c: */
