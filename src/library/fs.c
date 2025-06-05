/* fs.c: SimpleFS file system */

#include "sfs/fs.h"
#include "math.h"
#include "sfs/disk.h"
#include "sfs/logging.h"
#include "sfs/utils.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* External Functions */

/**
 * Debug FileSystem by doing the following
 *
 *  1. Read SuperBlock and report its information.
 *
 *  2. Read Inode Table and report information about each Inode.
 *
 * @param       disk        Pointer to Disk structure.
 **/
void fs_debug(Disk *disk) {
  Block block;

  /* Read SuperBlock */
  if (disk_read(disk, 0, block.data) == DISK_FAILURE) {
    error("error read super");
    exit(-1);
  }
  if (block.super.magic_number != MAGIC_NUMBER) {
    error("magic number not right");
    exit(-1);
  }

  debug("read super block success");

  printf("SuperBlock:\n");
  printf("    magic number is valid\n");
  printf("    %u blocks\n", block.super.blocks);
  printf("    %u inode blocks\n", block.super.inode_blocks);
  printf("    %u inodes\n", block.super.inodes);

  /* Read Inodes */

  for (unsigned int i = 0; i < block.super.inode_blocks; i++) {
    Block inode_block;
    disk_read(disk, i + 1, inode_block.data);
    for (int idx_inode = 0; idx_inode < INODES_PER_BLOCK; idx_inode++) {
      Inode inode = inode_block.inodes[idx_inode];
      if (is_valid_Inode(&inode)) {
        printf("Inode %u:\n", i * INODES_PER_BLOCK + idx_inode);
        printf("    size: %u bytes\n", inode.size);

        // direct
        uint32_t *dir_p = direct_pointer(&inode);
        printf("    direct blocks:");
        for (int i = 0; i < POINTERS_PER_INODE; i++) {
          if (dir_p[i] != 0) {
            printf(" %u", dir_p[i]);
          }
        }
        printf("\n");
        free(dir_p);

        // indirect
        if (inode.indirect != 0) {
          uint32_t *indir_p = indirect_pointer(disk, &inode);
          printf("    indirect block: %u\n", inode.indirect);
          printf("    indirect data blocks:");
          for (int i = 0; i < POINTERS_PER_BLOCK; i++) {
            if (indir_p[i] != 0) {
              printf(" %u", indir_p[i]);
            }
          }
          printf("\n");
          free(indir_p);
        }
      }
    }
  }
}

/**
 * Format Disk by doing the following:
 *
 *  1. Write SuperBlock (with appropriate magic number, number of blocks,
 *  number of inode blocks, and number of inodes).
 *
 *  2. Clear all remaining blocks.
 *
 * Note: Do not format a mounted Disk!
 *
 * @param       fs      Pointer to FileSystem structure.
 * @param       disk    Pointer to Disk structure.
 * @return      Whether or not all disk operations were successful.
 **/
bool fs_format(FileSystem *fs, Disk *disk) {
  // if mounted disk, not do format
  if (fs->disk == disk) {
    error("disk mounted, format failed!");
    return false;
  }

  // write superblock
  Block block;
  block.super.magic_number = MAGIC_NUMBER;
  block.super.blocks = disk->blocks;
  block.super.inode_blocks = (uint32_t)ceil(disk->blocks / 10.0);
  block.super.inodes = block.super.inode_blocks * BLOCK_SIZE / 32;
  debug("format-superblock-block-inode_block %ud %ud\n", block.super.blocks,
        block.super.inode_blocks);

  if (disk_write(disk, 0, block.data) == DISK_FAILURE) {
    error("format disk wirte super block failed");
    return false;
  }

  // clear remaining block
  char *empty_block_data = malloc(BLOCK_SIZE);
  for (int i = 1; i < block.super.blocks; i++) {
    if (disk_write(disk, i, empty_block_data) == DISK_FAILURE) {
      error("format disk wirte block %d failed", i);
      return false;
    }
  }
  free(empty_block_data);

  return true;
}

/**
 * Mount specified FileSystem to given Disk by doing the following:
 *
 *  1. Read and check SuperBlock (verify attributes).
 *
 *  2. Verify and record FileSystem disk attribute.
 *
 *  3. Copy SuperBlock to FileSystem meta data attribute
 *
 *  4. Initialize FileSystem free blocks bitmap.
 *
 * Note: Do not mount a Disk that has already been mounted!
 *
 * @param       fs      Pointer to FileSystem structure.
 * @param       disk    Pointer to Disk structure.
 * @return      Whether or not the mount operation was successful.
 **/
