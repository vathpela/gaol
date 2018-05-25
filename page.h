/*
 * page.h
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 */

#ifndef PAGE_H_
#define PAGE_H_

#define ALIGN_PADDING(addr, align) (((align) - ((addr) % (align))) % (align))
#define ALIGN_DOWN(addr, align) ((addr) - ((align) - ALIGN_PADDING(addr, align)))
#define ALIGN_UP(addr, align) ((addr) + ALIGN_PADDING(addr, align))

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif
#endif

#define BYTES_TO_PAGES(bytes) ((bytes) >> PAGE_SHIFT)
#define PAGES_TO_BYTES(pages) ((pages) << PAGE_SHIFT)

#define PAGE_ALIGN_UP(bytes) ALIGN_UP(bytes, PAGE_SIZE)
#define PAGE_ALIGN_DOWN(bytes) ALIGN_DOWN(bytes, PAGE_SIZE)

#define N_PAGES(bytes) BYTES_TO_PAGES(PAGE_ALIGN_UP(bytes))
#define STARTING_PAGE(addr) BYTES_TO_PAGES(PAGE_ALIGN_DOWN(bytes))

#endif /* !PAGE_H_ */
// vim:fenc=utf-8:tw=75:et
