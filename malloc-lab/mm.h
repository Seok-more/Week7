///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define WSIZE       sizeof(size_t)       // 워드(헤더/푸터)의 크기. 시스템에 따라 4 또는 8바이트로 자동 설정됨
#define DSIZE       (2 * WSIZE)          // 더블워드
#define CHUNKSIZE   (1<<12)              // 힙을 한번 확장하는데 쓰는 크기 2^12 바이트임

#define MAX(x, y)   ((x) > (y)? (x) : (y)) // 두 값 중 큰 값을 반환

#define PACK(size, alloc)   ((size) | (alloc)) // 사이즈와 할당여부를 하나의 워드로 합침: size는 8의 배수로 맞춤(하위 3비트가 0임) 
                                               // -> 할당 여부(0x1/0x0)를 LSB에 저장해도 크기 정보가 안겹치니까 합칠 수 있음

#define GET(p)      (*(size_t *)(p))           // 주소 p에 저장된 size_t 값을 읽음(헤더/푸터 읽기)
#define PUT(p, val) (*(size_t *)(p) = (val))   // 주소 p에 size_t 값인 val을 저장(헤더/푸터 쓰기)

#define GET_SIZE(p)     (GET(p) & ~0x7)      // 주소 p에 저장된 값에서 블록 크기만 추출(하위 3비트 제거), ~0x7은 1111...1000 (LSB 3비트만 0, 나머지 1) 이거랑 and해서 하위 3비트 제거임
#define GET_ALLOC(p)    (GET(p) & 0x1)       // 주소 p에 저장된 값에서 할당 여부(LSB) 추출, LSB만 남겨서 1이면 allocated, 0이면 free

#define HDRP(bp)    ((char *)(bp) - WSIZE)                      // 블록 포인터(bp)에서 헤더 주소 계산
#define FTRP(bp)    ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // bp에서 푸터 주소 계산

#define NEXT_BLKP(bp)   ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) // bp 기준 다음 블록 포인터 계산
#define PREV_BLKP(bp)   ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) // bp 기준 이전 블록 포인터 계산

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <stdio.h>

extern int mm_init (void);
extern void *mm_malloc (size_t size);
extern void mm_free (void *ptr);
extern void *mm_realloc(void *ptr, size_t size);


/* 
 * Students work in teams of one or two.  Teams enter their team name, 
 * personal names and login IDs in a struct of this
 * type in their bits.c file.
 */
typedef struct {
    char *teamname; /* ID1+ID2 or ID1 */
    char *name1;    /* full name of first member */
    char *id1;      /* login ID of first member */
    char *name2;    /* full name of second member (if any) */
    char *id2;      /* login ID of second member */
} team_t;

extern team_t team;

