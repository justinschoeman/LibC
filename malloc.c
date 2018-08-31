/*
 * Copyright 2018 Justin Schoeman
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this 
 * software and associated documentation files (the "Software"), to deal in the Software 
 * without restriction, including without limitation the rights to use, copy, modify, 
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to 
 * permit persons to whom the Software is furnished to do so, subject to the following 
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies 
 * or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A 
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION 
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>


/*
    Compile as follows to test...
    gcc -DTEST -g -Wall -o malloc malloc.c
*/


#ifdef TEST
#include <assert.h>
#define FNPRE(x) tst_ ## x
#define MEMSZ 15000
char mem[MEMSZ];
int pofs = 0;
void *tst_sbrk(intptr_t increment) {
    int newofs = pofs + increment;
    if(newofs < 0 || newofs > MEMSZ) {
        fprintf(stderr, "FAIL (%d): targ %d max %d\n", (int)increment, newofs, MEMSZ);
        errno = ENOMEM;
        return (void *)-1;
    }
    void * ret = mem + pofs;
    fprintf(stderr, "OK (%d): old: %d new: %d (%p)\n", (int)increment, pofs, newofs, ret);
    pofs = newofs;
    return ret;
}
#define TESTFN(x) x
#else
#define FNPRE(x) x
#define TESTFN(x) 
#endif

/*
    we assume we are the only dynamic allocator on the system - if
    something fragments our space, then die...
*/
void * __attribute__((weak)) safe_sbrk(int size) {
    static void * next_brk = NULL;
    void * ret = FNPRE(sbrk)(size);
    if(ret == (void*)-1) {
        TESTFN(fprintf(stderr, "SBRK FAILED %p %d\n", next_brk, size);)
        if(size <= 0) {
            // if decrement fails, stack is trashed
            TESTFN(fprintf(stderr, "SBRK DECREMENT FAILED %p %d\n", next_brk, size);)
            *(int*)0 = 0;
        }
    } else {
        if(next_brk && ret != next_brk) {
            TESTFN(fprintf(stderr, "SBRK MISMATCH %p %p\n", next_brk, ret);)
            *(int*)0 = 0;
        }
        next_brk = (char *)ret + size; // expect next brk to be directly after the space we just allocated
    }
    return ret;
}

/*
    header is precisely 32 bits
    23..0 is allocation size
    25..24 is extra size
    ...
    29..26 guard bits in test mode
    ...
    30 is empy flag
    31 is end flag
    
    single linked list
*/
typedef uint32_t hdr_t;
#define HDR_SIZE_MASK  		0x00ffffffU
#define HDR_END_MASK 		0x80000000U
#define HDR_FREE_MASK 		0x40000000U
#define HDR_GUARD_MASK		0x3c000000U
#define HDR_GUARD_VAL		0x14000000U
#define HDR_PAD_MASK		0x03000000U
#define HDR_PAD_SHIFT		24
static inline size_t hdr_size(hdr_t * h) { return *h & HDR_SIZE_MASK; }
static inline size_t hdr_pad_size(hdr_t * h) { return (*h & HDR_PAD_MASK) >> HDR_PAD_SHIFT; }
static inline size_t hdr_data_size(hdr_t * h) { return (*h & HDR_SIZE_MASK) - hdr_pad_size(h); }
static inline int hdr_free(hdr_t * h) { return *h & HDR_FREE_MASK; }
static inline int hdr_end(hdr_t * h) { return *h & HDR_END_MASK; }
static inline void * hdr_data(hdr_t * h) { return (void*)((char*)h + sizeof(hdr_t)); }
static inline hdr_t * hdr_hdr(void * d) { return (hdr_t*)((char*)d - sizeof(hdr_t)); }
static inline hdr_t * hdr_next(hdr_t * h) { return (hdr_t *)((char*)h + hdr_size(h) + sizeof(hdr_t)); }
static inline int hdr_check_guard(hdr_t * h) { return (*h & HDR_GUARD_MASK) == HDR_GUARD_VAL;}

