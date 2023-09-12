/**
 * \author Yongkai Zhang
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>

//define for error handle
#define DPLIST_NO_ERROR 0
#define DPLIST_MEMORY_ERROR 1  // error due to mem alloc failure
#define DPLIST_INVALID_ERROR 2 // error due to a list operation applied on a NULL list
#define NO_ERROR 0
#define MEMORY_ERROR 1  // error due to mem alloc failure
#define INVALID_ERROR 2 // error due to no such sensor
//define for sbuffer
#define SBUFFER_SUCCESS 0
#define SBUFFER_FAILURE -1
#define SBUFFER_NO_DATA 1
#define DATAMGR_READ 3
#define DATABASE_READ 4
//define for sensor_db
// stringify preprocessor directives using 2-level preprocessor magic
// this avoids using directives like -DDB_NAME=\"some_db_name\"
#define REAL_TO_STRING(s) #s
#define TO_STRING(s) REAL_TO_STRING(s)    //force macro-expansion on s before stringify s
#define DB_NAME Sensor.db
#define TABLE_NAME SensorData
#define DBCONN sqlite3
//other define
#define RUN_AVG_LENGTH 5
#define LOG_FILE "gateway.log"
#define MAX_ATTEMPT 3


typedef uint16_t sensor_id_t;
typedef double sensor_value_t;
typedef time_t sensor_ts_t;         // UTC timestamp as returned by time() - notice that the size of time_t is different on 32/64 bit machine

/**
 * structure to hold sensor data
 */
typedef struct {
    sensor_id_t id;         /** < sensor id */
    sensor_value_t value;   /** < sensor value */
    sensor_ts_t ts;         /** < sensor timestamp */
} sensor_data_t;

typedef struct {
    uint16_t sensor_id;
    uint16_t room_id;
    double RUN_AVG;
    time_t timestamp;
    double temperatures[RUN_AVG_LENGTH];
} sensor_t;

//the structure of sbuffer node
typedef struct sbuffer_node {
    sensor_data_t data;
    struct sbuffer_node* next;
    // those two flags is set to ensure the node not be delete falsely cuased by one reader get issue and another still work
    bool datamgr_read;
    bool database_read;
} sbuffer_node_t;

//the structure of sbuffer
typedef struct {
    sbuffer_node_t* head;
    sbuffer_node_t* tail;
    pthread_rwlock_t rwlock; // Read-Write Lock for synchronization(this method is use only one write and multi-readers can access the node data)
   pthread_mutex_t mutex;   // Mutex for condition variable that allow only one thread to change the status of the buffer node(either insert or remove)
   pthread_mutex_t rmutex;
} sbuffer_t;



#endif /* _CONFIG_H_ */