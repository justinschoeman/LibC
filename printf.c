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
    
    I have decided to cleanroom this, rather than reuse work which was partially done
    for customers. So starting from scratch, but there will probably be a lot of
    influence for Georges Mennie's wonderful implementation...
    
    1. Output failures are not tracked/counted. Return value is the number of characters 
    that _should have_ been printed.
    
    2. Does NOT handle *m$ style positional width/precision parameters!
    
    3. Limited precision - 64 bit ints are truncated to 32, doubles to floats
    
    4. Floats are cast back to int32 for rendering - if there are two many digits, zeros
    are appended (and possibly decimals are reduced) - PRECISION IS LOST ON LARGE FLOATS!
    
    5. targets 32 bit processors, so all modifiers which promote to int are ignored
    
    6. 'a' format is not implemented... (translated to 'g' radix 16)
    
    7. All 'capital letter' formats are handled, even if not valid (translated to lower
    case equivalent)
    
    8. m$ width/precision are parsed, but treated as unset...
    
    9. floating point precision is limited to 8 decimals
    
    10. 'g' is currently treated as 'e' need to clarify operation...
    
    11. width is limited to 255, precision to 254
    
*/

#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include "local_printf.h"

#define FLAG_ALT	0x00000001U
#define FLAG_0		0x00000002U
#define FLAG_MINUS	0x00000004U
#define FLAG_SPACE	0x00000008U
#define FLAG_PLUS	0x00000010U
#define FLAG_EXP	0x00000020U
#define FLAG_CAP	0x00000040U


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
// dummy for c test env
void _pprintf_putchar(void * p, int c) {
}
#else
#define FNPRE(x) x
#define TESTFN(x) 

#ifdef putchar
#undef putchar
#endif

// in production, override purchar with a call to our cpp symbol
extern void _printf_putchar(int c);
int putchar(int c) {
    _printf_putchar(c);
    return 1;
}

#endif




//output one chatacter at a time (whatever the destination...)
extern void _pprintf_putchar(void * p, int c);
static void _putc(pint_t * pint, int c) {
    if(pint->sptr) {
        if(pint->scnt == (size_t)-1) {
            _pprintf_putchar(pint->sptr, c);
            return;
        }
        if(pint->scnt == 0) return; // buffer full
        if(pint->scnt == 1) c = 0; // always null terminate the string
        pint->scnt--; // use up one char
        *(pint->sptr) = c;
        pint->sptr++;
    } else {
        putchar(c);
    }
}

// uses up n!
static int _padc(pint_t * pint, int c, int *n) {
    int ret = 0;
    while(*n > 0) {
        _putc(pint, c);
        (*n)--;
        ret++;
    }
    return ret;
}

// print an integer.  if dummy is true then only count the number of digits which would be output
// was trying to be 'eficient' by doing this without temp storage, but the cost in flash is way
// too high considering the stack saving.
// at worst 32 bit octal will be 11 digits, plus the one potential decimal (which can't happen with octal
// but is a potential input, so cater for it...
static int _puint(pint_t * pint, uint32_t num, int decs, int dummy) {
    char tmp[12];
    int ret = 0;
    
    for(;;) {
        // always output first digit
        tmp[ret++] = num % pint->radix;
        num /= pint->radix;
        if(!--decs) tmp[ret++] = '.' - '0'; // decs won't wrap back to zero again, so cheap test
        if(decs >= 0) continue; // if we have output a decimal, we must output at least one more
        if(!num) break;
    }
    if(dummy) return ret;
    // now spit em out in reverse order
    for(decs = ret - 1; decs >= 0; decs--) {
        tmp[decs] += '0';
        if(tmp[decs] > '9') tmp[decs] += pint->flags & FLAG_CAP ? 'A' - ':' : 'a' - ':';
        _putc(pint, tmp[decs]);
    }
    return ret;
}


