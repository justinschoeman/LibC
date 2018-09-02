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
