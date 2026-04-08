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
 * Closes the producer side of the queue.
 * \param buffer pointer to the queue
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
int sbuffer_close(sbuffer_t *buffer);

/**
 * Removes the first sensor data in 'buffer' (at the 'head') and returns this sensor data as '*data'
 * If queue is empty and still open, SBUFFER_NO_DATA is returned.
 * If queue is closed and empty, SBUFFER_CLOSED is returned.
 * \param buffer a pointer to the buffer that is used
 * \param data a pointer to pre-allocated sensor_data_t space, the data will be copied into this structure. No new memory is allocated for 'data' in this function.
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
int sbuffer_remove(sbuffer_t *buffer, sensor_data_t *data);

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