static int _printf(pint_t * pint, float fnum, char fmt) {
    uint32_t num;
    int pad;
    int ret = 0;
    char padc = 0;
    int exp = 0;
    int zeros = 0;
    
    // strip off sign
    if(fnum < 0) {
        padc = '-';
        fnum = -fnum;
    } else {
        if(pint->flags & FLAG_SPACE) padc = ' ';
        if(pint->flags & FLAG_PLUS) padc = '+';
    }
    // seems to be a common definition for all sub formats
    if(pint->prec == 255) pint->prec = 6;

retry:    
    // float formats
    switch(fmt) {
        case 'f': {
            float tmpf = fnum;
            for(pad = 0; pad < pint->prec; pad++) tmpf *= pint->radix; // make decimals significant
            tmpf += 0.5; // round correctly
            // ugly - truncate if too long
            while(tmpf > (float)0xffffffff) {
                tmpf /= pint->radix;
                zeros++;
                if(pint->prec > 0) pint->prec--;
            }
            num = (uint32_t)tmpf;
            break;
        }
        
        case 'a': pint->radix = 16; // broken
        case 'g':
        case 'e': {
            float tmpf = pint->radix / 2;
            pint->flags |= FLAG_EXP;
            fmt = 'f'; // pre-process then send to the float parser
            if(fnum == 0) goto retry; // 0 number, 0 exponent, process as such
            if(pint->prec > 8) pint->prec = 8; // only have 9 usable digits...
            for(pad = 0; pad <= pint->prec; pad++) tmpf /= pint->radix;
            while((uint32_t)(fnum+tmpf) < 1) {
                fnum *= pint->radix;
                exp--;
            }
            while((uint32_t)(fnum+tmpf) >= pint->radix) {
                fnum /= pint->radix;
                exp++;
            }
            //fprintf(stderr, "%f %d\n", fnum, exp);
            goto retry;
        }
    }
    pad = _puint(pint, num, pint->prec, 1);
    if(pint->prec == 0 && pint->flags & FLAG_ALT) pad++; // include space for decimal point
    if(padc) pad++;
    if(zeros) pad += zeros;
    if(pint->flags & FLAG_EXP) pad += 4; // e+dd
    if(pint->width > 0 && pad < pint->width) {
        pad = pint->width - pad;
    } else {
        pad = 0;
    }
    if(!(pint->flags & FLAG_MINUS))
        ret += _padc(pint, ' ', &pad);
    if(padc) {
        _putc(pint, padc);
        ret++;
    }
    ret += _puint(pint, num, pint->prec, 0);
    ret += _padc(pint, '0', &zeros);
    if(pint->prec == 0 && pint->flags & FLAG_ALT) {
        _putc(pint, '.');
        ret++;
    }
    if(pint->flags & FLAG_EXP) {
        _putc(pint, fmt >= 'a' ? 'e' : 'E');
        _putc(pint, exp >= 0 ? '+' : '-');
        exp = exp < 0 ? -exp : exp;
        if(exp < pint->radix) _putc(pint, '0');
        _puint(pint, exp, 0, 0);
        
        ret+=4;
    }
    ret += _padc(pint, ' ', &pad);
    return ret;
}