bool fs_mount(FileSystem *fs, Disk *disk) {
  // check if already mounted
  if (fs->disk == disk) {
    error("disk already mounted.");
    return false;
  }

  // check super block
  Block block;
  if (disk_read(disk, 0, block.data) == DISK_FAILURE) {
    error("read super block error");
    return false;
  }
  if (block.super.magic_number != MAGIC_NUMBER) {
    error("magic number invalid, mount fail.");
    return false;
  }
  if (block.super.blocks != disk->blocks) {
    error("block number invalid, mount fail.");
    return false;
  }
  if (block.super.inode_blocks != ceil(disk->blocks / 10.0)) {
    error("inode block number invalid, mount fail.");
    return false;
  }
  if (block.super.inodes != block.super.inode_blocks * INODES_PER_BLOCK) {
    error("inode number invalid, mount fail.");
    return false;
  }

  // copy attribute
  fs->meta_data = block.super;
  fs->disk = disk;

  // init bitmap
  if (!init_bit_map(fs)) {
    error("init bitmap failed");
    fs->disk = NULL;
    return false;
  }

  return true;
}

/**
 * Unmount FileSystem from internal Disk by doing the following:
 *
 *  1. Set FileSystem disk attribute.
 *
 *  2. Release free blocks bitmap.
 *
 * @param       fs      Pointer to FileSystem structure.
 **/
void fs_unmount(FileSystem *fs) {
  free(fs->free_blocks);
  fs->free_blocks = NULL;
  fs->disk = NULL;
}

/**
 * Allocate an Inode in the FileSystem Inode table by doing the following:
 *
 *  1. Search Inode table for free inode.
 *
 *  2. Reserve free inode in Inode table.
 *
 * Note: Be sure to record updates to Inode table to Disk.
 *
 * @param       fs      Pointer to FileSystem structure.
 * @return      Inode number of allocated Inode.
 **/
ssize_t fs_create(FileSystem *fs) {

  // get free inode from free inode
  Block block;
  for (uint32_t i = 1; i <= fs->meta_data.inode_blocks; i++) {
    disk_read(fs->disk, i, block.data);
    for (uint32_t j = 0; j < INODES_PER_BLOCK; j++) {
      if (!is_valid_Inode(&block.inodes[j])) {
        // save to disk
        block.inodes[j].valid = 1;
        for (uint32_t k = 0; k < POINTERS_PER_INODE; k++) {
          block.inodes[j].direct[k] = 0;
        }
        block.inodes[j].indirect = 0;
        block.inodes[j].size = 0;
        disk_write(fs->disk, i, block.data);
        // update bitmap
        init_bit_map(fs);
        return j + (i - 1) * INODES_PER_BLOCK;
      }
    }
  }

  return -1;

  // bool *bit_map=fs->free_blocks;
  // int inode_idx=0;
  // for (; inode_idx<fs->meta_data.inodes;inode_idx++) {
  //     if (!bit_map[inode_idx]) {
  //         bit_map[inode_idx]==true;
  //         break;
  //     }
  // }

  // save to disk

  return -1;
}

/**
 * Remove Inode and associated data from FileSystem by doing the following:
 *
 *  1. Load and check status of Inode.
 *
 *  2. Release any direct blocks.
 *
 *  3. Release any indirect blocks.
 *
 *  4. Mark Inode as free in Inode table.
 *
 * @param       fs              Pointer to FileSystem structure.
 * @param       inode_number    Inode to remove.
 * @return      Whether or not removing the specified Inode was successful.
 **/
bool fs_remove(FileSystem *fs, size_t inode_number) {

  // check inode metadata
  Block block;
  disk_read(fs->disk, inode_number / INODES_PER_BLOCK + 1, block.data);
  Inode inode = block.inodes[inode_number % INODES_PER_BLOCK];
  if (!is_valid_Inode(&inode)) {
    error("not valid inode.");
    return false;
  }

  // release dir point
  for (uint32_t dir_idx = 0; dir_idx < POINTERS_PER_INODE; dir_idx++) {
    if (inode.direct[dir_idx]) {
      fs->free_blocks[inode.direct[dir_idx]] = 0;
    }
  }
  // release indir point
  if (inode.indirect) {
    fs->free_blocks[inode.indirect] = 0;
    Block point_block;
    disk_read(fs->disk, inode.indirect, point_block.data);
    for (uint32_t pt_idx = 0; pt_idx < POINTERS_PER_BLOCK; pt_idx++) {
      if (point_block.pointers[pt_idx]) {
        fs->free_blocks[point_block.pointers[pt_idx]] = 0;
      } else {
        break;
      }
    }
  }

  // release inode
  inode.valid = 0;
  block.inodes[inode_number % INODES_PER_BLOCK] = inode;
  disk_write(fs->disk, inode_number / INODES_PER_BLOCK + 1, block.data);

  return true;
}

