/**
 * \author Yongkai Zhang
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "lib/dplist.h"
#include "datamgr.h"
#include <time.h>
#include "sbuffer.h"
/*
 * definition of error codes
 * */

#ifdef DEBUG
#define DEBUG_PRINTF(...)                                                                    \
    do                                                                                       \
    {                                                                                        \
        fprintf(stderr, "\nIn %s - function %s at line %d: ", __FILE__, __func__, __LINE__); \
        fprintf(stderr, __VA_ARGS__);                                                        \
        fflush(stderr);                                                                      \
    } while (0)
#else
#define DEBUG_PRINTF(...) (void)0
#endif

#define DPLIST_ERR_HANDLER(condition, err_code)   \
    do                                            \
    {                                             \
        if ((condition))                          \
            DEBUG_PRINTF(#condition " failed\n"); \
        assert(!(condition));                     \
    } while (0)

// define the dplist to contain the information
dplist_t *list = NULL;

void *element_copy(void *element)
{
    sensor_t *original = (sensor_t *)element;

    sensor_t *copy = malloc(sizeof(sensor_t));
    if (copy == NULL)
    {
        // Handle memory allocation failure
        return NULL;
    }

    copy->sensor_id = original->sensor_id;
    copy->room_id = original->room_id;
    copy->RUN_AVG = original->RUN_AVG;
    copy->timestamp = original->timestamp;

    // Copy the temperatures array using a loop
    for (int i = 0; i < RUN_AVG_LENGTH; i++)
    {
        copy->temperatures[i] = original->temperatures[i];
    }
    return (void *)copy;
}

void element_free(void **element)
{
    if ((*element) != NULL)
    {
        free((*element));
        *element = NULL;
    }
}

int element_compare(void *x, void *y)
{
    return ((((sensor_t *)x)->sensor_id < ((sensor_t *)y)->sensor_id) ? -1 : (((sensor_t *)x)->sensor_id == ((sensor_t *)y)->sensor_id) ? 0
                                                                                                                                        : 1);
}
/**
 *  This method holds the core functionality of your datamgr. It takes in 2 file pointers to the sensor files and parses them.
 *  When the method finishes all data should be in the internal pointer list and all log messages should be printed to stderr.
 *  \param fp_sensor_map file pointer to the map file
 *  \param buffer this is the shared bufffer
 * \param pipe_mutex is a pointer that protect the fifo safe for write data
 * \param pipe_fd is the file discriptor for the the child process
 * \param all_data_read this is the flat to show if all data inside the buffer have been read.
 * \param database_fail is a flag for database
 */
void datamgr_parse_sbuffer(FILE *fp_sensor_map, sbuffer_t *buffer, pthread_mutex_t *pipe_mutex, int *pipe_fd, int *all_data_read, int* database_fail)
{
    char *log_message;
    list = dpl_create(element_copy, element_free, element_compare);
    sensor_id_t sensorId = 0, roomId = 0;
    // get all the room id and sensor id
    while (fscanf(fp_sensor_map, "%hu %hu", &roomId, &sensorId) == 2)
    {
        sensor_t *sensor = (sensor_t *)malloc(sizeof(sensor_t));
        DPLIST_ERR_HANDLER(sensor == NULL, MEMORY_ERROR);
        sensor->room_id = roomId;
        sensor->sensor_id = sensorId;
        sensor->RUN_AVG = 0.0;
        sensor->timestamp = 0;
        for (int i = 0; i < RUN_AVG_LENGTH; i++)
        {
            sensor->temperatures[i] = 0.0;
        }
        dpl_insert_at_index(list, sensor, 0, true);
        free(sensor);
    }

    sensor_data_t data;
    sensor_t *dummy_sensor = NULL;

    while (!(*all_data_read) || !sbuffer_is_empty(buffer))
    {
        if (sbuffer_remove(buffer, &data, DATAMGR_READ, database_fail) == SBUFFER_SUCCESS)
        {
            double RUN_TEMP_TOTAL = 0;
            double RUN_AVG = 0;
            dummy_sensor = datamgr_get_sensor(data.id);
            if (dummy_sensor == NULL)
            {
                printf("There is no such a sensor id : %hd on the map.\n", data.id);
            }
            else
            {
                if (data.value >= 100.0 || data.value <= -50.0)
                {
                    printf("the invailed temp is %lf, this sensor id is %hd\n", data.value, data.id);
                    printf("The temperature measurement has an error.\n");
                }
                else
                {

                    dummy_sensor->timestamp = data.ts;
                    // shift the array 1->0 2->1
                    for (int count = 1; count < RUN_AVG_LENGTH; count++)
                    {
                        dummy_sensor->temperatures[count - 1] = dummy_sensor->temperatures[count];
                    }
                    dummy_sensor->temperatures[RUN_AVG_LENGTH - 1] = data.value;
                    for (int count = 0; count < RUN_AVG_LENGTH; count++)
                    {
                        RUN_TEMP_TOTAL = RUN_TEMP_TOTAL + dummy_sensor->temperatures[count];
                    }
                    RUN_AVG = RUN_TEMP_TOTAL / RUN_AVG_LENGTH;
                    dummy_sensor->RUN_AVG = RUN_AVG;
                    // printf("the sensor id is %hd the room id is %hd, and the temperature is %lf\n", dummy_sensor->sensor_id, dummy_sensor->room_id, temp_value);
                    if (dummy_sensor->temperatures[0] != 0.0)
                    {
                        printf("the sensor %hd, in room %hd, the RUN_AVG is %lf\n", dummy_sensor->sensor_id, dummy_sensor->room_id, dummy_sensor->RUN_AVG);
                        if (RUN_AVG < SET_MIN_TEMP)
                        {
                            // send to child process
                            asprintf(&log_message, "The sensor node with ID:%hd, reports it's too cold (running avg temperature = < %lf >)\n ", dummy_sensor->sensor_id, RUN_AVG);
                            send_into_pipe(pipe_mutex, pipe_fd, log_message);
                        }
                        else if (RUN_AVG > SET_MAX_TEMP)
                        { // send to child process
                            asprintf(&log_message, "The sensor node with ID:%hd, reports it's too hot (running avg temperature = < %lf >)\n ", dummy_sensor->sensor_id, RUN_AVG);
                            send_into_pipe(pipe_mutex, pipe_fd, log_message);
                        }
                    }
                }
            }
        }
        if (dpl_size(list) == -1)
        { // if the datamgr_free being called exit the loop immidiately;
            break;
        }
    }
}

/**
 * This method should be called to clean up the datamgr, and to free all used memory.
 * After this, any call to datamgr_get_room_id, datamgr_get_avg, datamgr_get_last_modified or datamgr_get_total_sensors will not return a valid result
 */
void datamgr_free()
{
    dpl_free(&list, true);
}

// get the sensor if we know the sensor_id

sensor_t *datamgr_get_sensor(sensor_id_t sensor_id)
{
    sensor_t *sensor = NULL;
    for (int index = 0; index < dpl_size(list); index++)
    {
        sensor = (sensor_t *)dpl_get_element_at_index(list, index);
        if (sensor->sensor_id == sensor_id)
        {
            return sensor;
        }
    }
    return NULL;
}