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

#ifndef _LOCAL_PRINTF_H_
#define _LOCAL_PRINTF_H_

/* waayyyy too much state to pass around between functions - ends up using a lot of stack space pushing them 
   all the time - rather create one structure up front and use it everywhere... packed to keep it tight ARM
   has good byte access capability (even non-aligned) */
typedef struct {
    char * sptr; // pointer to (initial) start of output string (also mapped to Print pointer when scnt=-1
    size_t scnt; // initial number of free bytes in above string (set to intmax for effective unlimited)
    uint8_t radix;
    uint8_t flags;
    uint8_t width;
    uint8_t prec;
} __attribute__((packed)) pint_t;


#endif
