#ifndef __CONMGR_H__
#define __CONMGR_H__

#define _GNU_SOURCE //needed for POLLRDHUP
#include <poll.h>
#include <stdio.h>
#include <stdbool.h>
#include "lib/tcpsock.h"
#include "lib/dplist.h"
#include "config.h"
#include "sbuffer.h"

#ifndef TIMEOUT
 // #error TIMEOUT not specified!(in seconds)
#endif

typedef struct pollfd pollfd_t;

typedef struct{
  pollfd_t *file_descriptors;
  time_t last_record;
  sensor_data_t* sensor;
  tcpsock_t* socket_id;
} pollinfo;

/*
 * This method holds the core functionality of your connmgr. It starts listening on the given port and
 * when when a sensor node connects it writes the data to a sensor_data_recv file. This file must have the
 *same format as the sensor_data file in assignment 6 and 7.
 */
void connmgr_listen(sbuffer_t *sbuffer, int port, int timeout_seconds);

/*
 * This method should be called to clean up the connmgr, and to free all used memory.
 * After this no new connections will be accepted
 */
void connmgr_free(void);
#endif  //__CONMGR_H__
