
#include <inttypes.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/user.h>

typedef union {
        uint8_t u8[PAGE_SIZE];
        uint16_t u16[PAGE_SIZE / sizeof(uint16_t)];
        uint32_t u32[PAGE_SIZE / sizeof(uint32_t)];
} page;

extern page buffer;

int
main(void)
{
        for (unsigned int x = 0; x < PAGE_SIZE / sizeof (uint32_t); x++) {
                buffer.u32[x] = x;
        }
        return 0;
}
