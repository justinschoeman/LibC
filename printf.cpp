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

#include <Arduino.h>
#include <stdarg.h>
#include "local_printf.h"

Print * _printf_printer = NULL;

void printf_setprint(Print * p) {
    _printf_printer = p;
}

extern "C" void _printf_putchar(int c) {
    _printf_printer->print((char)c);
}

extern "C" int _vprintf(pint_t * pint, const char *format, va_list ap);

extern "C" void _pprintf_putchar(void * p, int c) {
    if(!p) return; // default, no output until printf_setprint is called...
    Print * pr = (Print*)p;
    pr->print((char)c);
}

int pprintf(Print& p, const char *format, ...) {
    va_list args;
    pint_t pint = {0};
    pint.sptr = (char*)&p;
    pint.scnt = (size_t)-1;
    va_start(args, format);
    int ret = _vprintf(&pint, format, args);
    va_end(args);
    return ret;
}
