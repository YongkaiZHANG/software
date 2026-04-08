#ifndef __CONMGR_H__
#define __CONMGR_H__

#define _GNU_SOURCE //needed for POLLRDHUP
#include <poll.h>
#include <stdio.h>
#include <stdbool.h>
#include "lib/tcpsock.h"
#include "lib/dplist.h"
#include "config.h"

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
 * Starts the TCP receiver process.
 * Valid measurements are written to sensor_data_recv.txt and forwarded
 * to the datamgr child through the supplied pipe write end.
 */
int connmgr_listen(int pipe_write_fd, int port, int timeout_seconds);

/*
 * This method should be called to clean up the connmgr, and to free all used memory.
 * After this no new connections will be accepted
 */
void connmgr_free(tcpsock_t *point);
#endif  //__CONMGR_H__