static inline void hdr_clear_free(hdr_t * h) { *h &= ~HDR_FREE_MASK; }
static inline void hdr_set_free(hdr_t * h) { *h |= HDR_FREE_MASK; *h &= ~HDR_PAD_MASK; } // sanity check - just clear padding when free...
static inline void hdr_clear_end(hdr_t * h) { *h &= ~HDR_END_MASK; }
static inline void hdr_set_end(hdr_t * h) { *h |= HDR_END_MASK; }

/* reprocess a header - if required, split excess space into an empty header */
static void hdr_split(hdr_t * h, size_t size) {
    TESTFN(fprintf(stderr, "SPLIT: %08X %p %d %d %zu TO %zu\n", *h, h, hdr_end(h)?1:0, hdr_free(h)?1:0, hdr_size(h), size);)
    // sanity check - consider removing in production
    if(hdr_free(h) || size > hdr_size(h) || sizeof(hdr_t) != 4) {
        TESTFN(fprintf(stderr, "HDR_SPLIT INVALID HEADER %08X %zu %zu %zu\n", *h, size, hdr_size(h), sizeof(hdr_t));)
        *(int*)0 = 0;
    }
    // set padding to zero - will either stay 0 if split, or be updated if not
    *h &= ~HDR_PAD_MASK;
    // how many extra bytes are there?
    // hardcoded to 4 byte header (sanity check above) because we will have a 2 bit pad size
    int extra = hdr_size(h) - size;
    if(extra >= sizeof(hdr_t)) {
        *h -= extra;
        extra -= sizeof(hdr_t);
        hdr_t * newhdr = (hdr_t*)((char*)h + sizeof(hdr_t) + size);
        *newhdr = extra | HDR_FREE_MASK | HDR_GUARD_VAL; // basic flags
        // move end, if required
        if(hdr_end(h)) {
            hdr_clear_end(h);
            hdr_set_end(newhdr);
        }
    } else {
        // set pad (hardcoded to 4 byte header - sanity check above)
        *h |= (hdr_t)extra << HDR_PAD_SHIFT;
    }
}

