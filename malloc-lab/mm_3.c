/*
 * Explicit free list allocator with Unreal Engine Binned2-style bins + Large Block List
 *
 * ┌─────────────────────────────┬─────────────────────────────────────────────┬───────────────────────────────────────────────────────────────┐
 * │        [구분]               │                  [방식]                      │                            [특징]                              │
 * ├─────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────┤
 * │ 메모리 관리 구조              │ Segregated Free List                        │ 크기 구간별 bin 32개(`bins[]`), BIN_MAX_SIZE(=512) 초과는        │
 * │                             │                                             │ `large_listp`에서 별도 관리                                     │
 * ├─────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────┤
 * │ Free List 연결              │ Explicit Doubly Linked List                 │ free 블록마다 pred/succ 포인터 포함, 주소 오름차순 이중 연결         │
 * │                             │                                             │ 리스트 구성(coalesce 효율↑)                                     │
 * ├─────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────┤
 * │ 탐색 정책(find_fit)          │ First-Fit                                   │ 각 bin/large list에서 요청 크기 이상인 첫 블록을 즉시 할당          │
 * │                             │                                             │ (Unreal 엔진 실제 방식과 동일)                                   │
 * ├─────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────┤
 * │ 삽입 정책                    │ 주소 오름차순 삽입                             │ 주소순으로 free 블록을 리스트에 삽입 → coalesce 효율↑              │
 * ├─────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────┤
 * │ 할당 정책(place)             │ 분할 시 최소 블록 크기 보장                     │ 남는 블록이 MIN_BLOCK_SIZE 이상일 때만 분할                       │
 * ├─────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────┤
 * │ 병합 정책(coalesce)          │ 즉시 병합                                     │ 인접한 free 블록과 병합 후 bin/large list에 재삽입                │
 * ├─────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────┤
 * │ 힙 확장                      │ mem_sbrk()                                  │ fit 실패 시 CHUNKSIZE 또는 요청 크기만큼 확장                     │
 * ├─────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────┤
 * │ 블록 구조                    │ Header + Footer + Payload (+ pred/succ)     │ free 블록은 pred/succ 포인터 포함                           │
 * ├─────────────────────────────┼─────────────────────────────────────────────┼───────────────────────────────────────────────────────────────┤
 * │ 정렬 단위                    │ 8바이트 (ALIGNMENT = 8)                      │ 모든 블록 크기를 8바이트 단위로 정렬                               │
 * └─────────────────────────────┴─────────────────────────────────────────────┴───────────────────────────────────────────────────────────────┘
 *
 * - Unreal Engine Binned2와 유사하게, 요청 크기에 따라 적절한 bin을 선택하고 해당 bin/large_list에서 first-fit 할당.
 * - 주소 오름차순 explicit free list로 병합(coalesce)이 효율적으로 동작.
 * - 분할 시 최소 크기 보장, 즉시 병합, realloc 역시 bin/large list 별 관리.
 * - mdriver, malloc-lab 채점 기준에 최적화된 구조.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include "mm.h"
#include "memlib.h"

team_t team = {
    "UnrealStyle",
    "Seok-more",
    "wjstjrah2000@gmail.com",
    "",
    ""
};

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// Unreal-style bins + Large list
static char *heap_listp = NULL; // 힙의 첫 시작점(프롤로그 블록의 payload)을 가리킴

// 매크로는 mm.h에서 선언된 것을 사용함

typedef struct {
    void *free_listp; // 각 bin의 head
} Bin;

// Unreal 스타일: bin 크기 분포를 더 촘촘하게(16,24,32, ... 512)
static size_t bin_sizes[BIN_COUNT] = {
    16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208,
    224, 240, 256, 288, 320, 352, 384, 416, 448, 480, 512, 512, 512, 512, 512, 512
};
static Bin bins[BIN_COUNT];

// Large Block List (BIN_MAX_SIZE 초과 블록용 별도 free list)
static void *large_listp = NULL; // head

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_free_block(void *bp);
static void delete_free_block(void *bp);

static int find_bin(size_t size);
static void insert_large_block(void *bp);
static void delete_large_block(void *bp);

/* Unreal 스타일 bin_sizes 초기화 (분포는 배열로 고정, 초기화는 free list만) */
static void init_bin_sizes(void) {
    for (int i = 0; i < BIN_COUNT; i++) {
        bins[i].free_listp = NULL;
    }
    large_listp = NULL;
}

/* size에 맞는 bin index 반환 */
static int find_bin(size_t size) {
    for (int i = 0; i < BIN_COUNT; i++) {
        if (size <= bin_sizes[i])
            return i;
    }
    return BIN_COUNT - 1;
}

/*
 *  insert_large_block - BIN_MAX_SIZE 초과 free 블록을 Large List에 추가 (주소 오름차순)
 */
