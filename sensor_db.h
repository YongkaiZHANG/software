/**
 * \author Yongkai Zhang
 */

#ifndef _SENSOR_DB_H_
#define _SENSOR_DB_H_

#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include <sqlite3.h>
#include "sbuffer.h"



typedef int (*callback_t)(void *, int, char **, char **);

/**
 * Make a connection to the database server
 * Create (open) a database with name DB_NAME having 1 table named TABLE_NAME  
 * \param clear_up_flag if the table existed, clear up the existing data when clear_up_flag is set to 1
 * \return the connection for success, NULL if an error occurs
 */
DBCONN *init_connection(char clear_up_flag, pthread_mutex_t *pipe_mutex,int * pipe_fd);

/**
 * Disconnect from the database server
 * \param conn pointer to the current connection
 */
void disconnect(DBCONN *conn, pthread_mutex_t *pipe_mutex, int *pipe_fd);

/**
 * Write an INSERT query to insert a single sensor measurement
 * \param conn pointer to the current connection
 * \param id the sensor id
 * \param value the measurement value
 * \param ts the measurement timestamp
 * \return zero for success, and non-zero if an error occurs
 */
int insert_sensor(DBCONN *conn, sensor_id_t id, sensor_value_t value, sensor_ts_t ts);

/**
 * Write an INSERT query to insert all sensor measurements available in the file 'sensor_data'
 * \param conn pointer to the current connection
 * \param buffer is the sbuffer pointer
 * \param all_data_read is the flag to show if the data in the buffer read
 * \param pipe_mutex is the pointer to lock the write into fifo
 * \param pipe_fd is the pointer to the process file discriptor
 * \param database_fail is a flag for database fail
 * \return zero for success, and non-zero if an error occurs
 */
int insert_sensor_from_sbuffer(DBCONN *conn, sbuffer_t* buffer,const int* all_data_read, pthread_mutex_t *pipe_mutex, int *pipe_fd, int*database_fail);

#endif /* _SENSOR_DB_H_ */
