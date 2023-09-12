/**
 * \author Yongkai Zhang
 */

#ifndef __CONMGR_H__
#define __CONMGR_H__

#define _GNU_SOURCE //needed for POLLRDHUP
#include "lib/tcpsock.h"
#include "lib/dplist.h"
#include "config.h"
#include <stdio.h>
#include "sbuffer.h"


/*
 * This method holds the core functionality of your connmgr. It starts listening on the given port and
 * when when a sensor node connects it writes the data to a sbuffer. This file must have the
 *same format as the sensor_data file in assignment 6 and 7.
 */
void connmgr_listens(sbuffer_t* buffer,int port,pthread_mutex_t *pipe_mutex,int * pipe_fd, int* database_fail);

/*
 * This method should be called to clean up the connmgr, and to free all used memory.
 * After this no new connections will be accepted
 */
void connmgr_free();

#endif  //__CONMGR_H__