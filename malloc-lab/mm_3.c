/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "UnrealSegregated",
    /* First member's full name */
    "Seok-more",
    /* First member's email address */
    "wjstjrah2000@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/////////////////////////////////
static char *heap_listp = NULL; // 힙의 첫 시작점(프롤로그 블록의 payload)을 가리킴
static char *free_listp = NULL; // 힙에서 가장 처음에 있는 free 블록 주소 가리킴
static void *free_bins[BIN_COUNT] = {0}; // 각 bin별 free list 헤드
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
//static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_free_block(void *bp, int bin);
static void delete_free_block(void *bp, int bin);
static int find_bin(size_t size);
static void *find_fit(size_t asize, int *bin_out);
/////////////////////////////////

/*
 * find_bin - 크기에 맞는 bin 인덱스 반환
 */
static int find_bin(size_t size)
{
    int bin = BIN_INDEX(size);
    if (bin >= BIN_COUNT) bin = BIN_COUNT - 1;
    return bin;
}

/*
 *  insert_free_block - free 블록을 가용 리스트에 추가, address order가 더 좋대
 *  - pred/succ를 항상 초기화한다.
 *  - 중복 삽입(이미 list에 있는 블록을 또 넣는 것) 방지.
 */
static void insert_free_block(void *bp, int bin)
{
    // pred/succ 항상 초기화
    PRED(bp) = NULL;
    SUCC(bp) = free_bins[bin];

    // 중복삽입 방지: bp가 이미 list에 있으면 아무것도 하지 않음
    // (실제 구현에서는 assert로 잡거나 skip, 여기서는 간단하게 처리)
    if (free_bins[bin] == bp) return;

    if (free_bins[bin])
        PRED(free_bins[bin]) = bp;
    free_bins[bin] = bp;
}

/*
 *  delete_free_block - free 블록을 가용 리스트에서 제거
 *  - pred/succ를 항상 끊어준다.
 *  - bp가 list의 헤드일 때와 아닐 때 모두 안전하게 처리
 */
static void delete_free_block(void *bp, int bin)
{
    // bp가 free_bins[bin]의 헤드이면
    if (free_bins[bin] == bp) {
        free_bins[bin] = SUCC(bp);
        if (SUCC(bp)) PRED(SUCC(bp)) = NULL;
    } else {
        if (PRED(bp)) SUCC(PRED(bp)) = SUCC(bp);
        if (SUCC(bp)) PRED(SUCC(bp)) = PRED(bp);
    }
    // bp의 pred/succ 끊기
    PRED(bp) = NULL;
    SUCC(bp) = NULL;
}

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    // 1. 초기 빈 힙 생성 (4워드 메모리 할당)
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void*)-1)  return -1;

    // 2. 빈 힙에 프롤로그 블록과 에필로그 헤더를 추가 -> 프롤로그 불록은 힙의 맨 앞부분에 존재하는 allocated된 더미 블록
    // 초기 힙 구조 : [정렬 패딩][프롤로그 헤더][프롤로그 푸터][첫 가용 블록 ...][에필로그 헤더]
    //              ↑         ↑            ↑            ↑               ↑
    //        heap_listp heap_listp+WSIZE heap_listp+2*WSIZE heap_listp+3*WSIZE ...   

    PUT(heap_listp, 0);                          // 정렬 패딩
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // 프롤로그 헤더
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // 프롤로그 푸터
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     // 에필로그 헤더

    heap_listp += (2*WSIZE); // heap_listp를 첫 가용 블록의 payload 주소로 이동(보통 payload 기준으로 블록포인터 잡음)

    // free_bins 초기화
    for (int i = 0; i < BIN_COUNT; ++i)
        free_bins[i] = NULL;

    // 3. 빈 힙을 CHUNKSIZE 크기의 가용 블록으로 확장 
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) return -1;

    return 0;
}

/*
 * extend_heap - 힙을 words만큼 확장, 새로운 가용 블록을 리턴함
 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    // 항상 8바이트 단위로 정렬, 짝수 워드 할당
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;

    // 새 가용 블록의 헤더/푸터, 새로운 에필로그 헤더 초기화
    PUT(HDRP(bp), PACK(size, 0));         // 헤더: 크기, free
    PUT(FTRP(bp), PACK(size, 0));         // 푸터: 크기, free
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // 에필로그 헤더: 크기 0, 할당1

    // 이전 블록이 free라면 합침 (coalesce)
    return coalesce(bp);
}

/*
 * mm_malloc - segregated bin에서 first-fit으로 할당
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;
    int bin;

    if (size == 0) return NULL;

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = ALIGN(size + DSIZE);

    if ((bp = find_fit(asize, &bin)) != NULL)
    {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) return NULL;
    place(bp, asize);
    return bp;
}

/*
 * find_fit - 요구 asize 이상의 free block을 bin에서 first-fit으로 찾음
 */
