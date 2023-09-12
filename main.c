/**
 * \author Yongkai Zhang
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "sbuffer.h"
#include <string.h>
#include <limits.h>   // Include this header for PIPE_BUF
#include <sys/wait.h> // Include header for waitpid
#include <sys/types.h>
#include "connmgr.h"
#include "sensor_db.h"
#include "datamgr.h"

typedef struct
{
    sbuffer_t *buffer;
    pid_t child_pid;
    int pipe_fd[2]; // Pipe file descriptors
    pthread_mutex_t log_mutex;                                // mutex for synchronize the log
    pthread_mutex_t all_data_read_mutex;                      // to set the flag all data is readed from connmgr
    pthread_cond_t all_data_read_cond;                        // a signal to tell other thread all the data have been sent
    int all_data_read;                                        // the flag of all data sent by the connmgr
    int database_fail;                                        // the flag for database failed
    int port;                                                 // the port for connmgr
} SharedData;

void init_shared_data(SharedData *shared_data)
{
    shared_data->buffer = NULL; // Initialize this properly
    shared_data->child_pid = -1;
    //pipe(shared_data->pipe_fd); // Initialize the pipe
    pthread_mutex_init(&shared_data->log_mutex, NULL);
    pthread_mutex_init(&shared_data->all_data_read_mutex, NULL);
    pthread_cond_init(&shared_data->all_data_read_cond, NULL);
    shared_data->all_data_read = 0;
    shared_data->database_fail = 0;
}

void *connmgr_thread(void *arg)
{
    SharedData *shared_data = (SharedData *)arg;
    sbuffer_t *buffer = shared_data->buffer;
    int *database_fail = &(shared_data->database_fail);
    pthread_mutex_t *log_mutex = &(shared_data->log_mutex);
    int *pipe_fd = &shared_data->pipe_fd[1];
    int *all_data_read = &(shared_data->all_data_read);
    int PORT = shared_data->port;
    pthread_mutex_t *all_data_read_mutex = &(shared_data->all_data_read_mutex);
    pthread_cond_t *all_data_read_cond = &(shared_data->all_data_read_cond);
    printf("start the connmgr\n");
    // in this listen function it will always listen to the node coming
    connmgr_listens(buffer, PORT, log_mutex, pipe_fd, database_fail);
    // Signal that all data has been written
    pthread_mutex_lock(all_data_read_mutex);
    *all_data_read = 1;
    pthread_cond_signal(all_data_read_cond);
    pthread_mutex_unlock(all_data_read_mutex);
    printf("the connmgr is end\n");
    return NULL;
}

void *datamgr_thread(void *arg)
{
    SharedData *shared_data = (SharedData *)arg;
    sbuffer_t *buffer = shared_data->buffer;
    int *database_fail = &(shared_data->database_fail);
    pthread_mutex_t *log_mutex = &(shared_data->log_mutex);
    int *pipe_fd = &shared_data->pipe_fd[1];
    int *all_data_read = &(shared_data->all_data_read);
    FILE *fp_sensor_map = fopen("room_sensor.map", "r");
    printf("strat the datatmgr\n");
    datamgr_parse_sbuffer(fp_sensor_map, buffer, log_mutex, pipe_fd, all_data_read, database_fail);
    printf("the datamgr is end\n");
    return NULL;
}

void *database_thread(void *arg)
{
    SharedData *shared_data = (SharedData *)arg;
    sbuffer_t* buffer=shared_data->buffer;
    int* database_fail = &(shared_data->database_fail);
    pthread_mutex_t* log_mutex=&(shared_data->log_mutex);
    int* pipe_fd = &shared_data->pipe_fd[1];
    int* all_data_read=&(shared_data->all_data_read);
    DBCONN *db = init_connection(1, log_mutex, pipe_fd);
    // here if can't connect to db then sleep 2s try it again,untill the third time
    if (db == NULL)
    {
        sleep(2);
        for (int attempts = 0; attempts < MAX_ATTEMPT - 1; attempts++)
        {
            db = init_connection(1, log_mutex, pipe_fd);
            if (attempts == 1 && db == NULL)
            {
                connmgr_free();        // close the connmgr
                datamgr_free();        // stop the datamgr
                sbuffer_free(&buffer); // and free the resources in the buffer
                                       //  Send log message to child process
                char *log_message;
                asprintf(&log_message, "Unable to connect to SQL server.");
                send_into_pipe(log_mutex, pipe_fd, log_message);
                exit(0);
            }
            sleep(2);
        }
    }
    printf("start the database\n");
    insert_sensor_from_sbuffer(db, buffer, all_data_read, log_mutex, pipe_fd, database_fail);
    // when all the data read then disconnect the connection
    disconnect(db, log_mutex, pipe_fd);
    printf("the database is end\n");
    return NULL;
}
/**
 * For starting the sensor node 4 command line arguments are needed. These should be given in the order below
 * and can then be used through the argv[] variable
 *
 * argv[1] = server PORT
 */
