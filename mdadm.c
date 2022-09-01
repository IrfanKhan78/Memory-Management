#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"
#include "net.h"

/*creating an error variable to determine the state of the disk.
initially set to JBOD_ALREADY_UNMOUNTED */
jbod_error_t error = JBOD_ALREADY_UNMOUNTED;

int mdadm_mount(void)
{
  // checking if the disks are already unmounted
  if (error == 3)
  {
    uint32_t op = 0;                 // 32 bit unsigned integer
    jbod_cmd_t command = JBOD_MOUNT; // to mount the disks, mount command is called
    op = command << 26;              // the command is shifted 26 bits
    jbod_client_operation(op, NULL);        // the op is passed to the jbod_operation function which mounts the disks
    error = JBOD_ALREADY_MOUNTED;    // in the end set the error to already mounted
    return 1;
  }
  else
  {
    return -1;
  }
}

int mdadm_unmount(void)
{
  // checking whether the disks are mounted or not
  if (error == 2)
  {
    uint32_t op = 0;
    jbod_cmd_t command = JBOD_UNMOUNT; // to unmount the disks, unmount command is called
    op = command << 26;                // the command is shifted 26 bits
    jbod_client_operation(op, NULL);          // the op is passed to the jbod_operation function which unmounts the disks
    error = JBOD_ALREADY_UNMOUNTED;    // in the end set the error to already unmounted
    return 1;
  }
  else
  {
    return -1;
  }
}

