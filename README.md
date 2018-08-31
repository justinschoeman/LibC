# Simple Replacement Symbols For LIBC Library #

Initial commit - USE AT YOUR OWN RISK!

## Supported hardware ##

Built and tested for STM32F103CB (blue pill board), but should work on just 
about any platform.

## Use ##

Just include the following line:

#include <LibC.h>

in your main .ino file.

If you wish to use a static heap allocation for dynamic memory, then include
the following too:

#define LIBC_HEAP_SIZE 10000 // 10,000 byte heap
#include <LibC_heap.h>

## Symbols Replaced ##

### exit.c ###

Since main() cannot exit in Arduino, replace all the following libc symbols 
with dummies:

atexit
__cxa_atexit
exit

Saves a bit of code and RAM space in many situations.

NOTE: It is technically possible to declare a class destructor in a static
class. If you explicitly call exit() then this destructor would normally be
called before stopping the CPU.  This will not happen anymore with this
library - the CPU will stop immediately. (This is the same functionality as
the AVR Arduino cores, so should not be an issue.)

### malloc.c ###

Replace all the malloc/free family of functions with my own implementation.

Implementation priorities:
1) Compact memory use
2) Reduce fragmentation
3) Small code
4) Efficient code

So it is slow, and should not be used if you rely on rapid and frequent
dynamic memory allocations.

New symbol:

void * crealloc(void *)

This reallocates the same size data (freeing the old, if moved). It is
guaranteed not to fail, so it will always return an address, and that new
addresss must always be used to access the data. This is used to 'bubble
down' allocations, so that allocated data moves down whereever possible, and
free space moves up/aggregates.  Call this frequently on persistent
allocations, where practical, and it will reduce memory fragmentation.

eg:

ptr = crealloc(ptr);

Will update ptr to the lowest available position in the heap, preserving
data and size.

Replaced sumbols:

realloc
malloc
free
calloc
malloc_usable_size
reallocarray (NOTE: equivallent to realloc - does NOT check size overflow!!)

The following symbols are not implemented and are replaced with functions
which produce an instant memory fault.

posix_memalign
aligned_alloc
valloc
memalign
pvalloc

