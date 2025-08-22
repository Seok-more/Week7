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
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))


/////////////////////////////////
char *heap_listp;
void *extend_heap(size_t words);
void *coalesce(void *bp);
void *find_fit(size_t asize);
void place(void *bp, size_t asize);
/////////////////////////////////


/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) // Implicit free list
{
    // 1. 초기 빈 힙을 생성 (4워드를 메모리 할당) 
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void*)-1)
    {
        return -1;
    }

    // 2. 빈 힙에 프롤로그 블록과 에필로그 헤더를 추가 -> 프롤로그 불록은 힙의 맨 앞부분에 존재하는 allocated된 더미 블록
    // 초기 힙 구조 : [정렬 패딩][프롤로그 헤더][프롤로그 푸터][첫 가용 블록 ...][에필로그 헤더]
    //              ↑         ↑            ↑            ↑               ↑
    //        heap_listp heap_listp+WSIZE heap_listp+2*WSIZE heap_listp+3*WSIZE ...                   
    PUT(heap_listp, 0);                          // 정렬 패딩 (Alignment padding) 
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // 프롤로그 헤더 (할당된 8바이트) 
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // 프롤로그 푸터 (할당된 8바이트) 
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     // 에필로그 헤더 (크기 0, 할당됨) 

    // heap_listp += (WSIZE); -> 이건 프롤로그푸터를 가리켜서 그 다음 주소가 첫 가용 블록의 헤더임
    heap_listp += (2*WSIZE); // heap_listp를 첫 가용 블록의 payload 주소로 이동(보통 payload 기준으로 블록포인터 잡음)

    // 2. 빈 힙을 CHUNKSIZE 크기의 가용 블록으로 확장 
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
    {
        return -1; 
    }
            
    return 0; 
}

/*
 * extend_heap - Extend the heap with a new free block and return its block pointer.
 */

// 테스트 용으로 static 없앴다 나중에 붙이자
void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    // 8바이트(WSIZE) 단위로 정렬, 항상 짝수 워드 할당 (블록 정렬 유지)
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE; // (words + 1) * WSIZE -> 짝수로 올림, words * WSIZE -> 그대로 워드 단위

    // 새 메모리 할당
    if ((long)(bp = mem_sbrk(size)) == -1)
    {
        return NULL;
    }
            
    // 새 가용 블록의 헤더와 푸터 초기화
    PUT(HDRP(bp), PACK(size, 0));         // 헤더: size, free
    PUT(FTRP(bp), PACK(size, 0));         // 푸터: size, free

    // 새 에필로그 헤더 초기화
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // 에필로그 헤더: size 0, alloc

    // 테스트
    // printf("[extend_heap] bp=%p, size=%zu\n", bp, size);

    // coalesce 아직 구현안했으면
    // return bp

    // 이전 블록이 free라면 합쳐버림
    bp = coalesce(HDRP(bp));

    // 항상 힙의 끝에 에필로그 헤더 재설정
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return bp;
}



/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t Realsize;         // 실제 할당할 블록 크기 (헤더/푸터 포함, 정렬)
    size_t extendsize;    // fit이 없을 때 힙 확장 크기
    char *bp;

    if (size == 0) return NULL;

    // 1. 최소 블록 크기(헤더+payload+푸터) 맞추고 8바이트 단위로 정렬
    if (size <= DSIZE)
    {
        Realsize = 2 * DSIZE; // 최소 16바이트
    }
    else
    {
        Realsize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); // (size + DSIZE + (DSIZE-1)) / DSIZE -> 정렬 올림
    }
        
    // 2. 가용 리스트에서 asize만큼 맞는 블록 탐색
    bp = find_fit(Realsize);
    if (bp != NULL) 
    {
        place(bp, Realsize);    // 찾으면 그 블록에 Realsize만큼 할당
        return bp + WSIZE;           // payload 주소 리턴
    }

    // 3. 못 찾으면 힙 확장 후 새 블록 할당
    extendsize = MAX(Realsize, CHUNKSIZE);
    bp = extend_heap(extendsize / WSIZE); // 워드 단위로 힙 확장
    if (bp == NULL)
    {
        return NULL;
    }
        
    place(bp, Realsize);
    return bp + WSIZE;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    // printf("mm_free called with ptr=%p, heap_listp=%p\n", bp, heap_listp);

    // 블록의 헤더, 푸터의 위치를 정확히 조절하기 위해서는 헤더 기준으로 함
    char *bp_header = (char *)bp - WSIZE; // 헤더 위치로 변환
    size_t size = GET_SIZE(HDRP(bp_header));
    PUT(HDRP(bp_header), PACK(size, 0));       // 헤더: 크기, free (0)
    PUT(FTRP(bp_header), PACK(size, 0));       // 푸터: 크기, free (0)
    coalesce(bp_header);
}

