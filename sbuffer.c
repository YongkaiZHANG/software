#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include "config.h"
#include "sbuffer.h"

#if (SBUFFER_FULL_POLICY != SBUFFER_FULL_BLOCK) && \
    (SBUFFER_FULL_POLICY != SBUFFER_FULL_DROP_NEWEST) && \
    (SBUFFER_FULL_POLICY != SBUFFER_FULL_DROP_OLDEST)
#error Unsupported SBUFFER_FULL_POLICY
#endif

int sbuffer_init(sbuffer_t **buffer)
{
    pthread_mutexattr_t mattr;
    pthread_condattr_t cattr;

    *buffer = mmap(NULL, sizeof(sbuffer_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (*buffer == MAP_FAILED) {
        *buffer = NULL;
        return SBUFFER_FAILURE;
    }

    memset(*buffer, 0, sizeof(sbuffer_t));
    (*buffer)->capacity = SBUFFER_CAPACITY;

    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    if (pthread_mutex_init(&(*buffer)->mutex, &mattr) != 0) {
        munmap(*buffer, sizeof(sbuffer_t));
        *buffer = NULL;
        return SBUFFER_FAILURE;
    }

    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    
    if (pthread_cond_init(&(*buffer)->not_empty, &cattr) != 0) {
        pthread_mutex_destroy(&(*buffer)->mutex);
        munmap(*buffer, sizeof(sbuffer_t));
        *buffer = NULL;
        return SBUFFER_FAILURE;
    }
    if (pthread_cond_init(&(*buffer)->not_full, &cattr) != 0) {
        pthread_cond_destroy(&(*buffer)->not_empty);
        pthread_mutex_destroy(&(*buffer)->mutex);
        munmap(*buffer, sizeof(sbuffer_t));
        *buffer = NULL;
        return SBUFFER_FAILURE;
    }

    pthread_mutexattr_destroy(&mattr);
    pthread_condattr_destroy(&cattr);
    return SBUFFER_SUCCESS;
}

int sbuffer_close(sbuffer_t *buffer)
{
    if (buffer == NULL) return SBUFFER_FAILURE;

    pthread_mutex_lock(&buffer->mutex);
    buffer->closed = true;
    pthread_cond_broadcast(&buffer->not_empty);
    pthread_cond_broadcast(&buffer->not_full);
    pthread_mutex_unlock(&buffer->mutex);
    return SBUFFER_SUCCESS;
}

int sbuffer_free(sbuffer_t **buffer)
{
    if ((buffer == NULL) || (*buffer == NULL)) {
        return SBUFFER_FAILURE;
    }

    pthread_mutex_lock(&(*buffer)->mutex);
    (*buffer)->closed = true;
    pthread_cond_broadcast(&(*buffer)->not_empty);
    pthread_cond_broadcast(&(*buffer)->not_full);
    pthread_mutex_unlock(&(*buffer)->mutex);

    pthread_cond_destroy(&(*buffer)->not_empty);
    pthread_cond_destroy(&(*buffer)->not_full);
    pthread_mutex_destroy(&(*buffer)->mutex);
    
    munmap(*buffer, sizeof(sbuffer_t));
    *buffer = NULL;
    return SBUFFER_SUCCESS;
}

static void pop_head_unsafe(sbuffer_t *buffer, sensor_data_t *data)
{
    if (buffer->size == 0) return;
    if (data != NULL) {
        *data = buffer->nodes[buffer->head].data;
    }
    buffer->head = (buffer->head + 1) % buffer->capacity;
    buffer->size--;
}

int sbuffer_remove(sbuffer_t *buffer, sensor_data_t *data, int reader_id)
{
    if (buffer == NULL || data == NULL) return SBUFFER_FAILURE;

    pthread_mutex_lock(&buffer->mutex);

    while (true) {
        bool found = false;
        size_t current = buffer->head;
        for (size_t i = 0; i < buffer->size; i++) {
            sbuffer_node_t *node = &buffer->nodes[current];
            
            if (reader_id == READER_MGR && !node->ismgr) {
                *data = node->data;
                node->ismgr = true;
                found = true;
                if (node->isStore && i == 0) { // i == 0 means current == buffer->head
                    pop_head_unsafe(buffer, NULL);
                    pthread_cond_signal(&buffer->not_full);
                }
                break; // Found and consumed
            }
            if (reader_id == READER_STORE && !node->isStore) {
                *data = node->data;
                node->isStore = true;
                found = true;
                if (node->ismgr && i == 0) {
                    pop_head_unsafe(buffer, NULL);
                    pthread_cond_signal(&buffer->not_full);
                }
                break; // Found and consumed
            }
            current = (current + 1) % buffer->capacity;
        }

        if (found) {
            pthread_mutex_unlock(&buffer->mutex);
            return SBUFFER_SUCCESS;
        }

        if (buffer->closed) {
            pthread_mutex_unlock(&buffer->mutex);
            return SBUFFER_CLOSED;
        }

        pthread_cond_wait(&buffer->not_empty, &buffer->mutex);
    }
}

int sbuffer_insert(sbuffer_t *buffer, sensor_data_t *data)
{
    int result = SBUFFER_SUCCESS;

    if (buffer == NULL || data == NULL) return SBUFFER_FAILURE;

    pthread_mutex_lock(&buffer->mutex);
    if (buffer->closed) {
        pthread_mutex_unlock(&buffer->mutex);
        return SBUFFER_FAILURE;
    }

#if SBUFFER_FULL_POLICY == SBUFFER_FULL_BLOCK
    while (buffer->size >= buffer->capacity && !buffer->closed) {
        pthread_cond_wait(&buffer->not_full, &buffer->mutex);
    }
    if (buffer->closed) {
        pthread_mutex_unlock(&buffer->mutex);
        return SBUFFER_FAILURE;
    }
#elif SBUFFER_FULL_POLICY == SBUFFER_FULL_DROP_NEWEST
    if (buffer->size >= buffer->capacity) {
        buffer->dropped_count++;
        pthread_mutex_unlock(&buffer->mutex);
        return SBUFFER_DROPPED;
    }
#elif SBUFFER_FULL_POLICY == SBUFFER_FULL_DROP_OLDEST
    while (buffer->size >= buffer->capacity) {
        pop_head_unsafe(buffer, NULL);
        buffer->dropped_count++;
        result = SBUFFER_DROPPED;
    }
#endif

    size_t tail = (buffer->head + buffer->size) % buffer->capacity;
    buffer->nodes[tail].data = *data;
    buffer->nodes[tail].ismgr = false;
    buffer->nodes[tail].isStore = false;

    buffer->size++;
    buffer->last_stamp = data->timestamp;
    pthread_cond_broadcast(&buffer->not_empty);
    pthread_mutex_unlock(&buffer->mutex);
    return result;
}

unsigned long long sbuffer_get_drop_count(sbuffer_t *buffer)
{
    unsigned long long dropped;
    if (buffer == NULL) return 0;

    pthread_mutex_lock(&buffer->mutex);
    dropped = buffer->dropped_count;
    pthread_mutex_unlock(&buffer->mutex);
    return dropped;
}

int sbuffer_getlength(sbuffer_t *buffer)
{
    int size;
    if (buffer == NULL) return SBUFFER_FAILURE;

    pthread_mutex_lock(&buffer->mutex);
    size = (int)buffer->size;
    pthread_mutex_unlock(&buffer->mutex);
    return size;
}

void *sbuffer_getdataIndex(sbuffer_t *buffer, int index)
{
    if (buffer == NULL || index < 0) return NULL;

    pthread_mutex_lock(&buffer->mutex);
    if ((size_t)index >= buffer->size) {
        pthread_mutex_unlock(&buffer->mutex);
        return NULL;
    }
    
    size_t target = (buffer->head + index) % buffer->capacity;
    void *result = &buffer->nodes[target].data;
    pthread_mutex_unlock(&buffer->mutex);
    
    return result;
}
