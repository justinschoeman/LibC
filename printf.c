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

/*
    NOTES
    
    1. Output failures are not tracked/counted. Return value is the number of characters 
    that _should have_ been printed.
    
    2. ONLY supports 8 bit width and precision
    
*/

#include <stdio.h>
#include <stdarg.h>


/*
    Compile as follows to test...
    gcc -DTEST -g -Wall -o printf printf.c
*/


#ifdef TEST
#include <assert.h>
#define FNPRE(x) tst_ ## x
#define TESTFN(x) x
// need prototypes for test variants
int FNPRE(printf)(const char *format, ...) __attribute__ ((format (printf, 1, 2)));
int FNPRE(sprintf)(char *str, const char *format, ...) __attribute__ ((format (printf, 2, 3)));
int FNPRE(snprintf)(char *str, size_t size, const char *format, ...) __attribute__ ((format (printf, 3, 4)));
int FNPRE(vprintf)(const char *format, va_list ap);
int FNPRE(vsprintf)(char *str, const char *format, va_list ap);
int FNPRE(vsnprintf)(char *str, size_t size, const char *format, va_list ap);
int FNPRE(puts)(const char *s);
#else
#define FNPRE(x) x
#define TESTFN(x) 
#endif

static void _putc(char **pptr, size_t *pnbr, int c) {
    if(pptr) {
        if(pnbr == (size_t*)-1) *(int*)0 = 0; // crashme - this is the magic hook for pprint functions
        if(pnbr) {
            if(*pnbr <= 0) return; // buffer full
            if(*pnbr == 1) c = 0; // always null terminate the string
            (*pnbr)--; // use up one char
        }
        **pptr = c;
        (*pptr)++;
    } else {
        putchar(c);
    }
}

static int _vprintf(char **pptr, size_t *pnbr, const char *format, va_list ap) {
    while(*format) _putc(pptr, pnbr, *(format++));
    if(pptr && (pnbr != (size_t*)-1)) _putc(pptr, pnbr, 0); // null terminate string output
    return 0;
}

int FNPRE(printf)(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = _vprintf(NULL, NULL, format, args);
    va_end(args);
    return ret;
}

int FNPRE(puts)(const char *s) {
    while(*s) _putc(NULL, NULL, *(s++));
    _putc(NULL, NULL, '\n');
    return 1;
}

int FNPRE(sprintf)(char *str, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = _vprintf(&str, NULL, format, args);
    va_end(args);
    return ret;
}

int FNPRE(snprintf)(char *str, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = _vprintf(&str, &size, format, args);
    va_end(args);
    return ret;
}

#ifdef TEST
int main(void) {
    char buf[1000];
    int i;
    
    // basic puts
    puts("puts");
    tst_puts("tst_puts");
    // basic printf
    printf("printf foo '%s' %d\n", "bar", 99);
    tst_printf("tst_printf foo '%s' %d\n", "bar", 99);
    
    // when format is invalid, output % and all further chars (including the one that made it invalid) - do not consume arg
    printf("printf foo '%s' '%llld'  '%d' xxx\n", "bar", 99, 88);

#define TPRINT(x, ...) \
    printf("TEST STRING '%s':\n", "x"); \
    i = snprintf(buf, sizeof(buf), x, ##__VA_ARGS__); \
    printf("libc (%d): '%s'\n", i, buf); \
    i = tst_snprintf(buf, sizeof(buf), x, ##__VA_ARGS__); \
    printf("tst (%d): '%s'\n", i, buf);
    
    TPRINT("foo %d", 19)
    
    return 0;
}
#endif