static hdr_t * base = NULL;
void *FNPRE(realloc)(void *ptr, size_t size) {
    // special case
    if(!ptr && !size) return NULL; // null ptr, 0 size = return NULL (free of 0 = noop, malloc of 0 = optional null ret)
    // sanity check 
    if(size > HDR_SIZE_MASK/2) {
        TESTFN(fprintf(stderr, "OOM(pretest) %zu\n", size);)
        errno = ENOMEM;
        return NULL;
    }
    // special case - no chain yet?
    if(!base) {
        if(ptr) {
            // trying to free/realloc a pointer without a chain...
            TESTFN(fprintf(stderr, "FREE/REALLOC PTR %p W/O BASE\n", ptr);)
            *(int*)0 = 0;
        }
        // if we reach this point, ptr is NULL (new alloc), and size is non-zero
        void * newbase = safe_sbrk(size + sizeof(hdr_t));
        if(newbase == (void *)-1) {
            TESTFN(fprintf(stderr, "OOM %zu\n", size);)
            return NULL;
        }
        base = (hdr_t*)newbase;
        // set size, not empty, and chain end...
        *base = size | HDR_END_MASK | HDR_GUARD_VAL;
        return hdr_data(base);
    }
    // walk the chain
    // locate ptr and previous link to ptr
    // merge free space as we go
    // if size is non-zero, track first free space big enough for size...
    hdr_t * tailhdr = base;
    hdr_t * ptrhdr = NULL;
    hdr_t * freehdr = NULL;
    hdr_t * prevhdr = NULL;
    hdr_t * nexthdr;
    for(;;) {
        TESTFN(fprintf(stderr, "ITER: %08X %p %d %d %zu\n", *tailhdr, tailhdr, hdr_end(tailhdr)?1:0, hdr_free(tailhdr)?1:0, hdr_size(tailhdr));)
        // check guard - possibly remove from production
        if(!hdr_check_guard(tailhdr)) {
            TESTFN(fprintf(stderr, "HEADER GUARD FAIL AT %p (0x%x)\n", tailhdr, *tailhdr);)
            *(int*)0 = 0;
        }
        // if we are searching for a header, have we found it?
        if(ptr && hdr_data(tailhdr) == ptr) {
            ptrhdr = tailhdr;
        }
        // we are searching for free space, and this is free space?
        if(size && hdr_free(tailhdr) && hdr_size(tailhdr) >= size) {
            // special case - if it is an exact size match, and lower in heap, force use
            // was going to allow a 3 byte margin, but pathalogic cases would result in a proliferation of pads...
            if(hdr_size(tailhdr) == size) {
                // exact match
                if(ptr) {
                    if(!ptrhdr) freehdr = tailhdr; // if we are searching for a block but have not found it yet, then we are lower on the heap
                } else {
                    freehdr = tailhdr; // just looking for free space - anything on the heap is lower than the end ;)
                }
            }
            // otherwise, if we have nothing yet, this is the best option...
            // was going to abort at this point to save computation, but better to finish the walk for consistent state
            if(!freehdr) freehdr = tailhdr;
        }
        // end of chain?
        if(hdr_end(tailhdr)) break;
        // not end of chain - have a look at the next one...
        nexthdr = hdr_next(tailhdr);
        // aggregate/iterate
        // this one free, and next one free? merge
        if(hdr_free(tailhdr) && hdr_free(nexthdr)) {
            TESTFN(fprintf(stderr, "MERGE %p and %p\n", tailhdr, nexthdr);)
            *nexthdr += sizeof(hdr_t) + hdr_size(tailhdr); // keep nexthdr flags, but increase size to combined size
            *tailhdr = *nexthdr; // and make this our header
            continue; // for code efficiency, just rerun entire loop
        }
        if(!ptrhdr) prevhdr = tailhdr; // if we haven't yet found ptr header, track prev - this must point to one before ptrhdr
        tailhdr = nexthdr;
    }
    // only exit at end of chain, so tailhdr is the last element
    // evaluate results
    // ptr found - validate
    if(ptr) {
        if(!ptrhdr || hdr_free(ptrhdr)) {
            TESTFN(fprintf(stderr, "FREE/REALLOC FREED/NOTFOUND POINTER %p (0x%x)\n", ptrhdr, ptrhdr ? *ptrhdr : 0);)
            *(int*)0 = 0;
        }
        if(!size) {
            // if we found our target and we are freeing it, mark it free and return
            TESTFN(fprintf(stderr, "FREE POINTER %p (0x%x)\n", ptrhdr, *ptrhdr);)
            hdr_set_free(ptrhdr);
            ptrhdr = NULL;
            goto done;
        }
        // all free() cases now handled - rest are realloc
        // handle special-case realloc's here...
        // special case - if we are searching for a pointer (realloc), then check possible matches either side for
        // required space (note: if any lower header was big enough we will move it there - this is just if we
        // need to try grow the current pointer up or down...
        // This is ludicrously expensive given how infrequently it is used - reevaluate!
        if(!freehdr || freehdr > ptrhdr) {
            // realloc,  did not find existing free space, or it would result in moving up the heap
            // see if we can grow the existing block up and/or down and/or extend
            size_t freesize = hdr_size(ptrhdr); // existing block can always be reused...
            if(prevhdr && hdr_free(prevhdr)) {
                // if prevhdr is free, we will always use it too
                freesize += hdr_size(prevhdr) + sizeof(hdr_t); // only one header in final block
            } else {
                prevhdr = NULL; // not using prevhdr
            }
            nexthdr = hdr_next(ptrhdr);
            if(size > freesize && !hdr_end(ptrhdr) && hdr_free(nexthdr)) {
                // still need more space, and not end of chain, and next is empty
                freesize += hdr_size(nexthdr) + sizeof(hdr_t);
            } else {
                nexthdr = NULL; // not using nexthdr
            }
            // we tailtrim aggressively now, so the nexthdr check is not needed - but keeping it for safety for now
            //if(size > freesize && (hdr_end(ptrhdr) || (nexthdr && hdr_end(nexthdr)))) {
            if(size > freesize && hdr_end(ptrhdr)) {
                // still need more space, and one of these is the chain end - try to allocate more space
                TESTFN(fprintf(stderr, "REALLOC GROW %zu\n", size - freesize);)
                if(safe_sbrk(size - freesize) == (void *)-1) {
                    TESTFN(fprintf(stderr, "OOM(realloc) %zu\n", size - freesize);)
                    return NULL;
                }
                // got it and size is exact - grow tail data to accomodate new size
                // note - inefficient, as the final move will now move the extended data range...
                //*(nexthdr ? nexthdr : ptrhdr) += size - freesize;
                *ptrhdr += size - freesize;
                freesize = size;
            }
            // if after all of this, size is finally big enough, shuffel and recreate headers...
            if(size <= freesize) {
                TESTFN(fprintf(stderr, "REALLOC REUSE %p %p %p %zu %zu\n", prevhdr, ptrhdr, nexthdr, freesize, size);)
                // create new headers before moving data, as the move may overwrite intermediate headers
                // size is the total free size - set guard and copy end flag from highest included block
                // size already checked, so should be no overflow
                hdr_t tmphdr = freesize | HDR_GUARD_VAL;
                if(hdr_end(ptrhdr) || (nexthdr && hdr_end(nexthdr))) hdr_set_end(&tmphdr);
                if(prevhdr) {
                    // move down to prevhdr, if required
                    memmove(hdr_data(prevhdr), hdr_data(ptrhdr), hdr_size(ptrhdr));
                    // from now on we are working with a combined block from prevhdr
                    ptrhdr = prevhdr;
                }
                // set header on new combined block
                *ptrhdr = tmphdr;
                hdr_split(ptrhdr, size);
                goto done;
            }
        }
    }
    // all free use cases handled
    // all realloc-in-place use cases handled
    // now a simple alloc/realloc
    // UNUSED USE CASE - VERIFY AND REMOVE
    if(!freehdr && hdr_free(tailhdr)) {
        TESTFN(fprintf(stderr, "TAILHDR SHOULD NEVER BE FREE %p %08X\n", tailhdr, *tailhdr);)
        *(int*)0 = 0;
#if 0
        // no/insufficient freespace, and tail is empty
        int extra = size - hdr_size(tailhdr);
        // sanity check - consider removing in production
        if(extra <= 0) {
            TESTFN(fprintf(stderr, "FREEHDR SHOULD HAVE BEEN SET %zu %zu\n", size, hdr_size(tailhdr));)
            *(int*)0 = 0;
        }
        if(safe_sbrk(extra) == (void *)-1) {
            TESTFN(fprintf(stderr, "OOM(growfree) %d\n", extra);)
            return NULL;
        }
        *tailhdr += extra;
        freehdr = tailhdr;
#endif
    }
    if(!freehdr) {
        // allocate new space
        freehdr = safe_sbrk(size + sizeof(hdr_t));
        if(freehdr == (void *)-1) {
            TESTFN(fprintf(stderr, "OOM(new alloc) %zu\n", size);)
            return NULL;
        }
        // mark it as free for now
        *freehdr = size | HDR_END_MASK | HDR_FREE_MASK | HDR_GUARD_VAL;
        hdr_clear_end(tailhdr);
    }
    // finally, use freehdr...
    if(ptrhdr) {
        // realloc - move and free
        // non-overlapping, using memcopy
        memcpy(hdr_data(freehdr), hdr_data(ptrhdr), size < hdr_size(ptrhdr) ? size : hdr_size(ptrhdr));
        hdr_set_free(ptrhdr);
    }
    ptrhdr = freehdr;
    hdr_clear_free(ptrhdr);
    hdr_split(ptrhdr, size);
    // fall through to done...

done:
    // re-walk and trim tail at end... only
    tailhdr = base;
    prevhdr = NULL;
    for(;;) {
        TESTFN(fprintf(stderr, "REWALK: %08X %p %d %d %zu\n", *tailhdr, tailhdr, hdr_end(tailhdr)?1:0, hdr_free(tailhdr)?1:0, hdr_size(tailhdr));)
        // check guard - possibly remove from production
        if(!hdr_check_guard(tailhdr)) {
            TESTFN(fprintf(stderr, "REWALK HEADER GUARD FAIL AT %p (0x%x)\n", tailhdr, *tailhdr);)
            *(int*)0 = 0;
        }
        // end of chain?
        if(hdr_end(tailhdr)) break;
        // see if we can merge again?
        if(hdr_free(tailhdr) && hdr_free(hdr_next(tailhdr))) {
            TESTFN(fprintf(stderr, "REWALK MERGE: %08X %p %08X %p\n", *tailhdr, tailhdr, *(hdr_next(tailhdr)), hdr_next(tailhdr));)
            *(hdr_next(tailhdr)) += sizeof(hdr_t) + hdr_size(tailhdr); // keep all nextheaders flags, but increase size
            *tailhdr = *(hdr_next(tailhdr)); // then make it our header
            continue; // do not process this header yet - go back and check it again...
        }
        // not end of chain - have a look at the next one...
        prevhdr = tailhdr;
        tailhdr = hdr_next(tailhdr);
    }
    // sanity check - consider removing in production
    if(safe_sbrk(0) != hdr_next(tailhdr)) {
        TESTFN(fprintf(stderr, "REWALK END DOES NOT MATCH BRK %p %p\n", safe_sbrk(0), hdr_next(tailhdr));)
        *(int*)0 = 0;
    }
    // tail trim?
    if(hdr_free(tailhdr)) {
        TESTFN(fprintf(stderr, "TAIL TRIM %p %zu\n", tailhdr, hdr_size(tailhdr));)
        safe_sbrk(-(hdr_size(tailhdr) + sizeof(hdr_t)));
        if(prevhdr) {
            hdr_set_end(prevhdr);
        } else {
            // sanity check
            if(safe_sbrk(0) != base) {
                TESTFN(fprintf(stderr, "FINAL TRIM DOES NOT MATCH BASE %p %p\n", safe_sbrk(0), base);)
                *(int*)0 = 0;
            }
            base = NULL;
        }
    }
    TESTFN(fprintf(stderr, "RETURN: %p %p\n", ptrhdr, ptrhdr ? hdr_data(ptrhdr) : NULL);)
    return ptrhdr ? hdr_data(ptrhdr) : NULL;
}