/*
 * coalesce - Coalesce adjacent free blocks. // 132 쪽 그림 9.40
 */

// 테스트 용으로 static 없앴다 나중에 붙이자
void *coalesce(void *bp_header)
{
    if (bp_header == NULL) 
    {
        // printf("[coalesce] bp is NULL, Return.\n");
        return bp_header;
    }

    // 힙의 시작/끝 주소
    char *heap_start = heap_listp;
    char *heap_end = (char *)mem_sbrk(0);

    // printf("[coalesce] bp=%p\n", bp);

    char *prev_bp_header = PREV_BLKP(bp_header);
    char *next_bp_header = NEXT_BLKP(bp_header);

    // printf("[coalesce] prev_bp=%p (PREV_BLKP(bp)), next_bp=%p (NEXT_BLKP(bp))\n", prev_bp, next_bp);
    // printf("[coalesce] heap_listp=%p, heap_end=%p\n", heap_start, heap_end);

    // prev_bp가 힙 바깥이면 바로 return
    if (prev_bp_header < heap_start || prev_bp_header >= heap_end) 
    {
        // printf("[coalesce] prev_bp out of heap range, Return bp\n");
        return bp_header;
    }

    // prev_bp가 블록 크기 0이면 return
    size_t prev_hdr_val = GET(HDRP(prev_bp_header));
    size_t prev_size = GET_SIZE(HDRP(prev_bp_header));
    if (prev_size == 0) 
    {
        // printf("[coalesce] prev_bp has size 0, Return bp\n");
        return bp_header;
    }
    //printf("[coalesce] prev_bp HDRP(prev_bp)=%p, prev_hdr_val=0x%lx, prev_size=%zu\n", HDRP(prev_bp), prev_hdr_val, prev_size);

    // prev_bp의 푸터값
    size_t prev_footer_val = GET(FTRP(prev_bp_header));
    //printf("[coalesce] prev_bp FTRP(prev_bp)=%p, prev_footer_val=0x%lx\n", FTRP(prev_bp), prev_footer_val);

    // 현재 블록 헤더값
    size_t cur_hdr_val = GET(HDRP(bp_header));
    size_t size = GET_SIZE(HDRP(bp_header));
    //printf("[coalesce] HDRP(bp)=%p, cur_hdr_val=0x%lx, size=%zu\n", HDRP(bp), cur_hdr_val, size);

    // 다음 블록 헤더값
    size_t next_hdr_val = GET(HDRP(next_bp_header));
    size_t next_size = GET_SIZE(HDRP(next_bp_header));
    //printf("[coalesce] next_bp HDRP(next_bp)=%p, next_hdr_val=0x%lx, next_size=%zu\n", HDRP(next_bp), next_hdr_val, next_size);

    // 할당 여부
    size_t prev_alloc = 1;
    if (bp_header == heap_start) 
    {
        //printf("[coalesce] bp == heap_listp, prev_alloc=1\n");
        prev_alloc = 1;
    } 
    else 
    {
        prev_alloc = GET_ALLOC(FTRP(prev_bp_header));
        //printf("[coalesce] prev_alloc=%zu\n", prev_alloc);
    }
    size_t next_alloc = GET_ALLOC(HDRP(next_bp_header));
    //printf("[coalesce] next_alloc=%zu\n", next_alloc);


    // Case 1: 둘 다 할당됨
    if (prev_alloc && next_alloc) 
    {
        //printf("[coalesce] Case 1, return bp\n");
        return bp_header;
    }
    // Case 2: 다음만 free
    else if (prev_alloc && !next_alloc) 
    {
        //printf("[coalesce] Case 2 \n");
        size += next_size;
        PUT(HDRP(bp_header), PACK(size, 0));
        PUT(FTRP(bp_header), PACK(size, 0));
        PUT(HDRP(NEXT_BLKP(bp_header)), PACK(0, 1));
        //printf("[coalesce2] after merge: bp=%p, HDRP(bp)=%p, FTRP(bp)=%p, size=%zu\n",bp, HDRP(bp), FTRP(bp), GET_SIZE(HDRP(bp)));
        //printf("[coalesce2] next_hdr=%lx at %p\n", GET(HDRP(NEXT_BLKP(bp))), HDRP(NEXT_BLKP(bp)));
    }
    // Case 3: 이전만 free
    else if (!prev_alloc && next_alloc) 
    {
        //printf("[coalesce] Case 3 \n");
        size += prev_size;
        bp_header = prev_bp_header;
        PUT(HDRP(bp_header), PACK(size, 0));
        PUT(FTRP(bp_header), PACK(size, 0));
        PUT(HDRP(NEXT_BLKP(bp_header)), PACK(0, 1));
        //printf("[coalesce3] after merge: bp=%p, HDRP(bp)=%p, FTRP(bp)=%p, size=%zu\n",bp, HDRP(bp), FTRP(bp), GET_SIZE(HDRP(bp)));
        //printf("[coalesce3] next_hdr=%lx at %p\n", GET(HDRP(NEXT_BLKP(bp))), HDRP(NEXT_BLKP(bp)));
    }
    // Case 4: 이전, 다음 모두 free
    else 
    {
        //printf("[coalesce] Case 4: \n");
        size += prev_size + next_size;
        bp_header = prev_bp_header;
        PUT(HDRP(bp_header), PACK(size, 0));
        PUT(FTRP(bp_header), PACK(size, 0));
        PUT(HDRP(NEXT_BLKP(bp_header)), PACK(0, 1));
        //printf("[coalesce4] after merge: bp=%p, HDRP(bp)=%p, FTRP(bp)=%p, size=%zu\n", bp, HDRP(bp), FTRP(bp), GET_SIZE(HDRP(bp)));
        //printf("[coalesce4] next_hdr=%lx at %p\n", GET(HDRP(NEXT_BLKP(bp))), HDRP(NEXT_BLKP(bp)));
    }
    return bp_header;
}