void print_help(void);
int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        print_help();
        exit(EXIT_SUCCESS);
    }
    int PORT = atoi(argv[1]); // get the port number
    SharedData shared_data;
    init_shared_data(&shared_data);
    shared_data.port = PORT;
    // Initialize sbuffer
    if (sbuffer_init(&shared_data.buffer) != SBUFFER_SUCCESS)
    {
        fprintf(stderr, "Error initializing buffer\n");
        return 1;
    }
    // Create pipe for communication between parent and child
    if (pipe(shared_data.pipe_fd) == -1)
    {
        perror("pipe");
        return 1;
    }

    // Create child process for handling logging
    shared_data.child_pid = fork();

    if (shared_data.child_pid == -1)
    {
        perror("fork");
        return 1;
    }

    if (shared_data.child_pid == 0)
    {
        close(shared_data.pipe_fd[1]); // Close write end of the pipe
        FILE *log_file = fopen(LOG_FILE, "a");
        if (log_file == NULL)
        {
            perror("fopen");
            return 1;
        }

        char buffers[PIPE_BUF];
        int bytes_read;
        int count = 0;
        char *write_masssage=NULL;
        while ((bytes_read = read(shared_data.pipe_fd[0], buffers, PIPE_BUF)) > 0)
        {
            buffers[bytes_read] = '\0'; // Null-terminate the buffer
            count++;

            if (strcmp(buffers, "TERMINATE") == 0)
            {
                break; // Stop reading when termination message is received
            }
            time_t current_time = time(NULL);
            asprintf(&write_masssage, "No.%d %ld %s\n", count, current_time, buffers); // here is allocate the memory for the massage
            // fwrite(write_masssage, 1, bytes_read, log_file);
            fwrite(write_masssage, strlen(write_masssage), 1, log_file); // Write data from pipe to log file
            fflush(log_file);
            free(write_masssage); // free the memory allocate
        }
        
        fclose(log_file);
        close(shared_data.pipe_fd[0]);
        // Clean up and free resources
        printf("the child is finish\n");
        sbuffer_free(&shared_data.buffer);
        // here the child procerss to end to force the whole program stop
        exit(0);
    }

    // Parent process

    // Create writer thread
    pthread_t connmgr;
    if (pthread_create(&connmgr, NULL, connmgr_thread, &shared_data) != 0)
    {
        fprintf(stderr, "Error creating writer thread\n");
        return 1;
    }

    // Create datamgr thread
    pthread_t datamgr;
    if (pthread_create(&datamgr, NULL, datamgr_thread, &shared_data) != 0)
    {
        fprintf(stderr, "Error creating datamgr thread\n");
        return 1;
    }
    // creat database thread
    pthread_t databse;
    if (pthread_create(&databse, NULL, database_thread, &shared_data) != 0)
    {
        fprintf(stderr, "Error creating database thread\n");
        return 1;
    }

    // // Wait for all data to be read and written
    pthread_mutex_lock(&shared_data.all_data_read_mutex);
    while (!shared_data.all_data_read)
    {
        pthread_cond_wait(&shared_data.all_data_read_cond, &shared_data.all_data_read_mutex);
    }
    pthread_mutex_unlock(&shared_data.all_data_read_mutex);

    // Signal that all data has been written
    pthread_mutex_lock(&shared_data.all_data_read_mutex);
    shared_data.all_data_read = 1;
    pthread_cond_broadcast(&shared_data.all_data_read_cond); // Use broadcast to signal all waiting threads
    pthread_mutex_unlock(&shared_data.all_data_read_mutex);

    // Wait for reader threads to finish
    pthread_join(connmgr, NULL);
    pthread_join(datamgr, NULL);
    pthread_join(databse, NULL);

    // Send termination message to child process
    const char *termination_message = "TERMINATE";
    write(shared_data.pipe_fd[1], termination_message, strlen(termination_message));

    return 0;
}

/**
 * Helper method to print a message on how to use this application
 */
void print_help(void)
{
    printf("Use this program with 1 command line options: \n");
    printf("\t%-15s : TCP server port number\n", "\'server port\'");
}