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
    return SBUFFER_SUCCESS;
}

int sbuffer_close(sbuffer_t *buffer)
{
    if (buffer == NULL) return SBUFFER_FAILURE;
    buffer->closed = true;
    return SBUFFER_SUCCESS;
}

int sbuffer_free(sbuffer_t **buffer)
{
    sbuffer_node_t *dummy;

    if ((buffer == NULL) || (*buffer == NULL)) {
        return SBUFFER_FAILURE;
    }

    sbuffer_close(*buffer);
    while ((*buffer)->head != NULL) {
        dummy = (*buffer)->head;
        (*buffer)->head = (*buffer)->head->next;
        free(dummy);
    }
    (*buffer)->tail = NULL;
    (*buffer)->size = 0;
    free(*buffer);
    *buffer = NULL;
    return SBUFFER_SUCCESS;
}

int sbuffer_remove(sbuffer_t *buffer, sensor_data_t *data)
{
    if (buffer == NULL || data == NULL) return SBUFFER_FAILURE;
    if (buffer->size == 0) {
        return buffer->closed ? SBUFFER_CLOSED : SBUFFER_NO_DATA;
    }

    pop_head_unsafe(buffer, data);
    buffer->size--;
    return SBUFFER_SUCCESS;
}

int sbuffer_insert(sbuffer_t *buffer, sensor_data_t *data)
{
    sbuffer_node_t *dummy;
    int result = SBUFFER_SUCCESS;

    if (buffer == NULL || data == NULL) return SBUFFER_FAILURE;
    if (buffer->closed) return SBUFFER_FAILURE;

#if SBUFFER_FULL_POLICY == SBUFFER_FULL_BLOCK
    /*
     * In process-only mode there is no blocking producer/consumer handshake.
     * Treat a full queue as backpressure to the caller.
     */
    if (buffer->size >= buffer->capacity) {
        return SBUFFER_FAILURE;
    }
#elif SBUFFER_FULL_POLICY == SBUFFER_FULL_DROP_NEWEST
    if (buffer->size >= buffer->capacity) {
        buffer->dropped_count++;
        return SBUFFER_DROPPED;
    }
#elif SBUFFER_FULL_POLICY == SBUFFER_FULL_DROP_OLDEST
    if (buffer->size >= buffer->capacity) {
        pop_head_unsafe(buffer, NULL);
        buffer->size--;
        buffer->dropped_count++;
        result = SBUFFER_DROPPED;
    }
#endif

    dummy = malloc(sizeof(sbuffer_node_t));
    if (dummy == NULL) {
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
    return result;
}

unsigned long long sbuffer_get_drop_count(sbuffer_t *buffer)
{
    if (buffer == NULL) return 0;
    return buffer->dropped_count;
}

int sbuffer_getlength(sbuffer_t *buffer)
{
    if (buffer == NULL) return SBUFFER_FAILURE;
    return (int)buffer->size;
}

void *sbuffer_getdataIndex(sbuffer_t *buffer, int index)
{
    sbuffer_node_t *dummy;
    int count = 0;

    if (buffer == NULL || index < 0) return NULL;

    for (dummy = buffer->head; dummy != NULL; dummy = dummy->next, count++) {
        if (count == index) {
            return &dummy->data;
        }
    }
    return NULL;
}