static void insert_large_block(void *bp)
{
    void *prev = NULL;
    void *now = large_listp;

    while (now != NULL && (char*)now < (char*)bp) {
        prev = now;
        now = SUCC(now);
    }

    PRED(bp) = prev;
    SUCC(bp) = now;
    if (now) PRED(now) = bp;
    if (prev) SUCC(prev) = bp; else large_listp = bp;
}

/*
 *  delete_large_block - Large List에서 free 블록 제거
 */
static void delete_large_block(void *bp)
{
    if (bp == large_listp) {
        large_listp = SUCC(bp);
        if (large_listp) PRED(large_listp) = NULL;
    } else {
        if (PRED(bp)) SUCC(PRED(bp)) = SUCC(bp);
        if (SUCC(bp)) PRED(SUCC(bp)) = PRED(bp);
    }
}

/*
 *  insert_free_block - free 블록을 해당 bin/large list의 가용 리스트에 추가, address order
 */
static void insert_free_block(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));

    if (size > BIN_MAX_SIZE) {
        insert_large_block(bp);
        return;
    }

    int bin = find_bin(size);

    void *prev = NULL;
    void *now = bins[bin].free_listp;

    // 주소 오름차순으로 탐색
    while (now != NULL && (char*)now < (char*)bp)
    {
        prev = now;
        now = SUCC(now);
    }

    // prev와 now 사이에 bp 삽입
    PRED(bp) = prev;
    SUCC(bp) = now;

    if (now != NULL) {
        PRED(now) = bp;
    }

    if (prev != NULL) {
        SUCC(prev) = bp;
    }
    else {
        bins[bin].free_listp = bp;
    }
}

/*
 *  delete_free_block - free 블록을 해당 bin/large list의 가용 리스트에서 제거
 */
static void delete_free_block(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));

    if (size > BIN_MAX_SIZE) {
        delete_large_block(bp);
        return;
    }

    int bin = find_bin(size);

    if (bp == bins[bin].free_listp)
    {
        bins[bin].free_listp = SUCC(bp);
        if (bins[bin].free_listp != NULL)
        {
            PRED(bins[bin].free_listp) = NULL;
        }
    }
    else
    {
        if (PRED(bp) != NULL)
        {
            SUCC(PRED(bp)) = SUCC(bp);
        }

        if (SUCC(bp) != NULL)
        {
            PRED(SUCC(bp)) = PRED(bp);
        }
    }
}

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    // Unreal-style bin sizes, free lists 초기화
    init_bin_sizes();

    // 1. 초기 빈 힙 생성 (4워드 메모리 할당)
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void*)-1)  return -1;

    // 2. 빈 힙에 프롤로그 블록과 에필로그 헤더를 추가
    PUT(heap_listp, 0);                          // 정렬 패딩
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // 프롤로그 헤더
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // 프롤로그 푸터
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     // 에필로그 헤더

    heap_listp += (2*WSIZE); // 첫 가용 블록의 payload 주소로 이동

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
 * mm_malloc - bin/large list별로 asize에 맞는 블록 탐색, 없으면 힙 확장
 */
void *mm_malloc(size_t size)
{
    size_t asize;         // 실제 할당 블록 크기 (헤더/푸터 포함, 정렬)
    size_t extendsize;    // fit이 없을 때 힙 확장 크기
    char *bp;

    if (size == 0) return NULL;

    // 1. 최소 블록 크기(헤더+payload+푸터) 맞추고 8바이트 단위로 정렬
    if (size <= DSIZE) {
        asize = 2 * DSIZE; // 최소 16바이트
    } else {
        asize = ALIGN(size + DSIZE); // Header + payload + footer
    }

    // 2. bin/large list에서 asize에 맞는 블록 탐색
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    // 3. 못 찾으면 힙 확장 후 새 블록 할당 (Large 요청이면 asize만큼 확장)
    extendsize = (asize > CHUNKSIZE) ? asize : CHUNKSIZE;
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) return NULL;

    place(bp, asize);
    return bp;
}

/*
 * find_fit - bin/large list에서 asize 이상인 best-fit 블록 탐색
 * - BIN_MAX_SIZE 초과 요청은 Large List 먼저 탐색 (best-fit)
 * - 실패 시 일반 bin들도 탐색(fallback) — 안정성/재사용률 향상
 */

