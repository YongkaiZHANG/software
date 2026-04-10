#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#ifndef _CONFIG_H_
#define _CONFIG_H_
#define RUN_AVG_LENGTH 5
#define BUFFER_SIZE 1024
#define FIFO_NAME 	"logFifo"     //name of the FIFO
#define FIFO_LOG    "gateway.log"	//name of log file
#define RECEIVER_DATA_LOG "sensor_data_recv.txt" // raw receiver measurements
#define SBUFFER_CAPACITY 65536
#define SBUFFER_FULL_BLOCK 0
#define SBUFFER_FULL_DROP_NEWEST 1
#define SBUFFER_FULL_DROP_OLDEST 2

#ifndef SBUFFER_FULL_POLICY
#define SBUFFER_FULL_POLICY SBUFFER_FULL_BLOCK
#endif

//error code
#define ASPRINTF_ERROR(err) 								\
		do {												\
			if ( (err) == -1 )								\
			{												\
				perror("asprintf failed");					\
				exit( EXIT_FAILURE );						\
			}												\
		} while(0)

typedef uint16_t sensor_id_t;
typedef double sensor_value_t;
typedef time_t sensor_ts_t;         // UTC timestamp as returned by time() - notice that the size of time_t is different on 32/64 bit machine
typedef struct sbuffer sbuffer_t;
/**
 * structure to hold sensor data
 */
// typedef struct {
//     sensor_id_t id;         /** < sensor id */
//     sensor_value_t value;   /** < sensor value */
//     sensor_ts_t ts;         /** < sensor timestamp */
// } sensor_data_t;
// typedef struct pollfd pollfd_t;

// typedef struct{
//   pollfd_t *file_descriptors;
//   time_t last_record;
//   sensor_data_t* sensor;
//   tcpsock_t* socket_id;
// } pollinfo;

typedef struct {
    uint16_t sensor_id;
    uint16_t room_id;
    double RUN_AVG;
    sensor_value_t value;   /** < sensor value */
    time_t timestamp;
    size_t sample_count;
    int8_t alert_state;
    double temperatures[RUN_AVG_LENGTH];
} sensor_data_t;
/**
 * basic node for the buffer, these nodes are linked together to create the buffer
 */
typedef struct sbuffer_node {
    sensor_data_t data;         /**< a structure containing the data */
    bool ismgr;//to check if the data is read
    bool isStore;// to check if the data is stored
} sbuffer_node_t;

/**s
 * a structure to keep track of the buffer
 */

typedef struct sbuffer{
    sbuffer_node_t nodes[SBUFFER_CAPACITY];
    size_t head;                /**< index of the first element */
    size_t tail;                /**< index of the next available slot */
    size_t size;                /**< number of pending items in queue */
    size_t capacity;            /**< max number of pending items */
    bool closed;                /**< producer side closed flag */
    unsigned long long dropped_count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    time_t last_stamp;//to record the time of last processing
}sbuffer_t;


#endif /* _CONFIG_H_ */