// bubble down extension - realloc to existing size (possibly moving down the heap)
void *FNPRE(crealloc)(void *ptr) {
    hdr_t * hdr = hdr_hdr(ptr);
    if(!hdr_check_guard(hdr)) {
        TESTFN(fprintf(stderr, "CREALLOC HEADER GUARD FAIL AT %p (0x%x)\n", hdr, *hdr);)
        *(int*)0 = 0;
    }
    void * ret = FNPRE(realloc)(ptr, hdr_data_size(hdr));
    return ret ? ret : ptr;
}

// If ptr is NULL, then the call is equivalent to malloc(size), for all values of size
void *FNPRE(malloc)(size_t size) {
    return FNPRE(realloc)(NULL, size);
}

// if size is equal to zero, and ptr is not  NULL,  then  the  call  is  equivalent  to free(ptr).
void FNPRE(free)(void *ptr) {
    if(ptr) FNPRE(realloc)(ptr, 0);
}

void *FNPRE(calloc)(size_t nmemb, size_t size) {
    void * ret = FNPRE(realloc)(NULL, nmemb * size);
    if(ret && size > 0) bzero(ret, nmemb * size);
    return ret;
}

// non-standard, but useful - ASSUMES ptr is a valid malloc return!!!
size_t FNPRE(malloc_usable_size)(void *ptr) {
    if(!ptr) return 0;
    return hdr_data_size(hdr_hdr(ptr));
}

