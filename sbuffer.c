/**
 * \author Yongkai Zhang
 */

#define _GNU_SOURCE
#include <pthread.h>
#include "sbuffer.h"
#include <string.h>

int sbuffer_init(sbuffer_t **buffer)
{
    *buffer = (sbuffer_t *)malloc(sizeof(sbuffer_t));
    if (*buffer == NULL)
    {
        return SBUFFER_FAILURE;
    }

    (*buffer)->head = NULL;
    (*buffer)->tail = NULL;
    pthread_rwlock_init(&(*buffer)->rwlock, NULL);
     pthread_mutex_init(&(*buffer)->mutex, NULL);
    return SBUFFER_SUCCESS;
}

int sbuffer_free(sbuffer_t **buffer)
{

    if (*buffer == NULL)
    {
        return SBUFFER_FAILURE;
    }

    sbuffer_node_t *current = (*buffer)->head;
    while (current != NULL)
    {
        sbuffer_node_t *temp = current;
        current = current->next;
        free(temp);
    }
    pthread_rwlock_destroy(&(*buffer)->rwlock);
    pthread_mutex_destroy(&(*buffer)->mutex);
    free(*buffer);
    *buffer = NULL;

    return SBUFFER_SUCCESS;
}

int sbuffer_remove(sbuffer_t *buffer, sensor_data_t *data, int who_read, int* database_fail)
{
    //to check if the buffer not exsit or the data is not avaliable
    if (buffer == NULL || data == NULL)
    {
        return SBUFFER_FAILURE;
    }
    //ro check if  database failed to let the remove not work so that the sbuffer can be deleted
    if((*database_fail))
    {
        return SBUFFER_FAILURE;
    }
    //if the readers second time coming then just return, it will not work for the first coming
    if (who_read == DATABASE_READ && buffer->head != NULL && buffer->head->database_read)
    {
        return SBUFFER_FAILURE;
    }
    if (who_read == DATAMGR_READ && buffer->head != NULL && buffer->head->datamgr_read)
    {
        return SBUFFER_FAILURE;
    }

    //the first time when the readers coming
    if (buffer->head == NULL)
    {
        // Wait until data is available in the buffer
        return SBUFFER_FAILURE;
    }
    else
    {
        // Acquire the read lock
        pthread_rwlock_wrlock(&buffer->rwlock);
        // Create a copy of the data to be read by both readers
        *data = buffer->head->data;
        if (who_read == DATABASE_READ)
        {
            if (buffer->head->database_read == false)
            {
                buffer->head->database_read = true;
                // If both readers have finished, delete the node
                if (buffer->head->database_read && buffer->head->datamgr_read)
                {
                    sbuffer_node_t *temp = buffer->head;
                    buffer->head = temp->next;
                    free(temp);
                }
            }
        }
        else if (who_read == DATAMGR_READ)
        {
            if (buffer->head->datamgr_read == false)
            {
                buffer->head->datamgr_read = true;
                // If both readers have finished, delete the node
                if (buffer->head->database_read && buffer->head->datamgr_read)
                {
                    sbuffer_node_t *temp = buffer->head;
                    buffer->head = temp->next;
                    free(temp);
                }
            }
        }

        pthread_rwlock_unlock(&buffer->rwlock);

        return SBUFFER_SUCCESS;
    }
}

int sbuffer_insert(sbuffer_t *buffer, const sensor_data_t *data, int* database_fail)
{
    if (buffer == NULL || data == NULL)
    {
        return SBUFFER_FAILURE;
    }
    //to check if the the database fail, then prevent to insert in order to let the sbuffer free to accece resources
    if((*database_fail))
    {
        return SBUFFER_FAILURE;
    }

    sbuffer_node_t *new_node = (sbuffer_node_t *)malloc(sizeof(sbuffer_node_t));
    if (new_node == NULL)
    {
        return SBUFFER_FAILURE;
    }

    new_node->data = *data;
    // to set the flag to flase when insert to make sure the node is read by both readers
    new_node->database_read = false;
    new_node->datamgr_read = false;
    new_node->next = NULL;

    pthread_rwlock_wrlock(&buffer->rwlock);

    if (buffer->head == NULL) // the buffer is empty
    {
        buffer->head = new_node;
        buffer->tail = new_node;
    }
    else
    {
        buffer->tail->next = new_node;
        buffer->tail = new_node;
    }

    pthread_rwlock_unlock(&buffer->rwlock);
    return SBUFFER_SUCCESS;
}

int sbuffer_is_empty(sbuffer_t *buffer)
{
    pthread_mutex_lock(&buffer->mutex);
    int is_empty = (buffer->head == NULL);
    pthread_mutex_unlock(&buffer->mutex);
    return is_empty;
}

void send_into_pipe(pthread_mutex_t *pipe_mutex, int *pipe_fd, char *pipe_info)
{
    pthread_mutex_lock(pipe_mutex);
    write(*pipe_fd, pipe_info, strlen(pipe_info));
    pthread_mutex_unlock(pipe_mutex);
    free(pipe_info);
}
