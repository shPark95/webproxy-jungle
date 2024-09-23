#include "cache.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// 해시 함수 구현
unsigned int hash_function(const char *url) {
    unsigned int hash = 5381;
    int c;
    while ((c = *url++))
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    return hash % 1024;
}

// 캐시 초기화 함수
void cache_init(cache_t *cache) {
    memset(cache->hash_table, 0, sizeof(cache->hash_table));
    cache->head = NULL;
    cache->tail = NULL;
    cache->total_size = 0;
    pthread_mutex_init(&cache->lock, NULL);
}

// 캐시 조회 함수
cache_entry_t* cache_lookup(cache_t *cache, const char *url) {
    unsigned int idx = hash_function(url);
    cache_entry_t *entry = cache->hash_table[idx];
    while (entry) {
        if (strcmp(entry->url, url) == 0) {
            // LRU 업데이트: 가장 최근 사용된 항목으로 이동
            if (entry != cache->head) {
                // 연결 리스트에서 제거
                if (entry->prev)
                    entry->prev->next = entry->next;
                if (entry->next)
                    entry->next->prev = entry->prev;
                if (entry == cache->tail)
                    cache->tail = entry->prev;

                // 헤드로 이동
                entry->next = cache->head;
                entry->prev = NULL;
                if (cache->head)
                    cache->head->prev = entry;
                cache->head = entry;
            }
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

// 캐시 삽입 함수
void cache_insert(cache_t *cache, const char *url, const char *response, size_t response_size) {
    if (response_size > MAX_OBJECT_SIZE)
        return; // 캐시에 저장할 수 없는 크기

    // 캐시 공간 확보 (필요 시 LRU 항목 제거)
    while (cache->total_size + response_size > MAX_CACHE_SIZE) {
        if (cache->tail == NULL)
            break; // 캐시가 비어있지 않지만 공간이 부족할 때

        // 해시 테이블에서 제거
        unsigned int idx = hash_function(cache->tail->url);
        cache_entry_t *to_remove = cache->tail;
        if (to_remove->prev)
            to_remove->prev->next = NULL;
        cache->tail = to_remove->prev;

        // 해시 테이블에서 항목 제거
        cache_entry_t *current = cache->hash_table[idx];
        cache_entry_t *prev = NULL;
        while (current) {
            if (strcmp(current->url, to_remove->url) == 0) {
                if (prev)
                    prev->next = current->next;
                else
                    cache->hash_table[idx] = current->next;
                break;
            }
            prev = current;
            current = current->next;
        }

        // 메모리 해제
        cache->total_size -= to_remove->response_size;
        free(to_remove->url);
        free(to_remove->response);
        free(to_remove);
    }

    // 새 캐시 항목 생성
    cache_entry_t *new_entry = malloc(sizeof(cache_entry_t));
    if (!new_entry) {
        fprintf(stderr, "Failed to allocate memory for cache entry\n");
        return;
    }
    new_entry->url = strdup(url);
    new_entry->response = malloc(response_size);
    if (!new_entry->url || !new_entry->response) {
        free(new_entry->url);
        free(new_entry->response);
        free(new_entry);
        fprintf(stderr, "Failed to allocate memory for cache entry data\n");
        return;
    }
    memcpy(new_entry->response, response, response_size);
    new_entry->response_size = response_size;
    new_entry->prev = NULL;
    new_entry->next = cache->head;

    if (cache->head)
        cache->head->prev = new_entry;
    cache->head = new_entry;
    if (cache->tail == NULL)
        cache->tail = new_entry;

    // 해시 테이블에 삽입
    unsigned int idx = hash_function(url);
    new_entry->next = cache->hash_table[idx];
    cache->hash_table[idx] = new_entry;

    cache->total_size += response_size;
}

// 캐시 전체 해제 함수 (추가)
void cache_free(cache_t *cache) {
    for (int i = 0; i < 1024; i++) {
        cache_entry_t *entry = cache->hash_table[i];
        while (entry) {
            cache_entry_t *next = entry->next;
            free(entry->url);
            free(entry->response);
            free(entry);
            entry = next;
        }
        cache->hash_table[i] = NULL;
    }
    cache->head = NULL;
    cache->tail = NULL;
    cache->total_size = 0;
    pthread_mutex_destroy(&cache->lock);
}