// Should actually be resistent to overflow in size, but 
// non-standard anyway, so just return something...
void *FNPRE(reallocarray)(void *ptr, size_t nmemb, size_t size) {
    return realloc(ptr, nmemb * size);
}

/* the following are rarely used and not implemented - make them fault immediately if actually used. */
int FNPRE(posix_memalign)(void **memptr, size_t alignment, size_t size) {
    return *(int*)0;
}

void *FNPRE(aligned_alloc)(size_t alignment, size_t size) {
    return *(void**)0;
}

void *FNPRE(valloc)(size_t size) {
    return *(void**)0;
}

void *FNPRE(memalign)(size_t alignment, size_t size) {
    return *(void**)0;
}

void *FNPRE(pvalloc)(size_t size) {
    return *(void**)0;
}

/* validate the malloc pool */
void mval(void) {
    if(!base) return;
    hdr_t * tailhdr = base;
    size_t tot = 0;
    size_t hed = 0;
    size_t pad = 0;
    size_t alc = 0;
    size_t fre = 0;
    int alcc = 0;
    int frec = 0;
    for(;;) {
        TESTFN(fprintf(stderr, "MVAL: %08X %p %d %d %zu %zu\n", *tailhdr, tailhdr, hdr_end(tailhdr)?1:0, hdr_free(tailhdr)?1:0, hdr_size(tailhdr), hdr_pad_size(tailhdr));)
        tot += sizeof(hdr_t) + hdr_size(tailhdr);
        hed += sizeof(hdr_t);
        if(hdr_free(tailhdr)) {
            fre += hdr_size(tailhdr);
            frec++;
        } else {
            pad += hdr_pad_size(tailhdr);
            alc += hdr_size(tailhdr);
            alcc++;
        }
        if(!hdr_check_guard(tailhdr)) {
            TESTFN(fprintf(stderr, "MVAL HEADER GUARD FAIL AT %p (0x%x)\n", tailhdr, *tailhdr);)
            *(int*)0 = 0;
        }
        // end of chain?
        if(hdr_end(tailhdr)) break;
        // not end of chain - have a look at the next one...
        tailhdr = hdr_next(tailhdr);
    }
    // sanity check - consider removing in production
    if(safe_sbrk(0) != hdr_next(tailhdr)) {
        TESTFN(fprintf(stderr, "MVAL END DOES NOT MATCH BRK %p %p\n", safe_sbrk(0), hdr_next(tailhdr));)
        *(int*)0 = 0;
    }
    size_t stot = (char*)safe_sbrk(0) - (char*)base;
    TESTFN(fprintf(stderr, "TOTAL: %zu (%zu) HEADERS: %zu PAD: %zu ALLOCATED: %zu FREE: %zu: ALLOC CNT: %d FREE CNT: %d\n", tot, stot, hed, pad, alc, fre, alcc, frec);)
    TESTFN(fprintf(stderr, "ALLOC %%: %.1f FREE %%: %.1f OVERHEAD %%: %.1f FRAG RATIO %%: %.1f\n", 100.0F*(float)alc/(float)tot, 100.0F*(float)fre/(float)tot, 100.0F*(float)(hed+pad)/(float)tot, 100.0F*(float)frec/(float)(frec+alcc));)
    if(stot != tot) {
        TESTFN(fprintf(stderr, "MVAL WALK TOT (%zu) DOES NOT MATCH HEAP TOT (%zu)\n", tot, stot);)
        *(int*)0 = 0;
    }
}
                                   

