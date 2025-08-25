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
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_free_block(void *bp);   
static void delete_free_block(void *bp); 
/////////////////////////////////


/*
 *  insert_free_block - free 블록을 가용 리스트에 추가, address order가 더 좋대
 */

static void insert_free_block(void *bp)
{
    void *prev = NULL;
    void *now = free_listp;

    // 주소 오름차순으로 탐색
    while(now != NULL && (char*)now < (char*)bp)
    {
        prev = now;
        now = SUCC(now);
    }

    // prev와 now 사이에 bp 삽입
    PRED(bp) = prev;
    SUCC(bp) = now;

    // bp가 맨끝에 들어가는게 아니라면
    if (now != NULL)
    {
        PRED(now) = bp;
    }
    
    // bp가 맨 앞에 들어가는게 아니라면
    if (prev != NULL)
    {
        SUCC(prev) = bp;
    }

    // bp가 맨 앞에 들어감
    else 
    {
        free_listp = bp;
    }

}

/*
 *  delete_free_block - free 블록을 가용 리스트에서 제거
 */

static void delete_free_block(void *bp)
{
    // 삭제할 노드가 첫 노드
    if (bp == free_listp)
    {
        free_listp = SUCC(bp);
        
        // 리스트에 bp만 있는게 아니였다면
        if (free_listp != NULL)
        {
            PRED(free_listp) = NULL;
        }
    }
    // 삭제할 노드가 중간이나 마지막
    else
    {
        // 사실 이건 없어도 되는데 그냥 가시성 용으로
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
    free_listp = NULL; // 초기화를 해야지

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
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;         // 실제 할당 블록 크기 (헤더/푸터 포함, 정렬)
    size_t extendsize;    // fit이 없을 때 힙 확장 크기
    char *bp;

    if (size == 0) return NULL;

    // 1. 최소 블록 크기(헤더+payload+푸터) 맞추고 8바이트 단위로 정렬
    if (size <= DSIZE)
    {
        asize = 2 * DSIZE; // 최소 16바이트
    }
    else
    {
        asize = ALIGN(size + DSIZE); // Header + payload + footer
    }

    // 2. 가용 리스트에서 asize만큼 맞는 블록 탐색
    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize);    // 찾으면 그 블록에 asize만큼 할당
        return bp;           // payload 주소 리턴
    }

    // 3. 못 찾으면 힙 확장 후 새 블록 할당
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) return NULL;

    place(bp, asize);
    return bp;
}

/*
 * find_fit - asize 크기 이상의 가용 블록을 찾아서 payload 포인터 반환
 */
// best-fit 방삭
static void *find_fit(size_t asize) 
{
    char *bp = free_listp;
    char *best = NULL;
    size_t min_size = (size_t)-1;

    while (bp) 
    {
        size_t curr_size = GET_SIZE(HDRP(bp));
        if (curr_size >= asize && curr_size < min_size) 
        {
            best = bp;
            min_size = curr_size;
        }
        bp = SUCC(bp);
    }
    return best;
}

/*
 * place - bp 위치의 free 블록에 asize만큼 할당, 필요하면 분할해버림
 */
static void place(void *bp, size_t asize)
{
    size_t totalsize = GET_SIZE(HDRP(bp)); // 현재 가용 블록의 전체 크기

    delete_free_block(bp);

    // 남은 크기가 최소 블록 크기(헤더+payload+푸터 = 16바이트) 이상이면 분할
    if ((totalsize - asize) >= (2 * DSIZE))
    {
        // 블록 분할
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));


        char *next_bp = NEXT_BLKP(bp); // 남은 영역의 다음 블록 payload 주소
        PUT(HDRP(next_bp), PACK(totalsize - asize, 0)); // 남은 영역 헤더: 남은 크기, free해버림
        PUT(FTRP(next_bp), PACK(totalsize - asize, 0)); // 남은 영역 푸터: 남은 크기, free해버림

        insert_free_block(next_bp);

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
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 이전 블록 할당 여부
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 다음 블록 할당 여부
    size_t size = GET_SIZE(HDRP(bp));                   // 현재 블록 크기

    // Case 1: 이전/다음 모두 할당됨
    if (prev_alloc && next_alloc)
    {
        insert_free_block(bp); // 새로운 free 블록 리스트에 추가
        return bp;
    }

    // Case 2: 이전 할당, 다음 free
    else if (prev_alloc && !next_alloc)
    {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        delete_free_block(NEXT_BLKP(bp)); // 다음블록은 이제 없는거

        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        insert_free_block(bp);
    }

    // Case 3: 이전 free, 다음 할당
    else if (!prev_alloc && next_alloc)
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
        delete_free_block(bp);

        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        insert_free_block(bp);
    }

    // Case 4: 이전/다음 모두 free
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
 */

 // realloc in-place 확장
void *mm_realloc(void *ptr, size_t size) 
{
    if (ptr == NULL) return mm_malloc(size); // 그냥 그대로 해줘야함

    if (size == 0) 
    { 
        mm_free(ptr); 
        return NULL; 
    }

    size_t old_size = GET_SIZE(HDRP(ptr));
    size_t asize = (size <= DSIZE) ? 2 * DSIZE : ALIGN(size + DSIZE);

    // 축소
    if (asize < old_size && (old_size - asize) >= (2 * DSIZE)) 
    {
        // 앞부분은 asize만큼 할당
        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1));

        // 뒷부분은 새로운 free block
        char *next_blk = NEXT_BLKP(ptr);
        PUT(HDRP(next_blk), PACK(old_size - asize, 0));
        PUT(FTRP(next_blk), PACK(old_size - asize, 0));
        insert_free_block(next_blk);

        // 기존 ptr 반환
        return ptr;
    }


    // 확장 : 앞에만, 뒤에만, 앞뒤전부, 혼자
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

        // 분할 가능하면 분할
        if ((combined_size - asize) >= (2 * DSIZE)) 
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

        // 데이터 이동 (payload 복사)
        memmove(prev_blk, ptr, (old_size - DSIZE < size) ? (old_size - DSIZE) : size);

        // 분할 가능하면 분할
        if ((combined_size - asize) >= (2 * DSIZE)) 
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

        // 데이터 이동
        memmove(prev_blk, ptr, (old_size - DSIZE < size) ? (old_size - DSIZE) : size);

        // 분할 가능하면 분할
        if ((combined_size - asize) >= (2 * DSIZE)) 
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