/*
 * find_fit - Find a fit block for the given size.
 */
// 가용 블록 리스트에서 asize 크기 이상의 블록을 찾아 반환
void *find_fit(size_t Realsize) 
{
    // 가용 리스트를 순회하며 블록을 찾아야 함
    // 1. 첫 블록부터 끝까지 순회 (implicit free list)

    char *bp = heap_listp;
    while (GET_SIZE(HDRP(bp)) > 0) 
    {
        if (!GET_ALLOC(HDRP(bp)) && (GET_SIZE(HDRP(bp)) >= Realsize)) 
        {
            return bp;
        }
        bp = NEXT_BLKP(bp);
    }
    return NULL;
}

/*
 * place - Place the block of the given size at the start of the free block.
 */

// 블록 bp에 asize만큼 할당하고, 필요하면 블록을 분할
void place(void *bp, size_t Realsize) 
{
    size_t totalsize = GET_SIZE(HDRP(bp)); // 현재 가용 블록의 전체 크기
    if ((totalsize - Realsize) >= (2 * DSIZE)) 
    {
        // 블록 분할
        PUT(HDRP(bp), PACK(Realsize, 1));
        PUT(FTRP(bp), PACK(Realsize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(totalsize - Realsize, 0));
        PUT(FTRP(bp), PACK(totalsize - Realsize, 0));
    } 
    else 
    {
        // 그냥 전부 할당
        PUT(HDRP(bp), PACK(totalsize, 1));
        PUT(FTRP(bp), PACK(totalsize, 1));
    }
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}