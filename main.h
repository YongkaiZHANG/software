#ifndef MAIN_H_
#define MAIN_H_

#define _GNU_SOURCE //needed for asprintf
#include "connmgr.h"
#include "sbuffer.h"
#include "config.h"
#include "datamgr.h"
#include "sensor_db.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

//this is the method to start datamanager 
//datamgr is to store the infformation get from the sbuffer
void* start_datamgr(void *arg);

//this is the method to connect with the node with parameter port
void* start_connmgr(void *port);

//this is the method to start database storage
void* start_stormgr(void *arg);

//main function to generate fork() to control fifo and the sensor gateway
//the sensor gateway is conbinated by three threads, for dataconnection to write
//and the datamgr, stormgr to read.
int main();


//fifo init
void fifomgr_init();
//fifo read
void fifomgr_read();
//fifo write
void fifomgr_write(char * arg);

#endif  //MAIN_H_