// this function returns the block id
int get_block_id(uint32_t address, int disk_id)
{

  int block_id;
  int byte = 256;

  /* each disk have 256 blocks (id: 0-255). and each block have 256 bytes.
  so 256 x 256 = 65536 bytes/disk. to find block id, we can divid address with 256.*/
  block_id = address / byte;

  if (block_id > 255) // if block id is greater than 255, that means it go to the next disk
  {
    block_id = (address / byte) - (256 * disk_id);
  }
  return block_id;
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf)
{
  int block_id;
  int disk_id;
  int size;
  int bytes_read; // total bytes already read
  int diff; // the bytes that should be read
  int starting_point; // the point from where reading starts 

  uint8_t temp_buf1[256]; // temporary buffer where the contents from the block is added

  jbod_cmd_t command;
  uint32_t op;

  /* if the disks are unmounted, the operation will fail */
  if (error == 3)
  {
    return -1;
  }
  // if len is greater tha 1024, the operation will fail
  else if (len > 1024)
  {
    return -1;
  }
  /* if address is greater than 1MB or address is negative the operation will fail */
  else if (addr > 1048576 || addr < 0)
  {
    return -1;
  }
  // if end endress to be read is greater than 1MB, the operation will fail
  else if ((addr + len) > 1048576)
  {
    return -1;
  }
  // if len is not 0 but the buf is null, the operation will fail
  else if (len != 0 && buf == NULL)
  {
    return -1;
  }
  // if len is 0 and the buf is null, the operation will succeed
  else if (len == 0 && buf == NULL)
  {
    return 0;
  }
  // if no error, we proceed with reading the content from the blocks/disks
  else
  {
    size = 0;
    bytes_read = 0;

    starting_point = addr % 256;
    diff = 256 - starting_point;

    disk_id = addr / 65536; // per disk = 65536 byte
    block_id = get_block_id(addr, disk_id);

    while (len > 0)
    {
      if (len > 0)
      {
        if (len >= diff) // checking whether the len provided is greater or equal to the bytes that should be read
        {
          size = diff; // checking whether the len provided is less than the bytes that should be read
        }
        else
        {
          size = len;
        }
        // if cache is enabled, we check whether the entry is present in the cache
        if (cache_enabled()){
          if (cache_lookup(disk_id, block_id, temp_buf1) == 1){
            // if the entry is in the cache, we copy the content to the buf
            memcpy(buf + bytes_read, temp_buf1, size);
          }
          else{
            /* if the entry is not in the cache, we seek to the disk and block,
            read the block, and insert the entry to the cache for future use */

            // seeking to the specific disk
            command = JBOD_SEEK_TO_DISK;
            op = (command << 26) | (disk_id << 22);
            jbod_client_operation(op, NULL);

            // seeking to the specific block
            command = JBOD_SEEK_TO_BLOCK;
            op = (command << 26) | (block_id << 0);
            jbod_client_operation(op, NULL);

            command = JBOD_READ_BLOCK; // set command to read block
            op = (command << 26);
            jbod_client_operation(op, temp_buf1); 
            cache_insert(disk_id, block_id, temp_buf1);
          }
        }

        // if cache is not enabled, we simply seek to disk and block, and read the content to the buf
        else{
          // seeking to the specific disk
          command = JBOD_SEEK_TO_DISK;
          op = (command << 26) | (disk_id << 22);
          jbod_client_operation(op, NULL);

          // seeking to the specific block
          command = JBOD_SEEK_TO_BLOCK;
          op = (command << 26) | (block_id << 0);
          jbod_client_operation(op, NULL);

          command = JBOD_READ_BLOCK; // set command to read block
          op = (command << 26);
          jbod_client_operation(op, temp_buf1); // reads the block from the memory and copies it to the temporary buffer
          memcpy(buf + bytes_read, temp_buf1, size); // copies the content from the buffer to the buf provided 
        } 
      }
      addr += size; // pointing to the new address after reading the block
      starting_point = addr % 256;
      diff = 256 - starting_point;
      bytes_read += size; // total bytes read is incremented to the bytes that have been copied already
      len -= size; // the len argument is decremented as some bytes have been read already
      size = 0; // size is emptied to make sure it's copying the right data
      disk_id = addr / 65536; // getting the new diskID
      block_id = get_block_id(addr, disk_id); // getting the new blockID
    }
    len = bytes_read; // after the operation is completed, the len is equal to the total bytes read
  }
  return len;
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf)
{
  int block_id;
  int disk_id;
  int size;
  int bytes_read; // total bytes already read
  int diff; // the bytes that should be read
  int starting_point; // the point from where reading starts

  uint8_t temp_buf[256]; // temporary buffer where the contents from the block is added

  jbod_cmd_t command;
  uint32_t op;

  /* if the disks are unmounted, the operation will fail */
  if (error == 3)
  {
    return -1;
  }
  // if len is greater tha 1024, the operation will fail
  else if (len > 1024)
  {
    return -1;
  }
  /* if address is greater than 1MB or address is negative the operation will fail */
  else if (addr > 1048576 || addr < 0)
  {
    return -1;
  }
  // if end endress to be read is greater than 1MB, the operation will fail
  else if ((addr + len) > 1048576)
  {
    return -1;
  }
  // if len is not 0 but the buf is null, the operation will fail
  else if (len != 0 && buf == NULL)
  {
    return -1;
  }
  // if len is 0 and the buf is null, the operation will succeed
  else if (len == 0 && buf == NULL)
  {
    return 0;
  }
  else
  {
    size = 0;
    bytes_read = 0;

    disk_id = addr / 65536; // per disk = 65536 byte
    block_id = get_block_id(addr, disk_id);

    starting_point = addr % 256;
    diff = 256 - starting_point;

    while (len > 0)
    {      
      // seeking to the specific disk
      command = JBOD_SEEK_TO_DISK;
      op = (command << 26) | (disk_id << 22);
      jbod_client_operation(op, NULL);

      // seeking to the specific block
      command = JBOD_SEEK_TO_BLOCK;
      op = (command << 26) | (block_id << 0);
      jbod_client_operation(op, NULL);

      if (len > 0)
      {
        if (len >= diff) // checking whether the len provided is greater or equal to the bytes that should be read
        {
          size = diff;
        }
        else{
          size = len; // checking whether the len provided is less than the bytes that should be read
        }

        // checks whether cache is enabled or not
        if (cache_enabled()){
          if (cache_lookup(disk_id, block_id,temp_buf) == 1){
            /* if the entry is present in the cache, we copy the content from the block to the buf;
            then update the entry with the new buf provided and write that content */

            memcpy(temp_buf + starting_point, buf + bytes_read, size);
            cache_update(disk_id, block_id, temp_buf);
            command = JBOD_WRITE_BLOCK;
            op = (command << 26);
            jbod_client_operation(op, temp_buf);
          }
          else{
            /* if the entry is not present, we read the block, seek back to the block and 
            write the content to the temp_buf */

            command = JBOD_READ_BLOCK; // set command to read block
            op = (command << 26);
            jbod_client_operation(op, temp_buf); // reads the block from the memory and copies it to the temporary buffer
            memcpy(temp_buf + starting_point, buf + bytes_read, size); // copies the content from the buffer to the buf provided

            // seeks to the previous block as the read operation above moves to next block automatically
            command = JBOD_SEEK_TO_BLOCK;
            op = (command << 26) | (block_id << 0);
            jbod_client_operation(op, NULL);

            // writes the content from the temporary buffer to the memory
            command = JBOD_WRITE_BLOCK;
            op = (command << 26);
            jbod_client_operation(op, temp_buf); 
            cache_insert(disk_id, block_id, temp_buf);            
          }
        }
        else{
          /* if cache is not enabled, we simple read the block, seek back to the block,
          and write the content to the temp_buf */

          command = JBOD_READ_BLOCK; // set command to read block
          op = (command << 26);
          jbod_client_operation(op, temp_buf); // reads the block from the memory and copies it to the temporary buffer
          memcpy(temp_buf + starting_point, buf + bytes_read, size); // copies the content from the buffer to the buf provided

          // seeks to the previous block as the read operation above moves to next block automatically
          command = JBOD_SEEK_TO_BLOCK;
          op = (command << 26) | (block_id << 0);
          jbod_client_operation(op, NULL);

          // writes the content from the temporary buffer to the memory
          command = JBOD_WRITE_BLOCK;
          op = (command << 26);
          jbod_client_operation(op, temp_buf);
        }
      }
      addr += size; // pointing to the new address after reading the block
      starting_point = addr % 256;
      diff = 256 - starting_point;
      bytes_read += size; // total bytes read is incremented to the bytes that have been copied already
      len -= size; // the len argument is decremented as some bytes have been read already
      size = 0; // size is emptied to make sure it's copying the right data
      disk_id = addr / 65536; // getting the new diskID
      block_id = get_block_id(addr, disk_id); // getting the new blockID
    }
    len = bytes_read; // after the operation is completed, the len is equal to the total bytes read
  }
  return len;
}