static int _printi(pint_t * pint, unsigned int num, char fmt) {
    int pad;
    int ret = 0;
    int zeros = 0;
    char padc[2] = {0};
    
    if(num == 0 && pint->prec == 0) return 0; // special case - no output (only for decimals - process floats above...
    
    // decimal formats
    switch(fmt) {
        //case 'd': // translated to i before calling
        case 'i':
            if((int)num < 0) {
                num = -(int)num;
                padc[0] = '-';
            } else {
                if(pint->flags & FLAG_SPACE) padc[0] = ' ';
                if(pint->flags & FLAG_PLUS) padc[0] = '+';
            }
            break;
        
        case 'x':
            pint->radix = 16;
            if(pint->flags & FLAG_ALT) {
                padc[0] = '0';
                padc[1] = pint->flags & FLAG_CAP ? 'X' : 'x';
            }
            break;
        
        case 'o':
            pint->radix = 8;
            if(pint->flags & FLAG_ALT) padc[0] = '0'; // fixme - special case - suppress if 'zeros' is set
            break;
    }
    pad = _puint(pint, num, 0, 1);
    if(pint->prec != 255 && pad < pint->prec) {
        zeros = pint->prec - pad;
        pad = pint->prec;
    }
    if(zeros > 0 && fmt == 'o') padc[0] = 0; // no octal padding if we already insert leading zeros
    if(padc[0]) pad++;
    if(padc[1]) pad++;
    if(pint->width > 0 && pad < pint->width) {
        pad = pint->width - pad;
    } else {
        pad = 0;
    }
    if(pint->flags & FLAG_0 && pad > 0 && pint->prec != 255) {
        zeros += pad;
        pad = 0;
    }
    if(!(pint->flags & FLAG_MINUS))
        ret += _padc(pint, ' ', &pad);
    if(padc[0]) {
        _putc(pint, padc[0]);
        ret++;
    }
    if(padc[1]) {
        _putc(pint, padc[1]);
        ret++;
    }
    ret += _padc(pint, '0', &zeros);
    ret += _puint(pint, num, 0, 0);
    ret += _padc(pint, ' ', &pad);
    return ret;
}

static int _prints(pint_t * pint, char *s) {
    int pad = 0;
    int ret;
    if(!s) s = "(null)";
    if(pint->width > 0) {
        if(pint->prec != 255) {
            pad = strnlen(s, pint->prec);
        } else {
            pad = strlen(s);
        }
        if(pad < pint->width) {
            pad = pint->width - pad;
        } else {
            pad = 0;
        }
    }
    ret = pad;
    if(!(pint->flags & FLAG_MINUS)) _padc(pint, ' ', &pad);
    while(*s && pint->prec-- != 0) { // really long strings can make -ve prec overflow, but that should not be a problem ;)
        _putc(pint, *(s++));
        ret++;
    }
    _padc(pint, ' ', &pad);
    return ret;
}


