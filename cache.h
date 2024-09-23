#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>
#include <pthread.h>

// 캐시 항목 구조체
typedef struct cache_entry {
    char *url;                  // 요청 URL
    char *response;             // 서버로부터 받은 응답 데이터
    size_t response_size;       // 응답 데이터 크기
    struct cache_entry *prev;   // 이중 연결 리스트 이전 항목
    struct cache_entry *next;   // 이중 연결 리스트 다음 항목
} cache_entry_t;

// 캐시 구조체
typedef struct cache {
    cache_entry_t *hash_table[1024]; // 해시 테이블
    cache_entry_t *head;             // LRU 리스트의 헤드 (가장 최근 사용)
    cache_entry_t *tail;             // LRU 리스트의 꼬리 (가장 오랫동안 사용되지 않음)
    size_t total_size;               // 현재 캐시 총 크기
    pthread_mutex_t lock;            // 동기화를 위한 뮤텍스
} cache_t;

extern cache_t cache;
// 함수 선언
void cache_init(cache_t *cache);
unsigned int hash_function(const char *url);
cache_entry_t* cache_lookup(cache_t *cache, const char *url);
void cache_insert(cache_t *cache, const char *url, const char *response, size_t response_size);
void cache_free(cache_t *cache); // 캐시 전체를 해제하는 함수 (추가)

#endif