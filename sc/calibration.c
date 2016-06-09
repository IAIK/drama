#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#define MIN(X,Y) (((X) < (Y)) ? (X) : (Y))

uint64_t rdtsc() {
  uint64_t a, d;
  asm volatile ("mfence");
  asm volatile ("rdtsc" : "=a" (a), "=d" (d));
  a = (d<<32) | a;
  asm volatile ("mfence");
  return a;
}

void maccess(void* p)
{
  asm volatile ("movq (%0), %%rax\n"
    :
    : "c" (p)
    : "rax");
}

void flush(void* p) {
    asm volatile ("clflush 0(%0)\n"
      :
      : "c" (p)
      : "rax");
}

#define ARRAY_SIZE (16*1024*1024)
size_t array[ARRAY_SIZE] __attribute__((aligned(4096)));

size_t hit_histogram[200];
size_t miss_histogram[200];

#define assert(X) do { if (!(X)) { fprintf(stderr,"assertion '" #X "' failed\n"); exit(-1); } } while (0)
#define DIMMS (2)
int pagemap = -1;
// Extract the physical page number from a Linux /proc/PID/pagemap entry.
uint64_t frame_number_from_pagemap(uint64_t value) {
  return value & ((1ULL << 54) - 1);
}

size_t get_dram_row(void* phys_addr_p) {
  uint64_t phys_addr = (uint64_t) phys_addr_p;
  return phys_addr >> 18;
}

size_t get_dram_mapping(void* phys_addr_p) {
  uint64_t phys_addr = (uint64_t) phys_addr_p;
  static const size_t h0[] = { 14, 18 };
  static const size_t h1[] = { 15, 19 };
  static const size_t h2[] = { 16, 20 };
  static const size_t h3[] = { 17, 21 };
  static const size_t h4[] = { 7, 8, 9, 12, 13, 18, 19 };

  size_t count = sizeof(h0) / sizeof(h0[0]);
  size_t hash = 0;
  for (size_t i = 0; i < count; i++) {
    hash ^= (phys_addr >> h0[i]) & 1;
  }
  count = sizeof(h1) / sizeof(h1[0]);
  size_t hash1 = 0;
  for (size_t i = 0; i < count; i++) {
    hash1 ^= (phys_addr >> h1[i]) & 1;
  }
  count = sizeof(h2) / sizeof(h2[0]);
  size_t hash2 = 0;
  for (size_t i = 0; i < count; i++) {
    hash2 ^= (phys_addr >> h2[i]) & 1;
  }
  count = sizeof(h3) / sizeof(h3[0]);
  size_t hash3 = 0;
  for (size_t i = 0; i < count; i++) {
    hash3 ^= (phys_addr >> h3[i]) & 1;
  }
  count = sizeof(h4) / sizeof(h4[0]);
  size_t hash4 = 0;
  for (size_t i = 0; i < count; i++) {
    hash4 ^= (phys_addr >> h4[i]) & 1;
  }
  return (hash4 << 4) | (hash3 << 3) | (hash2 << 2) | (hash1 << 1) | hash;
}

uint64_t get_physical_addr(uint64_t virtual_addr) {
  uint64_t value;
  off_t offset = (virtual_addr / 4096) * sizeof(value);
  int got = pread(pagemap, &value, sizeof(value), offset);
  assert(got == 8);

  // Check the "page present" flag.
  assert(value & (1ULL << 63));

  uint64_t frame_num = frame_number_from_pagemap(value);
  return (frame_num * 4096) | (virtual_addr & (4095));
}

size_t onlyreload(void* addr) // row hit
{
  maccess(addr); // victim
  flush(addr); // victim
  size_t time = rdtsc();
  maccess(addr);
  size_t delta = rdtsc() - time;
  return delta;
}

size_t flushandreload(void* addr, void* addr2) // row miss
{
  flush(addr2); // victim
  maccess(addr2); // victim
  flush(addr2); // victim
  flush(addr); // victim
  maccess(addr2); // victim
  size_t time = rdtsc();
  maccess(addr);
  size_t delta = rdtsc() - time;
  flush(addr);
  return delta;
}

int main(int argc, char** argv)
{
  memset(array,-1,ARRAY_SIZE*sizeof(size_t));
  maccess(array + 20*1024);
  pagemap = open("/proc/self/pagemap", O_RDONLY);
  assert(pagemap >= 0);
  void* p = array+2*512+0xa00;
  void* p2 = 0;
  void* p3 = 0;
  size_t set = get_dram_mapping((void*)get_physical_addr((uint64_t)p));
  size_t row = get_dram_row((void*)get_physical_addr((uint64_t)p));
  printf("P:%p -> %zu,%zu\n",p,set,row);
  for (size_t i = 0; i < ARRAY_SIZE; i += 64/sizeof(array[0]))
    if (((array + i) != p) && get_dram_mapping((void*)get_physical_addr((uint64_t)(array + i))) == set && get_dram_row((void*)get_physical_addr((uint64_t)(array + i))) == row)
    {
      printf("P':%p -> %zu,%zu\n",(array + i),get_dram_mapping((void*)get_physical_addr((uint64_t)(array + i))),get_dram_row((void*)get_physical_addr((uint64_t)(array + i))));
      p2 = array + i;
      break;
    }
  if (p2 == 0)
    exit(printf("didn't find a second address mapping to the same row, please retry\n"));
  for (size_t i = 0; i < ARRAY_SIZE; i += 64/sizeof(array[0]))
    if (((array + i) != p) && ((array + i) != p2) && get_dram_mapping((void*)get_physical_addr((uint64_t)(array + i))) == set && get_dram_row((void*)get_physical_addr((uint64_t)(array + i))) != row)
    {
      printf("P'':%p -> %zu,%zu\n",(array + i),get_dram_mapping((void*)get_physical_addr((uint64_t)(array + i))),get_dram_row((void*)get_physical_addr((uint64_t)(array + i))));
      p3 = array + i;
      break;
    }
  if (p3 == 0)
    exit(8);
  sched_yield();
  for (int i = 0; i < 4*1024*1024; ++i)
  {
    size_t d = flushandreload(p,p2);
    hit_histogram[MIN(199,d/2)]++;
    sched_yield();
  }
  flush(array+1024);
  for (int i = 0; i < 4*1024*1024; ++i)
  {
    size_t d = flushandreload(p,p3);
    miss_histogram[MIN(199,d/2)]++;
    sched_yield();
  }
  printf("cycles     hits  conflicts\n");
  size_t hit_max = 0;
  size_t hit_max_i = 0;
  size_t miss_min_i = 0;
  for (int i = 0; i < 200; ++i)
  {
    printf("%3d: %10zu %10zu\n",i*2,hit_histogram[i],miss_histogram[i]);
  }
  return 0;
}
