#include <stdio.h>
#include "mm.h"
#include "memlib.h"

extern char *heap_listp;
void *extend_heap(size_t words);
void *coalesce(void *bp);

// 블록 정보를 보기 쉽게 출력
void print_block_info(const char *msg, void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    printf("%s\n", msg);
    printf("  bp=%p\n", bp);
    printf("  헤더=0x%lx, 푸터=0x%lx, 사이즈=%lu, 할당=%lu\n",
        (unsigned long)GET(HDRP(bp)),
        (unsigned long)GET(FTRP(bp)),
        (unsigned long)size,
        (unsigned long)GET_ALLOC(HDRP(bp)));
}

//MM 테스트 메인

// int main() {
//     mem_init(); // 반드시 먼저 호출

//     // 1. mm_init 테스트
//     printf("===== [1] mm_init() 테스트 =====\n");
//     if (mm_init() == -1) {
//         printf("mm_init 실패\n");
//         return 1;
//     }
//     printf("mm_init 성공 : heap_listp = %p\n", heap_listp);

//     // 2. extend_heap 테스트
//     printf("\n===== [2] extend_heap() 테스트 =====\n");
//     void *bp_ext = extend_heap(4);
//     if (bp_ext == NULL) {
//         printf("extend_heap 실패\n");
//         return 1;
//     }
//     printf("extend_heap 성공 : bp = %p\n", bp_ext);
//     print_block_info("extend_heap 블록 정보", bp_ext);

//     // 3. mm_malloc, find_fit, place 테스트
//     printf("\n===== [3] mm_malloc(), find_fit(), place() 테스트 =====\n");
//     size_t alloc_size1 = 16; // 최소 블록 크기
//     size_t alloc_size2 = 32;
//     void *p1 = mm_malloc(alloc_size1);
//     void *p2 = mm_malloc(alloc_size2);

//     if (p1 == NULL || p2 == NULL) {
//         printf("mm_malloc 실패\n");
//         return 1;
//     }
//     printf("mm_malloc 성공: p1=%p, p2=%p\n", p1, p2);
//     print_block_info("p1 블록 정보", (char*)p1 - WSIZE);
//     print_block_info("p2 블록 정보", (char*)p2 - WSIZE);

//     // find_fit 직접 확인
//     void *fit = find_fit(alloc_size1);
//     printf("find_fit(%zu): bp=%p\n", alloc_size1, fit);

//     // place 테스트: 남은 큰 free 블록에 직접 할당
//     void *free_bp = extend_heap(8);
//     printf("추가 free 블록(bp=%p)에 place(24) 적용\n", free_bp);
//     place(free_bp, 24);
//     print_block_info("place 이후 블록 정보", free_bp);

//     // 4. mm_free, coalesce 테스트
//     printf("\n===== [4] mm_free(), coalesce() 테스트 =====\n");
//     // 할당한 블록 해제 후 coalesce 체크
//     printf("p1(%p) free 전\n", p1);
//     print_block_info("p1 블록 정보", (char*)p1 - WSIZE);
//     mm_free(p1);
//     printf("p1(%p) free 후\n", p1);
//     print_block_info("p1 블록 정보", (char*)p1 - WSIZE);

//     // 여러 블록 free 후 병합 확인
//     void *bp_a = mm_malloc(32);
//     void *bp_b = mm_malloc(40);
//     void *bp_c = mm_malloc(48);
//     printf("연속 블록 bp_a=%p, bp_b=%p, bp_c=%p\n", bp_a, bp_b, bp_c);
//     mm_free(bp_b);
//     mm_free(bp_a);
//     // bp_a, bp_b 병합되어야 함
//     print_block_info("병합 후 (bp_a)", (char*)bp_a - WSIZE);

//     mm_free(bp_c); // 양옆 모두 free 병합
//     print_block_info("병합 후 (bp_a)", (char*)bp_a - WSIZE);

//     mem_deinit(); // 반드시 해제
//     return 0;
// }