// the real deal - this parses the format string and dishes out the individual formats
int _vprintf(pint_t * pint, const char *format, va_list ap) {
    int ret = 0;
    int mode = 0;
    char mod;
    char c;
    
    // going to do a direct phase for phase parser, each self contained...
    while((c = *(format++))) { // always consume the current character!
        //TESTFN(fprintf(stderr,"%d '%c'\n", mode, c);)
        
        // mode 0 - just spitting out characters...
        if(mode == 0) {
            if(c == '%') {
                mode = 1;
                pint->radix = 10; // default to 10
                pint->flags = 0;
                pint->width = 0;
                pint->prec = 255;
                mod = 0;
                continue;
            }
            // output current character
            _putc(pint, c);
            ret++;
            continue;
        }
        
        // mode 1 - flags
        if(mode == 1) {
            switch(c) {
                case '#': pint->flags |= FLAG_ALT; continue;
                case '0': pint->flags |= FLAG_0; continue;
                case '-': pint->flags |= FLAG_MINUS; continue;
                case ' ': pint->flags |= FLAG_SPACE; continue;
                case '+': pint->flags |= FLAG_PLUS; continue;
                case '\'':
                case 'I':
                    // don't handle these - but dont break format parser...
                    continue;
            }
            // not a flag
            mode = 2;
            // fall through and try width parser
        }
        
        // mode 2 - width (*)
        if(mode == 2) {
            if(c == '*') {
                // read width from next parm
                pint->width = va_arg(ap, int);
                if(pint->width < 0) {
                    pint->flags |= FLAG_MINUS;
                    pint->width = -pint->width;
                }
                mode = 4; // skip numeric width parser
                continue;
            }
            // not a *
            mode = 3;
            // fall through to read numeric width
        }
        
        // mode 3 - numeric width
        if(mode == 3) {
            if(c == '$') {
                // m$ format - treat as not set...
                pint->width = 0;
            } else if(isdigit(c)) {
                pint->width *= 10;
                pint->width += c - '0';
                continue;
            }
            mode = 4;
            // fall through and look for precision separator
        }
        
        // mode 4 - precision separator
        if(mode == 4) {
            if(c == '.') {
                mode = 5; // * precision
                continue;
            }
            mode = 7; // modifiers
        }

        // mode 5 - precision (*)
        if(mode == 5) {
            if(c == '*') {
                // read precision from next parm
                pint->prec = va_arg(ap, int);
                mode = 7; // skip numeric precision parser
                continue;
            }
            // not a *
            mode = 6;
            // very special case, any negative precision means no precision
            if(c == '-') {
                pint->prec = 255; // will always stay negative in numeric parser
                continue; // consume - sign
            }
            pint->prec = 0;
            // fall through to read numeric width
        }
        
        // mode 6 - numeric precision
        if(mode == 6) {
            if(c == '$') {
                // m$ format - treat as not set...
                pint->prec = 255;
            } else if(isdigit(c)) {
                pint->prec *= 10;
                pint->prec += c - '0';
                continue;
            }
            mode = 7;
            // fall through and look for modifier
        }
        
        // mode 7 - modifier
        if(mode == 7) {
            // single modifiers - always advance to conversion
            mode = 8;
            switch(c) {
                case 'h':
                    // both promote to int, so ignore modifier
                    //if(*format == 'h') {
                    //    // hh?
                    //    mod = 'H';
                    //    format++;
                    //} else mod = 'h';
                    continue;
                case 'l':
                    if(*format == 'l') {
                        // ll?
                        mod = 'q';
                        format++;
                    } else mod = 'l';
                    continue;
                case 'q':
                case 'L':
                case 'j':
                //case 'z': // promote to int, so ignore
                //case 't':
                    mod = c;
                    continue;
                case 'Z':
                    mod = 'z';
                    continue;
            }
            // else fall through
        }
        
        // mode 8 - conversion
        if(mode == 8) {
            // single conversions - always return to output
            mode = 0;
            char tmpc = c;
            if(c >= 'A' && c <= 'Z') {
                pint->flags |= FLAG_CAP;
                tmpc += 'a' - 'A';
            }
            switch(tmpc) {
                case 's': // S/mod = 'l'; // skip this as we aren't implementing it anyway...
                    // read both as chars, and trust the ouput function to interpret the charset correctly
                    ret += _prints(pint, va_arg(ap, char*));
                    continue;

                case 'c': // C
                    // read both as chars, and trust the ouput function to interpret the charset correctly
                    // parameters will be passed as full ints on all types anyway...
                    _putc(pint, va_arg(ap, int));
                    ret++;
                    continue;
                    
                case 'd': tmpc = 'i'; // elliminate duplicat check up top
                case 'i': 
                case 'o': 
                case 'u': 
                case 'x': {
                    // always read unsigned - if it was signed we will later strip the sign and conver to unsigned
                    unsigned int i;
                    switch(mod) {
                        case 'q': i = va_arg(ap, unsigned long long int); break;
                        case 'j': i = va_arg(ap, uintmax_t); break;
                        // all these promote to int
                        //case 'z': i = va_arg(ap, size_t); break;
                        //case 't': i = va_arg(ap, ptrdiff_t); break;
                        //case 'l': i = va_arg(ap, long int); break;
                        //case 'H': // char and short are promoted to int by va_arg
                        //case 'h':
                        default: i = va_arg(ap, unsigned int);
                    }
                    ret += _printi(pint, i, tmpc);
                    continue;
                }

                case 'a': c += 4; // waaaay too expensive to implement a fmt and i doubt it will ever be used
                case 'e':
                case 'f':
                case 'g':
                    ret += _printf(pint, va_arg(ap, double), tmpc);
                    continue;

                case 'p':
                    pint->flags = FLAG_0|FLAG_ALT;
                    pint->width = 8;
                    pint->prec = 255;
                    ret += _printi(pint, (uint32_t)va_arg(ap, void*), 'x');
                    continue;

                case 'm':
                    pint->flags = 0;
                    pint->width = 0;
                    pint->prec = -1;
                    ret += _printi(pint, errno, 'd');
                    continue;
            }
            // not consumed - output the char instead
            _putc(pint, c);
            ret++;
        }
    }
    if(pint->sptr && (pint->scnt != (size_t)-1)) _putc(pint, 0); // null terminate string output
    return ret;
}