// first-fit
static void *find_fit(size_t asize)
{
    // 1) Large 요청이면 Large List에서 먼저 검색
    if (asize > BIN_MAX_SIZE) {
        void *bp = large_listp;
        while (bp) {
            size_t curr_size = GET_SIZE(HDRP(bp));
            if (curr_size >= asize)
                return bp;
            bp = SUCC(bp);
        }
    }

    // 2) 작은 요청은 해당 bin부터 큰 bin까지 first-fit 탐색
    int bin_start = find_bin(asize);
    for (int i = bin_start; i < BIN_COUNT; i++) {
        void *bp = bins[i].free_listp;
        while (bp) {
            size_t curr_size = GET_SIZE(HDRP(bp));
            if (curr_size >= asize)
                return bp;
            bp = SUCC(bp);
        }
    }
    
    // 3) 마지막으로 Large List도 한 번 더 확인
    void *bp = large_listp;
    while (bp) {
        size_t curr_size = GET_SIZE(HDRP(bp));
        if (curr_size >= asize)
            return bp;
        bp = SUCC(bp);
    }

    return NULL;
}

// best-fit
//  static void *find_fit(size_t asize)
// {
//     void *best = NULL;
//     size_t min_size = (size_t)-1;

//     // 1) Large 요청이면 Large List에서 먼저 검색
//     if (asize > BIN_MAX_SIZE) {
//         void *bp = large_listp;
//         while (bp) {
//             size_t curr_size = GET_SIZE(HDRP(bp));
//             if (curr_size >= asize && curr_size < min_size) {
//                 best = bp;
//                 min_size = curr_size;
//                 if (min_size == asize) break; // 완전 일치 시 조기종료
//             }
//             bp = SUCC(bp);
//         }
//         if (best) return best;
//     }

//     // 2) 작은 요청은 해당 bin부터 큰 bin까지 best-fit 탐색
//     int bin_start = find_bin(asize);
//     for (int i = bin_start; i < BIN_COUNT; i++) {
//         void *bp = bins[i].free_listp;
//         while (bp) {
//             size_t curr_size = GET_SIZE(HDRP(bp));
//             if (curr_size >= asize && curr_size < min_size) {
//                 best = bp;
//                 min_size = curr_size;
//                 if (min_size == asize) break; // 완전 일치
//             }
//             bp = SUCC(bp);
//         }
//         if (best) break;
//     }
    
//     // 3) 마지막으로 Large List도 한 번 더 확인 (작은 요청이라도 large 여유 블록이 있을 수 있음)
//     if (!best) {
//         void *bp = large_listp;
//         while (bp) {
//             size_t curr_size = GET_SIZE(HDRP(bp));
//             if (curr_size >= asize && curr_size < min_size) {
//                 best = bp;
//                 min_size = curr_size;
//                 if (min_size == asize) break;
//             }
//             bp = SUCC(bp);
//         }
//     }

//     return best;
// }


/*
 * place - bp 위치의 free 블록에 asize만큼 할당, 필요하면 분할해버림
 * - 엄격한 분할 정책 적용: 남은 크기가 MIN_BLOCK_SIZE(헤더+푸터+pred+succ) 이상일 때만 분할
 * - 너무 작은 블록(헤더+푸터+pred+succ+payload)이 생기지 않도록 방지
 */
static void place(void *bp, size_t asize)
{
    size_t totalsize = GET_SIZE(HDRP(bp)); // 현재 가용 블록의 전체 크기

    delete_free_block(bp); // 현재 속한 리스트(bin 또는 large)에서 제거

    // 분할 정책: 남은 크기가 MIN_BLOCK_SIZE 이상이어야 분할
    if ((totalsize - asize) >= MIN_BLOCK_SIZE)
    {
        // 블록 분할
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));

        char *next_bp = NEXT_BLKP(bp); // 남은 영역의 다음 블록 payload 주소
        PUT(HDRP(next_bp), PACK(totalsize - asize, 0)); // 남은 영역 헤더: 남은 크기, free
        PUT(FTRP(next_bp), PACK(totalsize - asize, 0)); // 남은 영역 푸터: 남은 크기, free

        insert_free_block(next_bp); // 크기에 맞는 bin 또는 large list에 삽입

    }
    else
    {
        // 그냥 전부 할당
        PUT(HDRP(bp), PACK(totalsize, 1));
        PUT(FTRP(bp), PACK(totalsize, 1));
    }
}

/*
 * coalesce - 인접 free 블록을 병합. 병합된 블록의 payload 포인터 반환
 * 병합된 블록은 bin/large list에 재삽입
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 이전 블록 할당 여부
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 다음 블록 할당 여부
    size_t size = GET_SIZE(HDRP(bp));                   // 현재 블록 크기

    if (prev_alloc && next_alloc)
    {
        insert_free_block(bp);
        return bp;
    }
    else if (prev_alloc && !next_alloc)
    {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        delete_free_block(NEXT_BLKP(bp));

        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        insert_free_block(bp);
    }
    else if (!prev_alloc && next_alloc)
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
        delete_free_block(bp);

        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        insert_free_block(bp);
    }
    else
    {
        size += ( GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp))) );
        delete_free_block(NEXT_BLKP(bp));
        bp = PREV_BLKP(bp);
        delete_free_block(bp);

        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        insert_free_block(bp);
    }

    return bp;
}

/*
 * mm_free - 블록을 해제하고 인접 free 블록과 병합
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

/*
 * mm_realloc - 블록 크기 재조정 (새 블록 할당, 데이터 복사, 기존 블록 해제)
 * 병합 확장, 분할 모두 bin/large list별 관리
 */