#ifdef TEST
#define BMAX 100
char * b[BMAX] = {0};
size_t bs[BMAX];

// malloc
void * bm(int i, size_t s) {
    fprintf(stderr, "***** MALLOC %d to %zu\n", i, s);
    assert(b[i] == 0);
    b[i] = tst_malloc(s);
    if(b[i]) {
        bs[i] = s;
        char * c = b[i];
        while(s-- > 0) *(c++) = (i & 0xff);
    }
    return b[i];
}

// free
void bf(int i) {
    fprintf(stderr, "***** FREE %d (%p) from %zu\n", i, b[i], bs[i]);
    assert(b[i]);
    char * c = b[i];
    int s = bs[i];
    while(s-- > 0) assert(*(c++) == (i & 0xff));
    
    tst_free(b[i]);
    b[i] = NULL;
}

// realloc
void * br(int i, size_t ns) {
    fprintf(stderr, "***** REALLOC %d (%p) from %zu to %zu\n", i, b[i], bs[i], ns);
    assert(b[i]);
    char * c = b[i];
    int s = bs[i];
    while(s-- > 0) assert(*(c++) == (i & 0xff));
    
    void * np = tst_realloc(b[i], ns);
    if(!np) {
        if(!ns) b[i] = NULL;
        return NULL;
    }
    
    c = np;
    s = ns < bs[i] ? ns : bs[i];
    while(s-- > 0) assert(*(c++) == (i & 0xff));

    c = np;
    s = ns;
    while(s-- > 0) *(c++) = (i & 0xff);
    
    b[i] = np;
    bs[i] = ns;
    return np;
}