// function wrappers for the libc calls...
int FNPRE(printf)(const char *format, ...) {
    va_list args;
    pint_t pint = {0};
    va_start(args, format);
    int ret = _vprintf(&pint, format, args);
    va_end(args);
    return ret;
}

int FNPRE(puts)(const char *s) {
    pint_t pint = {0};
    while(*s) _putc(&pint, *(s++));
    _putc(&pint, '\n');
    return 1;
}

int FNPRE(sprintf)(char *str, const char *format, ...) {
    va_list args;
    pint_t pint = {0};
    pint.sptr = str;
    pint.scnt = 0x7fffffff; // effectively unlimited...
    va_start(args, format);
    int ret = _vprintf(&pint, format, args);
    va_end(args);
    return ret;
}

int FNPRE(snprintf)(char *str, size_t size, const char *format, ...) {
    va_list args;
    pint_t pint = {0};
    pint.sptr = str;
    pint.scnt = size;
    va_start(args, format);
    int ret = _vprintf(&pint, format, args);
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
    //printf("printf foo '%s' '%llld'  '%d' xxx\n", "bar", 99, 88);

#define TPRINT(x, ...) \
    printf("*************** '%s' ****************\n", #x); \
    i = snprintf(buf, sizeof(buf), x, ##__VA_ARGS__); \
    printf("libc (%d): '%s'\n", i, buf); \
    i = tst_snprintf(buf, sizeof(buf), x, ##__VA_ARGS__); \
    printf("test (%d): '%s'\n", i, buf);
    
    //TPRINT("foo %% '%0*.*s' '%10.2s' '%s' '%10s' %c", 20,10,"barbarbroomba", (char*)NULL, (char*)NULL, (char*)NULL, 65)

    TPRINT("foo %% '% +-*.*d' %c", 20,15,-0x7fffffff, 65)

#if 1
    TPRINT("foo %% '%#015X'", 0x7fffffff)
    TPRINT("foo %% '%#015X'", 0x8fffffff)
    TPRINT("foo %% '%#015.12X'", 0x8fffffff)
    
    TPRINT("foo %% '%#015o'", 0x7fffffff)
    TPRINT("foo %% '%#015o'", 0x8fffffff)
    
    TPRINT("foo %% '%#o'", 0x7fffffff)
    TPRINT("foo %% '%#15o'", 0x8fffffff)
    
    TPRINT("foo %% '%o'", 0x7fff)
    TPRINT("foo %% '%15o'", 0x8fff)
    
    TPRINT("foo %% '%p' '%X' '%m'", &i, &i)

    TPRINT("foo %% '%f' '%f' '%f'", 99.0, 99999999.0, 0.099)
    TPRINT("foo %% '%10.0e' '%10.4e' '%10.4e'", 99.0, 9999999999999.0, 0.099)
    TPRINT("foo %% '%10.0g' '%10.4g' '%10.4g'", 99.0, 9999999999999.0, 0.099)
    TPRINT("foo %% '%10.0a' '%10.4a' '%10.4a'", 99.0, 9999999999999.0, 0.099)
#endif
    
    return 0;
}
#endif
