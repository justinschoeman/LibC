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
    
    6. 'a' format is not not complete - optionally compiled out to save space
    
    7. All 'capital letter' formats are handled, even if not valid (translated to lower
    case equivalent)
    
    8. m$ width/precision are parsed, but treated as unset...
    
    9. floating point precision is limited to 8 decimals
    
    10. 'g' is currently treated as 'e' need to clarify operation...
    
    11. width is limited to 255, precision to 254
    
*/

//#define BUILD_A
#define ROUND_UP

#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <math.h>
#include "local_printf.h"

#define FLAG_ALT	0x00000001U
#define FLAG_0		0x00000002U
#define FLAG_MINUS	0x00000004U
#define FLAG_SPACE	0x00000008U
#define FLAG_PLUS	0x00000010U
//#define FLAG_STRIP0	0x00000020U
#define FLAG_CAP	0x00000040U


/*
    Compile as follows to test...
    gcc -DTEST -g -Wall -o printf printf.c
*/


#ifdef TEST
#include <assert.h>
#include <stdlib.h>
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
        *(pint->sptr++) = c;
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

static int _prints(pint_t * pint, char *s) {
    int pad = 0;
    int ret = 0;
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
    if(!(pint->flags & FLAG_MINUS)) _padc(pint, ' ', &pad);
    while(*s) {
        if(pint->prec != 255 && ret >= pint->prec) break;
        _putc(pint, *(s++));
        ret++;
    }
    _padc(pint, ' ', &pad);
    return ret + pad;
}

// print an integer.  if dummy is true then only count the number of digits which would be output
// was trying to be 'eficient' by doing this without temp storage, but the cost in flash is way
// too high considering the stack saving.
// at worst 32 bit octal will be 11 digits, plus the one potential decimal (which can't happen with octal
// but is a potential input, so cater for it...
// DECS MUST NEVER BE BIGGER THAN 10
static int _puint(pint_t * pint, uint32_t num, int decs, int dummy) {
    int8_t tmp[12];
    int ret = 0;
    
    for(;;) {
        TESTFN(if(ret >= 12) *(int*)0=0;)
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
        dummy = tmp[decs];
        //_putc(pint, ' ');
        //_putc(pint, '0'+dummy/100);
        //_putc(pint, '0'+(dummy/10)%10);
        //_putc(pint, '0'+dummy%10);
        //_putc(pint, ' ');
        dummy += '0';
        if(dummy > '9') dummy += pint->flags & FLAG_CAP ? 'A' - ':' : 'a' - ':';
        _putc(pint, dummy);
    }
    return ret;
}

#ifdef BUILD_A
#define FRADIX pint->radix
#else
#define FRADIX 10
#endif

