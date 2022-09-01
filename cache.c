#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int starting = 0; // the starting position in the cache
int cache_items = 0; // total number of items in the cache

bool cache_present = false; // checks whether cache is already created or not

// sets the default value of each entry's disk and block 
void set_initial(){
    int i;
    for (i = 0; i < cache_size; i++){
    cache[i].disk_num = -1;
    cache[i].block_num = -1;
    }
}

int cache_create(int num_entries) {
  if (num_entries < 2 || num_entries > 4096){
    return -1;
  }
  if (cache_present == true){ // if cache already created, returns -1
    return -1;
  }
  else{
    cache = (cache_entry_t *) malloc(num_entries * sizeof(cache_entry_t)); // dynamically allocate space
    cache_size = num_entries;
    cache_present = true; // cache is created
    set_initial(); // sets the initial value to -1
    return 1;
  }
}

int cache_destroy(void) {
  if (cache_present == false){ // if cache not created, returns -1
    return -1;
  }
  else{
    free(cache);
    cache = NULL;
    cache_size = 0;
    starting = 0;
    cache_items = 0;
    clock = 0;
    cache_present = false;
    return 1;
  }
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {

  num_queries = num_queries + 1; // increments the num_queries whenever this function is called
 
  if (buf == NULL){
    return -1;
  }
  else if (cache_present == false || cache_size == 0){
    return -1;
  }
  else if (cache_items == 0){
    return -1;
  }
  else{
    int i;
    int success; // the final output

    /* looping through the cache and checking whether the entry is present or not*/
    for (i = 0; i < cache_size; i++){
      if (cache[i].disk_num == disk_num && cache[i].block_num == block_num){
        /* if the entry is present, num_hit and clock is incremented;
        the access time of that entry is updated; block copied to buf */
        num_hits = num_hits + 1;
        clock = clock + 1;
        cache[i].access_time = clock;
        memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
        success = 1;
        break;
      }
      else{
        success = -1;
      }
    }
    return success;
  }
}
void cache_update(int disk_num, int block_num, const uint8_t *buf) {

  int i;
  clock = clock + 1; // increments the clock whenever this function is called
  num_queries = num_queries + 1; // increments the num_queries whenever this function is called

/* looping through the cache and checking whether the entry is present or not*/
  for (i = 0; i < cache_size; i++){
    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num){
      /* if the entry is present the access time of that entry is updated; 
      buf copied to the block of that entry */
      cache[i].access_time = clock;
      num_hits = num_hits + 1;
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      break;
    }
  }
}

// returns the entry with the lowest access time
int smallest_access_time()
{
  int i;
  num_queries = num_queries + 1;

  int small_access_time = cache[0].access_time;

  for (i = 1; i < cache_size; i++){
      if (cache[i].access_time < small_access_time){
          small_access_time = cache[i].access_time;
      }
  }
  return small_access_time;   
}

// checks whether the entry is in the cache or not
bool entry_present(int disk, int block){
  int i;
  num_queries = num_queries + 1;

  for (i = 0; i < cache_size; i++){
    if (cache[i].disk_num == disk && cache[i].block_num == block){
      num_hits = num_hits + 1;
      return true;
      break;
    }
  }
  return false;
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {

  num_queries = num_queries + 1; // increments the num_queries whenever this function is called

  if (cache_present == false){ 
    return -1;
  }
  else if (buf == NULL){
    return -1;
  }
  else if (disk_num < 0 || disk_num > 15){
    return -1;
  }
  else if (block_num < 0 || block_num > 255){
    return -1;
  }
  else if (entry_present(disk_num, block_num) == true){
    return -1;
  }
  else{
    int success;
    int lowest_access_time;

    cache_entry_t new_entry; // new entry of type 'cache_entry_t'
    clock = clock + 1; // increments the clock 
    new_entry.disk_num = disk_num; // the disk is set to the disk_num provided
    new_entry.block_num = block_num; // the block is set to the block_num provided
    memcpy(new_entry.block, buf, JBOD_BLOCK_SIZE); // copies the buf to the block of that entry
    new_entry.access_time = clock;  // the access time is set to the value of clock

    if (starting < cache_size){ 
      /* if the cache is not full, inserts the entry to next available spot */
      cache[starting] = new_entry;
      starting = starting + 1; // increments the current position
      cache_items += 1; // items present is icremented
      success = 1;
    }
    // if the cache is full, inserts entry using LRU
    else{
      int i;
      lowest_access_time = smallest_access_time(); // gets the entry with the lowest access time      
      for (i = 0; i < cache_size; i++){
        /* if the cache entry's lowest time equal to the lowest accee time,
        the new entry is inserted at that position */
        if (cache[i].access_time == lowest_access_time){
          cache[i] = new_entry;
        }
      }
      success = 1;
    }
    return success;
  }
}

// if cache is present/created, the cache is enabled
bool cache_enabled(void) {
  if (cache_size > 2){
    return true;
  }
  else if (cache_present == true){
      return true;
  }
  else{
      return false;
  }
}



void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
