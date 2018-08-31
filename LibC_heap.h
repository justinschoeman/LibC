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

#ifndef _LIBC_HEAP_H_
#define _LIBC_HEAP_H

#ifndef LIBC_HEAP_SIZE
#error LIBC_HEAP_SIZE must be defined to the desired heap size in bytes
#endif

#include <errno.h>

static char _HEAP_MEM_[LIBC_HEAP_SIZE];

extern "C" void * safe_sbrk(int size) {
    static char * hptr = _HEAP_MEM_;
    char * newptr = hptr + size;
    
    if(hptr < _HEAP_MEM_ || hptr > (_HEAP_MEM_ + LIBC_HEAP_SIZE)) {
        // if decrement fails, stack is trashed
        if(size <= 0) *(int*)0 = 0;
        // if increment fails we are just out of memory
        errno = ENOMEM;
        return (void *)-1;
    }
    void * ret = (void*)hptr;
    hptr = newptr;
    return ret;
}

#endif