static int _printf(pint_t * pint, float fnum, char fmt) {
    uint32_t num = 0;
    int exp = 0;
    int sigc = 1; // default, print the zero
    int pad;
    int ret;
    char padc = 0;
    int zeros;
    const float limf = 4000000000.0f;
    //const float limf = 4000.0f;
    
    // strip least significant digit
    void chompnum(void) {
        num /= FRADIX;
        sigc--;
        exp++;
    }
    
    // round num to desired number of significant digits (if more are available than the target count)
    // note! rounding may ADD an extra significant digit
    // if force is set, then prune any additional significant digits that were added by rounding
#ifdef ROUND_UP
#warning using round_up
    void roundnum(int trg, int force) {
        if(trg < 0) {
            num = 0;
            goto done;
        }
        if(sigc <= trg) return; // already there...
        // initially, trim until we have one more sigc than required
        while(sigc > trg + 1) chompnum();
        if(padc == '-') {
            // negative number, up is towards 0, so number can never get longer
            chompnum();
            goto done;
        }
        // we now have one spare sigc, round it...
        num += FRADIX/2;
        chompnum(); // now remove this digit
        // recount digits as rounding may have changed things
        sigc = _puint(pint, num, 0, 1);
        // prune further, if rounded up
        if(force) while(sigc > trg) chompnum();
        //fprintf(stderr, "round %f %u %d %d\n", fnum, num, exp, sigc);
done:
        if(num == 0) {
            sigc = 1;
            exp = 0;
        }
    }
#else
#warning using round_down
    // C99 spec allows implementation defined rounding and we do not have default rounding control in libc
    // so for efficiency, choose round towards 0
    // this also means that significant digits can't increase, so we can skip 'force'
    void roundnum(int trg, int force __attribute__((unused))) {
        if(trg < 0) {
            num = 0;
            goto done;
        }
        while(sigc > trg) chompnum();
done:
        if(num == 0) {
            sigc = 1;
            exp = 0;
        }
    }
#endif    
    // special cases
    zeros = isinf(fnum);
    if(zeros || isnan(fnum)) {
        static char infstr[] = "inf";
        static char ninfstr[] = "-inf";
        static char nanstr[] = "nan";
        char * s;
        pint->prec = 255;
        if(zeros > 0) {
            s = infstr;
        } else if(zeros < 0) {
            s = ninfstr;
        } else {
            s = nanstr;
        }
        return _prints(pint, s);
    }
    
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
    
    // before we do anything else, convert to a uint with as many significant digits as possible...
    // no rounding yet, as we are packing a 23bit significand into 32 bits...
    // 2^32-1 = 4,294,967,295 - final rounding may bump up last digit, so 3,999,999,999 is the limit
    while(fnum != 0.0f) { // otherwise defaults to 0e0 .. FAKE WHILE LOOP SO WE CAN BREAK OUT AFTER SPECIAL FORMATS
#ifdef BUILD_A
        if(fmt == 'a') {
            // formatting not complete yet
            union { float f; uint32_t u; } unum = { .f = fnum };
            num = unum.u & 0x7fffff;
            num |= 0x800000;
            num <<= 1; // normailse (the 1 bit we added is the only one before the decimal)
            exp = (unum.u >> 23) & 0xff;
            exp -= 127 + 6;
            sigc = 7;
            break;
        }
#endif
        // scale to fit in uint
        while(fnum >= limf) {
            fnum /= (float)FRADIX;
            exp++;
        }
        while(fnum*FRADIX < limf) {
            fnum *= (float)FRADIX;
            exp--;
        }
        // cast to uint
        num = (uint32_t)fnum;
        if(num != 0) {
            // trim trailing zeros so we only have significant digits left
            while(num%FRADIX == 0) chompnum();
            // count significant digits
            sigc = _puint(pint, num, 0, 1);
        }
        break;
    }
    //fprintf(stderr, "%f %u %d %d\n", fnum, num, exp, sigc);
    
    // pre-process 'g' and morph to 'e'/'f' as required
    if(fmt == 'g') {
        //if(!(pint->flags & FLAG_ALT)) pint->flags |= FLAG_STRIP0; // strip trailing zeros for 'g' and not alt format
        if(pint->prec == 0) pint->prec++; // ...  if the precision is zero,  it  is  treated  as  1
        roundnum(pint->prec, 1); // ...The precision specifies the number of significant digits we desire in the output
        while(num && num%FRADIX == 0) chompnum(); // chomp trailing zeros, will use precision specifier to add them back, if required
        pad = exp + (sigc - 1); // tmp calculate exponent
        if(pad < -4 || pad >= pint->prec) { // from man page, exponent < -4 or >= precision, render as e otherwise f
            if(pint->flags & FLAG_ALT) {
                pint->prec--; // prec is total significant digits
            } else {
                pint->prec = sigc - 1;
            }
            fmt = 'e';
        } else {
            if(exp < 0) {
                pint->prec = -exp;
            } else {
                pint->prec = 0;
            }
            fmt = 'f';
        }
    }
    // calculate length, and padding
    if(fmt == 'f') {
        // direct formats
        // if our exponent moves our significant range below precision, then round...
        pad = pint->prec + exp; // how many we want *minus* how many we have (-exp) - gives us a -ve number telling us how many to remove
        if(pad < 0) roundnum(sigc + pad, 0); //(0)
        //fprintf(stderr, "%f %u %d %d\n", fnum, num, exp, sigc);
        // how many significant bits before the decimal?
        pad = sigc + exp;
        if(pad < 0) pad = 1; // always print at least 1 0 before the decimal
        if(pint->prec > 0) {
            pad += pint->prec + 1;
        } else {
            if(pint->flags & FLAG_ALT) pad++;
        }
    } else {
        // all other formats are exponent formats
        // round to 1+prec significant digits
        roundnum(pint->prec + 1, 1); // (1)
        exp += sigc - 1; // final exponent
        // calculate final length
        pad = 5; // 1 digit + e+dd
        //fprintf(stderr, "%f %u %d %d\n", fnum, num, exp, sigc);
        //if(pint->flags & FLAG_STRIP0) {
        //    pad += sigc; // sigc = 1 + decimals, we already counted the 1 above, so this is decimal point + decimals
        //    if(sigc == 1) pad--;  // only one digit, no decimal point, flag alt has different meaning for 'g' so don't check here
        //} else 
        if(pint->prec > 0) {
            pad += pint->prec + 1;
        } else {
            if(pint->flags & FLAG_ALT) pad++; // no decimals, but force decimal point
        }
    }
    if(padc) pad++;
    
    // calculate total padding (and set ret to pre-calculated chars)
    if(pint->width > 0 && pad < pint->width) {
        pad = pint->width - pad;
        ret = pint->width;
    } else {
        ret = pad;
        pad = 0;
    }
    // output initial pad
    if(!(pint->flags & FLAG_MINUS)) {
        if(pint->flags & FLAG_0) {
            if(padc) _putc(pint, padc);
            padc = 0;
            _padc(pint, '0', &pad);
        } else {
            _padc(pint, ' ', &pad);
        }
    }
    // output first char
    if(padc) _putc(pint, padc);
    
    // now build the numeric portion based on format
    if(fmt == 'f') {
        // different choices depending on where we have to pad zeros...
        // do we have significant digits before the decimal?
        if(sigc + exp > 0) {
            _puint(pint, num, -exp, 0);
            // still need more zeros?
            zeros = exp;
        } else zeros = 1; // at least 1 zero before the decimal...
        _padc(pint, '0', &zeros);
        if(pint->prec > 0) {
            // have we already output significant digits?
            if(sigc + exp > 0) {
                zeros = pint->prec;
                if(exp < 0) {
                    zeros += exp;
                } else {
                    _putc(pint, '.');
                }
            } else {
                // still need to send significant digits
                _putc(pint, '.');
                // zero padding required?
                zeros = -exp - sigc;
                _padc(pint, '0', &zeros);
                _puint(pint, num, 0, 0);
                // still more zeros???
                zeros = pint->prec + exp;
            }
            _padc(pint, '0', &zeros);
        } else {
            if(pint->flags & FLAG_ALT) _putc(pint, '.'); // no decimals, but force decimal point
        }
    } else {
        // exponent type formats
        // we have sigc-1 decimals...
        sigc--;
        _puint(pint, num, sigc, 0); // display what we have
        //if(pint->flags & FLAG_ALT || (sigc == 0 && pint->prec > 0 && !(pint->flags & FLAG_STRIP0))) _putc(pint, '.');
        if(sigc == 0 && (pint->flags & FLAG_ALT || pint->prec > 0)) _putc(pint, '.');
        //if(!(pint->flags & FLAG_STRIP0)) {
            sigc = pint->prec - sigc;
            _padc(pint, '0', &sigc);
        //}
#ifdef BUILD_A
        _putc(pint, pint->flags & FLAG_CAP ? (fmt == 'a' ? 'P' : 'E') : (fmt == 'a' ? 'p' : 'e'));
#else
        _putc(pint, pint->flags & FLAG_CAP ? 'E' : 'e');
#endif
        _putc(pint, exp >= 0 ? '+' : '-');
        exp = exp < 0 ? -exp : exp;
        if(exp < 10) _putc(pint, '0');
        pint->radix=10; // exponent is always base 10?
        _puint(pint, exp, 0, 0);
    }
    
    // if there is any padding left, send it now
    _padc(pint, ' ', &pad);
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
    if(!(pint->flags & FLAG_MINUS)) {
        if(pint->flags & FLAG_0 && pad > 0 && pint->prec == 255) {
            zeros += pad;
            pad = 0;
        } else {
            ret += _padc(pint, ' ', &pad);
        }
    }
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
                int i = va_arg(ap, int);
                if(i < 0) {
                    pint->flags |= FLAG_MINUS;
                    pint->width = -i;
                } else {
                    pint->width = i;
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
                int i = va_arg(ap, int);
                pint->prec = i < 0 ? 255 : i;
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
                    if(*format == 'h') {
                        // hh?
                        mod = 'H';
                        format++;
                    } else mod = 'h';
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
            if(pint->flags & FLAG_MINUS) pint->flags &= ~(uint8_t)FLAG_0; // ... A  - overrides a 0 if both are given.
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

#ifdef BUILD_A
                case 'a': pint->radix=16;
#else
                case 'a': tmpc += 4; // waaaay too expensive to implement a fmt and i doubt it will ever be used
#endif
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
                
                case 'n': {
                    void * p = va_arg(ap, void*);
                    switch(mod) {
                        case 'H': *(int8_t*)p = ret; break;
                        case 'h': *(int16_t*)p = ret; break;
                        case 'L': *(int64_t*)p = ret; break;
                        default: *(int32_t*)p = ret;
                    }
                    continue;
                }
                    
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

void stress(void) {
    for(;;) {
        float sig = rand()-RAND_MAX/2;
        float expn = rand()%150-75.0;
        int width = rand()%50-25;
        int prec = rand()%20;
        float num = sig * powf(10.0, expn);
        tst_printf("sig:%.0f exp:%.0f width:%d prec:%d\n", sig, expn, width, prec);
        tst_printf     ("g:'%g'\n", num);
        fprintf(stderr, "g:'%g'\n", num);
        tst_printf(     "g**:'%*.*g'\n", width, prec, num);
        fprintf(stderr, "g**:'%*.*g'\n", width, prec, num);
    }
}

int main(void) {
    char buf[1000];
    char buf1[1000];
    int i;
    int8_t nc;
    int16_t ns;
    int32_t ni;
    int64_t nl;
    
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
    i = tst_snprintf(buf1, sizeof(buf1), x, ##__VA_ARGS__); \
    printf("test (%d): '%s'\n", i, buf1); \
    if(strncmp(buf, buf1, sizeof(buf)) != 0) printf("############################### FAIL ###############################\n");
    
    //TPRINT("foo %% '%0*.*s' '%10.2s' '%s' '%10s' %c", 20,10,"barbarbroomba", (char*)NULL, (char*)NULL, (char*)NULL, 65)

    TPRINT("foo %% '% +-*.*d' %c", 20,15,-0x7fffffff, 65)

#if 1
    TPRINT("foo %% '%#015X'", 0x7fffffff)
    TPRINT("foo %% '%#015X'", 0x8fffffff)
    TPRINT("foo %% '%#015.12X'", 0x8fffffff)

    TPRINT("foo %% '%015X'", 0x7fffffff)
    TPRINT("foo %% '%015X'", 0x8fffffff)
    TPRINT("foo %% '%015.12X'", 0x8fffffff)
    
    TPRINT("foo %% '%-015X'", 0x7fffffff)
    TPRINT("foo %% '%-015X'", 0x8fffffff)
    TPRINT("foo %% '%-015.12X'", 0x8fffffff)
    
    TPRINT("foo %% '%#015o'", 0x7fffffff)
    TPRINT("foo %% '%#015o'", 0x8fffffff)
    
    TPRINT("foo %% '%#o'", 0x7fffffff)
    TPRINT("foo %% '%#15o'", 0x8fffffff)
    
    TPRINT("foo %% '%o'", 0x7fff)
    TPRINT("foo %% '%15o'", 0x8fff)
    
    TPRINT("foo %% '%p' '%X' '%m'", &i, &i)

    TPRINT("foo %% '%f' '%f' '%f'", 99.0, 99999999.0, 0.099)
    TPRINT("foo %% '%f' '%f' '%f' '%f'", 0.000099f, 0.0000099f, 0.00000099f, 0.000000099f)
    TPRINT("foo %% '%20.0e' '%20.4e' '%20.4e'", 99.0, 9999999999999.0, 0.099)
    TPRINT("foo %% '%20.0g' '%20.4g' '%20.4g'", 99.0, 9999999999999.0, 0.099)
    TPRINT("foo %% '%20.0g' '%20.4g' '%20.4g'", 999999.0, 999999.0, 0.099)
    TPRINT("foo %% '%20.0g' '%20.4g' '%20.4g'", 9.999f, 99.99f, 999.9f)
    TPRINT("foo %% '%-20.0g' '%020.4g' '%20.4g'", 99.999f, -999.99f, 9999.9f)
    TPRINT("foo %% '%#20.0g' '%#20.4g' '%#20.4g'", 99.0, 9999999999999.0, 0.099)
    TPRINT("foo %% '%#20.0g' '%#20.4g' '%#20.4g'", 999999.0, 999999.0, 0.099)
    TPRINT("foo %% '%#20.0g' '%#20.4g' '%#20.4g'", 9.999f, 99.99f, 999.9f)
    TPRINT("foo %% '%#20.0g' '%#20.4g' '%#20.4g'", 99.999f, 999.99f, 9999.9f)
    TPRINT("foo %% '%e' '%e' '%e' '%e'", 1234567890e20f, 432109876543e-20f, 123456789012345.0f, 234567890123456789e5f)
    TPRINT("foo %% '%.20e' '%.20e' '%.20e' '%.20e'", 1234567890e20f, 432109876543e-20f, 123456789012345.0f, 234567890123456789e5f)
    TPRINT("foo %% '%.20f' '%.20f' '%.20f' '%.20f'", 1234567890e20f, 432109876543e-20f, 123456789012345.0f, 234567890123456789e5f)
    TPRINT("foo %% '%f' '%f' '%f'", NAN, INFINITY, -INFINITY)
    TPRINT("foo %% '%.0f' '%.0f' '%.0f'", 0.5f, -0.5f, 0.0f)
    TPRINT("foo %% '%20.0a' '%20.4a' '%20.4a' '%20.4a' 1 %hhn 2  %hn 3  %n 4  %lln 5", 99.0, 9999999999999.0, 0.099, 12345e-10f, &nc, &ns, &ni, &nl)
    fprintf(stderr, "%d %d %d %lld\n", nc, ns, ni, nl);
#endif
    fprintf(stderr, "%d\n", sizeof(pint_t));
    stress();
    return 0;
}
#endif
