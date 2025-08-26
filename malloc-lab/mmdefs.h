#include <stdio.h>

/* 상수 정의 */
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)

/* 유틸리티 매크로 */
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))

/* 메모리 접근 매크로 */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* 블록 정보 추출 매크로 */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* 블록 포인터 연산 매크로 */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* 인접 블록 포인터 연산 매크로 */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))