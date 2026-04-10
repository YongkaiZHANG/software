/**
 * \author Yongkai Zhang
 */

#ifndef _SBUFFER_H_
#define _SBUFFER_H_

#include "config.h"

#define SBUFFER_FAILURE -1
#define SBUFFER_SUCCESS 0
#define SBUFFER_NO_DATA 1
#define SBUFFER_CLOSED 2
#define SBUFFER_DROPPED 3

// typedef struct sbuffer sbuffer_t;

/**
 * Allocates and initializes a new shared buffer
 * \param buffer a double pointer to the buffer that needs to be initialized
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
int sbuffer_init(sbuffer_t **buffer);

/**
 * All allocated resources are freed and cleaned up
 * \param buffer a double pointer to the buffer that needs to be freed
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
int sbuffer_free(sbuffer_t **buffer);

/**
 * Closes the producer side of the queue and wakes up waiting consumers/producers.
 * \param buffer pointer to the queue
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
int sbuffer_close(sbuffer_t *buffer);

#define READER_MGR   1
#define READER_STORE 2

/**
 * Removes the first sensor data in 'buffer' that has not been read by the reader identified by 'reader_id'.
 * If the data has been read by both READER_MGR and READER_STORE, the node is removed from the buffer.
 * If queue is empty and still open, the function blocks until data becomes available.
 * If queue is closed and empty, SBUFFER_CLOSED is returned.
 * \param buffer a pointer to the buffer that is used
 * \param data a pointer to pre-allocated sensor_data_t space
 * \param reader_id the id of the reader (READER_MGR or READER_STORE)
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
int sbuffer_remove(sbuffer_t *buffer, sensor_data_t *data, int reader_id);

/**
 * Inserts the sensor data in 'data' at the end of 'buffer' (at the 'tail')
 * \param buffer a pointer to the buffer that is used
 * \param data a pointer to sensor_data_t data, that will be copied into the buffer
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occured
*/
int sbuffer_insert(sbuffer_t *buffer, sensor_data_t *data);

/**
 * Returns the total number of dropped items caused by full-queue policy.
 * \param buffer a pointer to the buffer that is used
 * \return number of dropped items
 */
unsigned long long sbuffer_get_drop_count(sbuffer_t *buffer);
/**
 * Get the length of the buffer
 * \param buffer a pointer to the buffer that is used
 * \return the length of buffer
*/
int sbuffer_getlength(sbuffer_t *buffer);
/**
 * Get the data at index
 * \param buffer a pointer to the buffer that is used
 *  \return the data at the corresponding index
*/
void *sbuffer_getdataIndex(sbuffer_t *buffer,int index);

#endif  //_SBUFFER_H_