// crealoc
void bc(int i) {
    fprintf(stderr, "***** CREALLOC %d (%p) from %zu\n", i, b[i], bs[i]);
    assert(b[i]);
    char * c = b[i];
    int s = bs[i];
    while(s-- > 0) assert(*(c++) == (i & 0xff));
    
    b[i] = tst_crealloc(b[i]);

    c = b[i];
    s = bs[i];
    while(s-- > 0) assert(*(c++) == (i & 0xff));
    //exit(0);
}

// reset - fee all allocated data and start clean
void rst(void) {
    int i;
    mval();
    for(i = 0; i < BMAX; i++) {
        if(b[i]) {
            hdr_t * hdr = hdr_hdr(b[i]);
            assert(bs[i] == hdr_data_size(hdr));
            bf(i);
        }
    }
    assert(base == NULL);
}

// write to every allocated memory location, then reread to make sure nothing was scribbled
void rewrite(void) {
    int i;
    char * c;
    int s;
    // read all values
    for(i = 0; i < BMAX; i++) {
        if(b[i]) {
            c = b[i];
            s = bs[i];
            while(s-- > 0) assert(*(c++) == (i & 0xff));
        }
    }
    // write all values
    for(i = 0; i < BMAX; i++) {
        if(b[i]) {
            c = b[i];
            s = bs[i];
            while(s-- > 0) *(c++) = (i & 0xff);
        }
    }
    // read all values
    for(i = 0; i < BMAX; i++) {
        if(b[i]) {
            assert(bs[i] == hdr_data_size(hdr_hdr(b[i])));
            assert(hdr_check_guard(hdr_hdr(b[i])));
            c = b[i];
            s = bs[i];
            while(s-- > 0) assert(*(c++) == (i & 0xff));
        }
    }
}

// stress test - perform random operations
void stress(int scount) {
    rst();
    srandom(0);
    while(scount-- > 0) {
        int i = random() % BMAX;
        if(b[i]) {
            int c = random() % 10;
            if(c < 2) {
                bc(i);
            } else if(c < 4) {
                bf(i);
            } else {
                int s = random() % 200;
                br(i, s);
            }
        } else {
            int c = random() % 10;
            if(c < 2) {
                int s = random() % 200;
                bm(i, s);
            }
        }
        mval();
        rewrite();
    }
}

// run crealloc on all data
void cmpct(void) {
    int i;
    for(i = 0; i < BMAX; i++) {
        if(b[i]) b[i] = tst_crealloc(b[i]);
    }
}

int main(void) {
    int i;
    
    assert(bm(0, 10) == (char*)base + 4);
    rst();
    //return 0;

    assert(bm(0, 10) == (char*)base + 4);
    assert(bm(1, 10) == (char*)base + 18);
    rst();

    assert(bm(0, 10) == (char*)base + 4);
    assert(bm(1, 10) == (char*)base + 18);
    assert(bm(2, 10) == (char*)base + 32);
    assert(bm(3, 10) == (char*)base + 46);
    bf(0);
    bf(2);
    bf(1);
    bf(3);
    assert(base == NULL);

    assert(bm(0,4) == (char*)base + 4);
    rst();

    assert(bm(0, 10) == (char*)base + 4);
    assert(bm(1, 10) == (char*)base + 18);
    assert(bm(2, 10) == (char*)base + 32);
    assert(bm(3, 10) == (char*)base + 46);
    bf(1);
    assert(bm(1, 4) == (char*)base + 18);
    bf(1);
    bf(2);
    assert(bm(1, 24) == (char*)base + 18);
    bf(1);
    assert(bm(1, 30) == (char*)base + 60);
    bf(0);
    bf(3);
    assert(bm(4, 60) == (char*)base + 94);
    assert(br(1,30) == (char*)base + 4);
    assert(br(1,29) == (char*)base + 4);
    assert(br(1,28) == (char*)base + 4);
    assert(br(1,27) == (char*)base + 4);
    assert(br(1,26) == (char*)base + 4);
    assert(br(1,25) == (char*)base + 4);
    rst();
    
    stress(1000);
    mval();
    return;
    
    for(i = 0; i < 100; i++) {
        cmpct();
        mval();
    }
    //rst();
    
    return 0;
}
#endif
