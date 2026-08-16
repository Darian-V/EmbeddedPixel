#ifndef __CC_H__
#define __CC_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define BYTE_ORDER  LITTLE_ENDIAN

typedef uint8_t     u8_t;
typedef int8_t      s8_t;
typedef uint16_t    u16_t;
typedef int16_t     s16_t;
typedef uint32_t    u32_t;
typedef int32_t     s32_t;
typedef uintptr_t   mem_ptr_t;

#define LWIP_ERR_T  int

/* Compiler hints for packing structures */
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__ ((__packed__))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

#ifdef __cplusplus
extern "C" {
#endif

static inline void lwip_platform_diag(const char *format, ...) {
    static char buffer[256]; // Static to avoid stack overflow in tcpip_thread
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Convert any solo '\n' to '\r\n' for clean serial console output
    for (int i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] == '\n' && (i == 0 || buffer[i - 1] != '\r')) {
            putchar('\r');
        }
        putchar(buffer[i]);
    }
}

#ifdef __cplusplus
}
#endif

#define LWIP_PLATFORM_DIAG(x)   do { lwip_platform_diag x; } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { printf("Assertion \"%s\" failed at line %d in %s\r\n", \
                                     x, __LINE__, __FILE__); while(1); } while(0)

#define LWIP_RAND() ((u32_t)rand())

#endif /* __CC_H__ */