void *mm_realloc(void *ptr, size_t size) 
{
    if (ptr == NULL) return mm_malloc(size);

    if (size == 0) 
    { 
        mm_free(ptr); 
        return NULL; 
    }

    size_t old_size = GET_SIZE(HDRP(ptr));
    size_t asize = (size <= DSIZE) ? 2 * DSIZE : ALIGN(size + DSIZE);

    // 축소
    if (asize < old_size && (old_size - asize) >= MIN_BLOCK_SIZE) 
    {
        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1));

        char *next_blk = NEXT_BLKP(ptr);
        PUT(HDRP(next_blk), PACK(old_size - asize, 0));
        PUT(FTRP(next_blk), PACK(old_size - asize, 0));
        insert_free_block(next_blk);

        return ptr;
    }

    void *prev_blk = PREV_BLKP(ptr);
    size_t prev_alloc = GET_ALLOC(FTRP(prev_blk));
    size_t prev_size = GET_SIZE(HDRP(prev_blk));

    void *next_blk = NEXT_BLKP(ptr);
    size_t next_alloc = GET_ALLOC(HDRP(next_blk));
    size_t next_size = GET_SIZE(HDRP(next_blk));

    // 1. next block만으로 확장
    if (!next_alloc && (old_size + next_size) >= asize) 
    {
        delete_free_block(next_blk);
        size_t combined_size = old_size + next_size;

        if ((combined_size - asize) >= MIN_BLOCK_SIZE) 
        {
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            char *next_new_blk = NEXT_BLKP(ptr);
            PUT(HDRP(next_new_blk), PACK(combined_size - asize, 0));
            PUT(FTRP(next_new_blk), PACK(combined_size - asize, 0));
            insert_free_block(next_new_blk);
        } 
        else 
        {
            PUT(HDRP(ptr), PACK(combined_size, 1));
            PUT(FTRP(ptr), PACK(combined_size, 1));
        }
        return ptr;
    }

    // 2. prev block만으로 확장
    if (!prev_alloc && (prev_size + old_size) >= asize) 
    {
        delete_free_block(prev_blk);
        size_t combined_size = prev_size + old_size;

        // payload 복사 시 헤더/푸터 제외
        size_t copy_n = (old_size - DSIZE < size) ? (old_size - DSIZE) : size;
        memmove(prev_blk, ptr, copy_n);

        if ((combined_size - asize) >= MIN_BLOCK_SIZE) 
        {
            PUT(HDRP(prev_blk), PACK(asize, 1));
            PUT(FTRP(prev_blk), PACK(asize, 1));
            char *next_new_blk = NEXT_BLKP(prev_blk);
            PUT(HDRP(next_new_blk), PACK(combined_size - asize, 0));
            PUT(FTRP(next_new_blk), PACK(combined_size - asize, 0));
            insert_free_block(next_new_blk);
        } 
        else 
        {
            PUT(HDRP(prev_blk), PACK(combined_size, 1));
            PUT(FTRP(prev_blk), PACK(combined_size, 1));
        }
        return prev_blk;
    }

    // 3. prev + next block 모두 free라면 3개 병합
    if (!prev_alloc && !next_alloc && (prev_size + old_size + next_size) >= asize) {
        delete_free_block(prev_blk);
        delete_free_block(next_blk);
        size_t combined_size = prev_size + old_size + next_size;

        size_t copy_n = (old_size - DSIZE < size) ? (old_size - DSIZE) : size;
        memmove(prev_blk, ptr, copy_n);

        if ((combined_size - asize) >= MIN_BLOCK_SIZE) 
        {
            PUT(HDRP(prev_blk), PACK(asize, 1));
            PUT(FTRP(prev_blk), PACK(asize, 1));
            char *next_new_blk = NEXT_BLKP(prev_blk);
            PUT(HDRP(next_new_blk), PACK(combined_size - asize, 0));
            PUT(FTRP(next_new_blk), PACK(combined_size - asize, 0));
            insert_free_block(next_new_blk);
        } 
        else 
        {
            PUT(HDRP(prev_blk), PACK(combined_size, 1));
            PUT(FTRP(prev_blk), PACK(combined_size, 1));
        }
        return prev_blk;
    }

    // 4. 확장 불가: 새로 할당
    void *newptr = mm_malloc(size);

    if (newptr == NULL) return NULL;

    size_t copySize = (old_size - DSIZE < size) ? (old_size - DSIZE) : size;
    memcpy(newptr, ptr, copySize);
    mm_free(ptr);

    return newptr;
}