/**
 * \author Yongkai Zhang
 */

#ifndef DATAMGR_H_
#define DATAMGR_H_

#include <stdlib.h>
#include <stdio.h>
#include "config.h"
#include "sbuffer.h"

#ifndef SET_MAX_TEMP 
#error SET_MAX_TEMP not set
#endif

#ifndef SET_MIN_TEMP
#error SET_MIN_TEMP not set
#endif


/**
 *  This method holds the core functionality of your datamgr. It takes in 2 file pointers to the sensor files and parses them. 
 *  When the method finishes all data should be in the internal pointer list and all log messages should be printed to stderr.
 *  \param fp_sensor_map file pointer to the map file
 *  \param buffer this is the shared bufffer
 * \param pipe_mutex is a pointer that protect the fifo safe for write data
 * \param pipe_fd is the file discriptor for the the child process
 * \param all_data_read this is the flat to show if all data inside the buffer have been read.
 * \param database_fail is a flag for database fail
 */
void datamgr_parse_sbuffer(FILE *fp_sensor_map, sbuffer_t* buffer, pthread_mutex_t *pipe_mutex,int * pipe_fd, int *all_data_read , int* database_fail);
/**
 * This method should be called to clean up the datamgr, and to free all used memory. 
 * After this, any call to datamgr_get_room_id, datamgr_get_avg, datamgr_get_last_modified or datamgr_get_total_sensors will not return a valid result
 */
void datamgr_free();


//get the sensor if we know the sensor_id
sensor_t *datamgr_get_sensor(sensor_id_t sensor_id);

#endif  //DATAMGR_H_
