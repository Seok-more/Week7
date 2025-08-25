#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "mm.h"
#include "memlib.h"

team_t team = {
    "UnrealMallocLab",
    "Seok-more",
    "seokmore@github.com",
    "",
    ""
};

#define MIN_BLOCK_SIZE (4 * WSIZE) // 헤더+pred+succ+푸터
#define BIN_COUNT 32

typedef struct FreeBlock {
    struct FreeBlock* pred;
    struct FreeBlock* succ;
} FreeBlock;

static FreeBlock* bins[BIN_COUNT];
static char *heap_listp = NULL;

// bp는 pred 위치
#define FB(bp) ((FreeBlock *)(bp))

static int get_bin_index(size_t size);
static void* extend_heap(size_t words);
static void* coalesce(void* bp);
static void insert_free_block(void* bp);
static void delete_free_block(void* bp);
static void* find_fit(size_t asize);
static void place(void* bp, size_t asize);

// bin index 계산
static int get_bin_index(size_t size) {
    int idx = 0;
    size_t t = MIN_BLOCK_SIZE;
    while (idx < BIN_COUNT-1 && size > t) {
        t <<= 1;
        idx++;
    }
    return idx;
}

static void insert_free_block(void* bp) {
    int idx = get_bin_index(GET_SIZE(HDRP(bp)));
    FB(bp)->pred = NULL;
    FB(bp)->succ = bins[idx];
    if (bins[idx]) FB(bins[idx])->pred = bp;
    bins[idx] = bp;
}

static void delete_free_block(void* bp) {
    int idx = get_bin_index(GET_SIZE(HDRP(bp)));
    if (FB(bp)->pred)
        FB(FB(bp)->pred)->succ = FB(bp)->succ;
    else
        bins[idx] = FB(bp)->succ;
    if (FB(bp)->succ)
        FB(FB(bp)->succ)->pred = FB(bp)->pred;
}

static void* extend_heap(size_t words) {
    size_t size = ALIGN(words * WSIZE);
    if (size < MIN_BLOCK_SIZE) size = MIN_BLOCK_SIZE;
    char* block = mem_sbrk(size + WSIZE); // 에필로그 포함
    if ((long)block == -1) return NULL;
    PUT(block, PACK(size, 0)); // 헤더
    PUT(block + size - WSIZE, PACK(size, 0)); // 푸터
    PUT(block + size, PACK(0, 1)); // 에필로그 헤더
    void* bp = block + WSIZE; // pred 위치
    return coalesce(bp);
}

static void* coalesce(void* bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    void* prev_bp = PREV_BLKP(bp);
    void* next_bp = NEXT_BLKP(bp);

    if (!prev_alloc && GET_SIZE(HDRP(prev_bp)) >= MIN_BLOCK_SIZE) {
        delete_free_block(prev_bp);
        size += GET_SIZE(HDRP(prev_bp));
        bp = prev_bp;
    }
    if (!next_alloc && GET_SIZE(HDRP(next_bp)) >= MIN_BLOCK_SIZE) {
        delete_free_block(next_bp);
        size += GET_SIZE(HDRP(next_bp));
    }
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    insert_free_block(bp);
    return bp;
}

static void* find_fit(size_t asize) {
    int idx = get_bin_index(asize);
    for (int i = idx; i < BIN_COUNT; i++) {
        void* bp = bins[i];
        while (bp) {
            if (GET_SIZE(HDRP(bp)) >= asize)
                return bp;
            bp = FB(bp)->succ;
        }
    }
    return NULL;
}

static void place(void* bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    delete_free_block(bp);
    if (csize - asize >= MIN_BLOCK_SIZE) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void* next_bp = (char*)bp + asize;
        PUT(HDRP(next_bp), PACK(csize-asize, 0));
        PUT(FTRP(next_bp), PACK(csize-asize, 0));
        insert_free_block(next_bp);
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

int mm_init(void) {
    memset(bins, 0, sizeof(bins));
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void*)-1) return -1;
    PUT(heap_listp, 0);                          // padding
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1));     // prologue header
    PUT(heap_listp + 2*WSIZE, PACK(DSIZE, 1));   // prologue footer
    PUT(heap_listp + 3*WSIZE, PACK(0, 1));       // epilogue header
    heap_listp += 2*WSIZE;
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) return -1;
    return 0;
}

void* mm_malloc(size_t size) {
    if (size == 0) return NULL;
    size_t asize = (size <= DSIZE) ? MIN_BLOCK_SIZE : ALIGN(size + 2*WSIZE); // 헤더+pred+succ+푸터+payload
    void* bp = find_fit(asize);
    if (bp) {
        place(bp, asize);
        return (char*)bp + 2*WSIZE; // payload 주소 반환!
    }
    size_t extendsize = (asize > CHUNKSIZE) ? asize : CHUNKSIZE;
    bp = extend_heap(extendsize/WSIZE);
    if (!bp) return NULL;
    place(bp, asize);
    return (char*)bp + 2*WSIZE;
}

void mm_free(void* ptr) {
    if (!ptr) return;
    void* bp = (char*)ptr - 2*WSIZE; // payload→pred 변환
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

void* mm_realloc(void* ptr, size_t size) {
    if (ptr == NULL) return mm_malloc(size);
    if (size == 0) { mm_free(ptr); return NULL; }
    void* bp = (char*)ptr - 2*WSIZE;
    size_t old_size = GET_SIZE(HDRP(bp));
    size_t asize = (size <= DSIZE) ? MIN_BLOCK_SIZE : ALIGN(size + 2*WSIZE);
    if (asize <= old_size) return ptr;
    char* next_bp = NEXT_BLKP(bp);
    size_t next_alloc = GET_ALLOC(HDRP(next_bp));
    size_t next_size = GET_SIZE(HDRP(next_bp));
    if (!next_alloc && old_size + next_size >= asize) {
        delete_free_block(next_bp);
        PUT(HDRP(bp), PACK(old_size + next_size, 1));
        PUT(FTRP(bp), PACK(old_size + next_size, 1));
        return ptr;
    }
    void* newptr = mm_malloc(size);
    if (!newptr) return NULL;
    size_t copySize = old_size - 2*WSIZE;
    if (size < copySize) copySize = size;
    memcpy(newptr, ptr, copySize);
    mm_free(ptr);
    return newptr;
}