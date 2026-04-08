/**
 * \author Yongkai Zhang
 */

#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "sbuffer.h"

#if (SBUFFER_FULL_POLICY != SBUFFER_FULL_BLOCK) && \
    (SBUFFER_FULL_POLICY != SBUFFER_FULL_DROP_NEWEST) && \
    (SBUFFER_FULL_POLICY != SBUFFER_FULL_DROP_OLDEST)
#error Unsupported SBUFFER_FULL_POLICY
#endif

static void pop_head_unsafe(sbuffer_t *buffer, sensor_data_t *data)
{
    /* Caller must hold buffer->mutex. */
    sbuffer_node_t *dummy = buffer->head;

    if (dummy == NULL) return;
    if (data != NULL) {
        *data = dummy->data;
    }

    buffer->head = dummy->next;
    if (buffer->head == NULL) {
        buffer->tail = NULL;
    }
    free(dummy);
}

int sbuffer_init(sbuffer_t **buffer)
{
    *buffer = malloc(sizeof(sbuffer_t));
    if (*buffer == NULL) return SBUFFER_FAILURE;

    (*buffer)->head = NULL;
    (*buffer)->tail = NULL;
    (*buffer)->size = 0;
    (*buffer)->capacity = SBUFFER_CAPACITY;
    (*buffer)->closed = false;
    (*buffer)->dropped_count = 0;
    (*buffer)->last_stamp = 0;

    if (pthread_mutex_init(&(*buffer)->mutex, NULL) != 0) {
        free(*buffer);
        *buffer = NULL;
        return SBUFFER_FAILURE;
    }
    if (pthread_cond_init(&(*buffer)->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&(*buffer)->mutex);
        free(*buffer);
        *buffer = NULL;
        return SBUFFER_FAILURE;
    }
    if (pthread_cond_init(&(*buffer)->not_full, NULL) != 0) {
        pthread_cond_destroy(&(*buffer)->not_empty);
        pthread_mutex_destroy(&(*buffer)->mutex);
        free(*buffer);
        *buffer = NULL;
        return SBUFFER_FAILURE;
    }

    return SBUFFER_SUCCESS;
}

int sbuffer_close(sbuffer_t *buffer)
{
    if (buffer == NULL) return SBUFFER_FAILURE;

    pthread_mutex_lock(&buffer->mutex);
    buffer->closed = true;
    /* Wake both producers and consumers so blocked waits can exit cleanly. */
    pthread_cond_broadcast(&buffer->not_empty);
    pthread_cond_broadcast(&buffer->not_full);
    pthread_mutex_unlock(&buffer->mutex);
    return SBUFFER_SUCCESS;
}

int sbuffer_free(sbuffer_t **buffer)
{
    sbuffer_node_t *dummy;

    if ((buffer == NULL) || (*buffer == NULL)) {
        return SBUFFER_FAILURE;
    }

    sbuffer_close(*buffer);

    pthread_mutex_lock(&(*buffer)->mutex);
    while ((*buffer)->head != NULL) {
        dummy = (*buffer)->head;
        (*buffer)->head = (*buffer)->head->next;
        free(dummy);
    }
    (*buffer)->tail = NULL;
    (*buffer)->size = 0;
    pthread_mutex_unlock(&(*buffer)->mutex);

    pthread_cond_destroy(&(*buffer)->not_empty);
    pthread_cond_destroy(&(*buffer)->not_full);
    pthread_mutex_destroy(&(*buffer)->mutex);
    free(*buffer);
    *buffer = NULL;
    return SBUFFER_SUCCESS;
}

int sbuffer_remove(sbuffer_t *buffer, sensor_data_t *data)
{
    if (buffer == NULL || data == NULL) return SBUFFER_FAILURE;

    pthread_mutex_lock(&buffer->mutex);
    /* Blocking consumer: wait for data unless producer side is closed. */
    while (buffer->size == 0 && !buffer->closed) {
        pthread_cond_wait(&buffer->not_empty, &buffer->mutex);
    }

    if (buffer->size == 0 && buffer->closed) {
        pthread_mutex_unlock(&buffer->mutex);
        return SBUFFER_CLOSED;
    }

    pop_head_unsafe(buffer, data);
    buffer->size--;
    pthread_cond_signal(&buffer->not_full);
    pthread_mutex_unlock(&buffer->mutex);
    return SBUFFER_SUCCESS;
}

int sbuffer_insert(sbuffer_t *buffer, sensor_data_t *data)
{
    sbuffer_node_t *dummy;
    int result = SBUFFER_SUCCESS;

    if (buffer == NULL || data == NULL) return SBUFFER_FAILURE;

    pthread_mutex_lock(&buffer->mutex);
    if (buffer->closed) {
        pthread_mutex_unlock(&buffer->mutex);
        return SBUFFER_FAILURE;
    }

#if SBUFFER_FULL_POLICY == SBUFFER_FULL_BLOCK
    /* Backpressure mode: never drop, producer waits for free capacity. */
    while (buffer->size >= buffer->capacity && !buffer->closed) {
        pthread_cond_wait(&buffer->not_full, &buffer->mutex);
    }
    if (buffer->closed) {
        pthread_mutex_unlock(&buffer->mutex);
        return SBUFFER_FAILURE;
    }
#elif SBUFFER_FULL_POLICY == SBUFFER_FULL_DROP_NEWEST
    /* Keep old data, reject newest sample when queue is full. */
    if (buffer->size >= buffer->capacity) {
        buffer->dropped_count++;
        pthread_mutex_unlock(&buffer->mutex);
        return SBUFFER_DROPPED;
    }
#elif SBUFFER_FULL_POLICY == SBUFFER_FULL_DROP_OLDEST
    /* Keep throughput by discarding oldest queued sample. */
    if (buffer->size >= buffer->capacity) {
        pop_head_unsafe(buffer, NULL);
        buffer->size--;
        buffer->dropped_count++;
        result = SBUFFER_DROPPED;
    }
#endif

    dummy = malloc(sizeof(sbuffer_node_t));
    if (dummy == NULL) {
        pthread_mutex_unlock(&buffer->mutex);
        return SBUFFER_FAILURE;
    }
    dummy->data = *data;
    dummy->next = NULL;

    if (buffer->tail == NULL) {
        buffer->head = buffer->tail = dummy;
    } else {
        buffer->tail->next = dummy;
        buffer->tail = dummy;
    }

    buffer->size++;
    buffer->last_stamp = data->timestamp;
    /* One waiting consumer is enough for one newly inserted item. */
    pthread_cond_signal(&buffer->not_empty);
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
    sbuffer_node_t *dummy;
    int count = 0;

    if (buffer == NULL || index < 0) return NULL;

    pthread_mutex_lock(&buffer->mutex);
    for (dummy = buffer->head; dummy != NULL; dummy = dummy->next, count++) {
        if (count == index) {
            void *result = &dummy->data;
            pthread_mutex_unlock(&buffer->mutex);
            return result;
        }
    }
    pthread_mutex_unlock(&buffer->mutex);
    return NULL;
}
