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