/**
 * Return size of specified Inode.
 *
 * @param       fs              Pointer to FileSystem structure.
 * @param       inode_number    Inode to remove.
 * @return      Size of specified Inode (-1 if does not exist).
 **/
ssize_t fs_stat(FileSystem *fs, size_t inode_number) {

  Block block;
  size_t block_id = 1 + inode_number / INODES_PER_BLOCK;
  disk_read(fs->disk, block_id, block.data);
  Inode inode = block.inodes[inode_number % INODES_PER_BLOCK];

  return is_valid_Inode(&inode) ? (ssize_t)inode.size : -1;
}

/**
 * Read from the specified Inode into the data buffer exactly length bytes
 * beginning from the specified offset by doing the following:
 *
 *  1. Load Inode information.
 *
 *  2. Continuously read blocks and copy data to buffer.
 *
 *  Note: Data is read from direct blocks first, and then from indirect blocks.
 *
 * @param       fs              Pointer to FileSystem structure.
 * @param       inode_number    Inode to read data from.
 * @param       data            Buffer to copy data to.
 * @param       length          Number of bytes to read.
 * @param       offset          Byte offset from which to begin reading.
 * @return      Number of bytes read (-1 on error).
 **/
ssize_t fs_read(FileSystem *fs, size_t inode_number, char *data, size_t length,
                size_t offset) {
  Block block;
  disk_read(fs->disk, inode_number / INODES_PER_BLOCK + 1, block.data);
  Inode inode = block.inodes[inode_number % INODES_PER_BLOCK];
  uint32_t size = inode.size;
  // if unvalid inode
  if (!is_valid_Inode(&inode)) {
    return -1;
  }
  // read form dir pt
  if (offset >= size) {
    return -1;
  }

  int pt_idx = offset / BLOCK_SIZE;
  uint32_t block_idx;

  if (pt_idx < POINTERS_PER_INODE) {
    block_idx = inode.direct[pt_idx];
  } else {
    uint32_t indir_idx = (pt_idx - POINTERS_PER_INODE) % POINTERS_PER_BLOCK;
    Block indir_block;
    disk_read(fs->disk, inode.indirect, indir_block.data);
    block_idx = indir_block.pointers[indir_idx];
  }

  char *buf = malloc(BLOCK_SIZE);
  disk_read(fs->disk, block_idx, buf);
  memcpy(data, buf, min(size - offset, BLOCK_SIZE));

  free(buf);
  return offset + length <= size ? length : size - offset;
}

/**
 * Write to the specified Inode from the data buffer exactly length bytes
 * beginning from the specified offset by doing the following:
 *
 *  1. Load Inode information.
 *
 *  2. Continuously copy data from buffer to blocks.
 *
 *  Note: Data is read from direct blocks first, and then from indirect blocks.
 *
 * @param       fs              Pointer to FileSystem structure.
 * @param       inode_number    Inode to write data to.
 * @param       data            Buffer with data to copy
 * @param       length          Number of bytes to write.
 * @param       offset          Byte offset from which to begin writing.
 * @return      Number of bytes read (-1 on error).
 **/
ssize_t fs_write(FileSystem *fs, size_t inode_number, char *data, size_t length,
                 size_t offset) {
  Block block;
  disk_read(fs->disk, inode_number / INODES_PER_BLOCK + 1, block.data);
  Inode inode = block.inodes[inode_number % INODES_PER_BLOCK];
  if (!is_valid_Inode(&inode)) {
    return -1;
  }

  uint32_t pt_idx = offset / BLOCK_SIZE;
  if (pt_idx >= POINTERS_PER_INODE + POINTERS_PER_BLOCK) {
    return -1;
  }

  // malloc free block
  uint32_t bk_idx = assign_block(fs);
  if (bk_idx == -1) {
    return -1;
  }
  char *buf = malloc(BLOCK_SIZE);
  memcpy(buf, data, length);
  ssize_t result = disk_write(fs->disk, bk_idx, data);
  free(buf);

  // change pointer metadata in inode
  if (pt_idx < POINTERS_PER_INODE) {
    // dir pointer
    inode.direct[pt_idx] = bk_idx;
  } else {
    // malloc indir block
    uint32_t indir_pt_bk_idx;
    if (!inode.indirect) {
      indir_pt_bk_idx = assign_block(fs);
      if (indir_pt_bk_idx == -1) {
        unassign_block(fs, bk_idx);
        return -1;
      }
      inode.indirect = indir_pt_bk_idx;
    } else {
      indir_pt_bk_idx = inode.indirect;
    }

    Block point_block;
    disk_read(fs->disk, indir_pt_bk_idx, point_block.data);
    uint32_t i = 0;
    for (; i < POINTERS_PER_BLOCK; i++) {
      if (point_block.pointers[i] == 0) {
        point_block.pointers[i] = bk_idx;
        break;
      }
    }

    if (i >= POINTERS_PER_BLOCK) {
      error("reach to max inode size.");
      return -1;
    }

    disk_write(fs->disk, indir_pt_bk_idx, point_block.data);
  }

  // change inode metadata
  inode.size += length;
  block.inodes[inode_number % INODES_PER_BLOCK] = inode;
  disk_write(fs->disk, inode_number / INODES_PER_BLOCK + 1, block.data);
  return length;
}

