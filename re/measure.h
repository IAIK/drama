#ifndef _TIMING_H_
#define _TIMING_H_

#include <stdint.h>
#include <unistd.h>
#include <assert.h>
#include <map>
#include <vector>
#include <list>

extern int verbosity;
// -----------------------------------

typedef uint64_t pointer;

typedef std::pair<pointer, pointer> addrpair;

#define logError(f, ...) do { printf("[%-9s] ", "ERROR"); printf(f, __VA_ARGS__); } while(0);
#define logWarning(f, ...) do { if(verbosity > 0) {printf("[%-9s] ", "WARNING"); printf(f, __VA_ARGS__);} } while(0);
#define logInfo(f, ...) do { if(verbosity > 1) {printf("[%-9s] ", "INFO"); printf(f, __VA_ARGS__); }} while(0);
#define logLog(f, ...) do { if(verbosity > 2) {printf("[%-9s] ", "LOG"); printf(f, __VA_ARGS__); }} while(0);
#define logDebug(f, ...) do { if(verbosity > 3) {printf("[%-9s] ", "DEBUG"); printf(f, __VA_ARGS__); }} while(0);

#define printBinary(x) do { std::bitset<sizeof(size_t) * 8> bs(x); std::cout << bs; } while(0);

#endif