static void *find_fit(size_t asize, int *bin_out)
{
    int bin = find_bin(asize);

    // 작은 bin부터 큰 bin까지 탐색
    for (int i = bin; i < BIN_COUNT; ++i)
    {
        void *bp = free_bins[i];
        int cnt = 0;
        while (bp)
        {
            // 무한루프 방지: bp == SUCC(bp)면 break
            if (bp == SUCC(bp)) break;
            if (GET_SIZE(HDRP(bp)) >= asize)
            {
                if (bin_out) *bin_out = i;
                return bp;
            }
            bp = SUCC(bp);
            cnt++;
            if (cnt > 10000) break; // 비정상 루프 방어 (실제 프로그램에서는 assert)
        }
    }
    return NULL;
}

/*
 * place - bp 위치에 asize만큼 할당, 분할 시 남은 부분을 올바른 bin에 삽입
 */
static void place(void *bp, size_t asize)
{
    size_t totalsize = GET_SIZE(HDRP(bp));
    int bin = find_bin(totalsize);
    delete_free_block(bp, bin);

    if ((totalsize - asize) >= (2 * DSIZE))
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));

        char *next_bp = NEXT_BLKP(bp);
        PUT(HDRP(next_bp), PACK(totalsize - asize, 0));
        PUT(FTRP(next_bp), PACK(totalsize - asize, 0));
        int next_bin = find_bin(totalsize - asize);
        insert_free_block(next_bp, next_bin);
    }
    else
    {
        PUT(HDRP(bp), PACK(totalsize, 1));
        PUT(FTRP(bp), PACK(totalsize, 1));
    }
}

/*
 * coalesce - 인접 free 블록 병합 (Unreal: 병합 후 적절 bin에 삽입)
 * - 병합된 블록의 pred/succ를 항상 초기화
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    int bin;
    if (prev_alloc && next_alloc) {
        bin = find_bin(size);
        insert_free_block(bp, bin);
        return bp;
    }
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        int next_bin = find_bin(GET_SIZE(HDRP(NEXT_BLKP(bp))));
        delete_free_block(NEXT_BLKP(bp), next_bin);

        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bin = find_bin(size);
        insert_free_block(bp, bin);
    }
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        int prev_bin = find_bin(GET_SIZE(HDRP(PREV_BLKP(bp))));
        bp = PREV_BLKP(bp);
        delete_free_block(bp, prev_bin);

        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bin = find_bin(size);
        insert_free_block(bp, bin);
    }
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        int prev_bin = find_bin(GET_SIZE(HDRP(PREV_BLKP(bp))));
        int next_bin = find_bin(GET_SIZE(HDRP(NEXT_BLKP(bp))));
        delete_free_block(PREV_BLKP(bp), prev_bin);
        delete_free_block(NEXT_BLKP(bp), next_bin);

        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bin = find_bin(size);
        insert_free_block(bp, bin);
    }
    // 병합 후 항상 pred/succ 초기화
    PRED(bp) = NULL;
    SUCC(bp) = NULL;
    return bp;
}

/*
 * mm_free - free block 병합 후 bin에 삽입
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

/*
 * mm_realloc - in-place 확장/축소, 아니면 새로 할당 후 복사
 */
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) return mm_malloc(size);
    if (size == 0) { mm_free(ptr); return NULL; }

    size_t old_size = GET_SIZE(HDRP(ptr));
    size_t asize = (size <= DSIZE) ? 2 * DSIZE : ALIGN(size + DSIZE);

    // 축소: in-place 분할
    if (asize < old_size && (old_size - asize) >= (2 * DSIZE))
    {
        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1));
        char *next_blk = NEXT_BLKP(ptr);
        PUT(HDRP(next_blk), PACK(old_size - asize, 0));
        PUT(FTRP(next_blk), PACK(old_size - asize, 0));
        int bin = find_bin(old_size - asize);
        insert_free_block(next_blk, bin);
        return ptr;
    }

    // in-place 확장: 다음 블록이 free이고 충분하다면 병합 후 사용
    void *next_blk = NEXT_BLKP(ptr);
    size_t next_alloc = GET_ALLOC(HDRP(next_blk));
    size_t next_size = GET_SIZE(HDRP(next_blk));
    if (!next_alloc && (old_size + next_size) >= asize)
    {
        int next_bin = find_bin(next_size);
        delete_free_block(next_blk, next_bin);
        size_t combined_size = old_size + next_size;
        if ((combined_size - asize) >= (2 * DSIZE))
        {
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            char *next_new_blk = NEXT_BLKP(ptr);
            PUT(HDRP(next_new_blk), PACK(combined_size - asize, 0));
            PUT(FTRP(next_new_blk), PACK(combined_size - asize, 0));
            int bin = find_bin(combined_size - asize);
            insert_free_block(next_new_blk, bin);
        }
        else
        {
            PUT(HDRP(ptr), PACK(combined_size, 1));
            PUT(FTRP(ptr), PACK(combined_size, 1));
        }
        return ptr;
    }

    // 새로 할당하여 복사
    void *newptr = mm_malloc(size);
    if (newptr == NULL) return NULL;
    size_t copySize = (old_size - DSIZE < size) ? (old_size - DSIZE) : size;
    memcpy(newptr, ptr, copySize);
    mm_free(ptr);
    return newptr;
}