bool is_valid_Inode(Inode *inode) { return inode->valid == 1; }

uint32_t direct_pointer_num(Inode *inode) {
  int direct_p_num = 0;
  for (int i = 0; i < POINTERS_PER_INODE; i++) {
    if (inode->direct[i] != 0) {
      direct_p_num++;
    }
  }
  return direct_p_num;
}

uint32_t *direct_pointer(Inode *inode) {
  uint32_t *dir_p = malloc(sizeof(uint32_t) * POINTERS_PER_INODE);
  for (int i = 0; i < POINTERS_PER_INODE; i++) {
    if (inode->direct[i] != 0) {
      dir_p[i] = inode->direct[i];
    }
  }
  return dir_p;
}

uint32_t *indirect_pointer(Disk *disk, Inode *inode) {
  if (inode->indirect == 0)
    return NULL;
  Block *block = malloc(sizeof(Block));
  disk_read(disk, inode->indirect, block->data);
  return block->pointers;
}

uint32_t indirect_pointer_num(uint32_t *pointers) {
  uint32_t indir_p_num = 0;
  for (uint32_t i = 0; i < POINTERS_PER_BLOCK; i++) {
    if (pointers[i] != 0) {
      indir_p_num++;
    }
  }
  return indir_p_num;
}

void *free_block_of_inode(Disk *disk, Inode *inode, bool *block_map) {
  uint32_t *dir_p = direct_pointer(inode);
  uint32_t *indir_p = indirect_pointer(disk, inode);

  for (int i = 0; i < POINTERS_PER_INODE; i++) {
    if (dir_p[i] != 0) {
      block_map[dir_p[i]] = 1;
    }
  }

  if (indir_p != NULL) {
    for (int i = 0; i < POINTERS_PER_BLOCK; i++) {
      if (indir_p[i] != 0) {
        block_map[indir_p[i]] = 1;
      }
    }
    block_map[inode->indirect] = 1;
  }

  free(dir_p);
  free(indir_p);

  return 0;
}

bool busy_block_of_disk(FileSystem *fs, bool *block_map) {
  block_map[0] = 1; // super block
  // check used block
  Block block;
  for (int i = 1; i <= fs->meta_data.inode_blocks; i++) {
    if (disk_read(fs->disk, i, block.data) == DISK_FAILURE) {
      error("read disk failure in init bit map");
      return false;
    }

    for (int j = 0; j < INODES_PER_BLOCK; j++) {

      Inode inode = block.inodes[j];
      if (is_valid_Inode(&inode)) {
        free_block_of_inode(fs->disk, &inode, block_map);
      }
    }
  }

  return true;
}

bool init_bit_map(FileSystem *fs) {
  uint32_t bn = fs->meta_data.blocks;
  fs->free_blocks = malloc(bn * sizeof(bool));
  if (!busy_block_of_disk(fs, fs->free_blocks)) {
    return false;
  }
  // inode block set to busy
  for (uint32_t i = 1; i <= fs->meta_data.inode_blocks; i++) {
    fs->free_blocks[i] = 1;
  }
  return true;
}

uint32_t assign_block(FileSystem *fs) {
  for (uint32_t i = 0; i < fs->meta_data.blocks; i++) {
    if (!fs->free_blocks[i]) {
      fs->free_blocks[i] = 1;

      // clean block
      Block block;
      for (uint32_t j = 0; j < POINTERS_PER_BLOCK; j++) {
        block.pointers[j] = 0;
      }

      disk_write(fs->disk, i, block.data);
      return i;
    }
  }

  error("malloc block fail.");
  return -1;
}

uint32_t unassign_block(FileSystem *fs, uint32_t bid) {
  if (fs->free_blocks[bid]) {
    fs->free_blocks[bid] = 0;
  }
  error("free block fail.");
  return -1;
}
