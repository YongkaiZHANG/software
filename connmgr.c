/**
 * \author Yongkai Zhang
 */

#define _GNU_SOURCE
#include "connmgr.h"
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <poll.h>
#include <string.h>

#ifndef TIMEOUT
#define TIMEOUT
#error TIMEOUT not specified!(in seconds)
#endif

typedef struct pollfd mypollfd;

// Define the client data structure here
typedef struct
{
    bool is_new;
    mypollfd fds;
    time_t last_record;
    tcpsock_t *sock_ptr;
    uint16_t sensor_id;
} tcp_poll_t;

dplist_t *connections = NULL;

// dplist element free function for the client_t struct
void client_free(void **element)
{
    if (element != NULL && *element != NULL)
    {
        // tcp_poll_t *client = *(tcp_poll_t **)element;
        // tcp_close(&client->sock_ptr); // Close the socket
        free(*element);
        *element = NULL;
    }
}

// dplist element compare function for the client_t struct
int client_compare(void *element1, void *element2)
{
    tcp_poll_t *client1 = (tcp_poll_t *)element1;
    tcp_poll_t *client2 = (tcp_poll_t *)element2;
    return ((((tcp_poll_t *)client1)->sensor_id < ((tcp_poll_t *)client2)->sensor_id) ? -1 : (((tcp_poll_t *)client1)->sensor_id == ((tcp_poll_t *)client2)->sensor_id) ? 0
                                                                                                                                                                        : 1);
}

// dplist element copy function for the client_t struct
void *client_copy(void *element)
{
    tcp_poll_t *client = (tcp_poll_t *)element;
    tcp_poll_t *new_client = (tcp_poll_t *)malloc(sizeof(tcp_poll_t));
    new_client->sock_ptr = client->sock_ptr;
    new_client->sensor_id = client->sensor_id;
    new_client->fds = client->fds;
    new_client->is_new = client->is_new;
    new_client->last_record = client->last_record;
    return new_client;
}

void connmgr_listens(sbuffer_t *buffer, int port, pthread_mutex_t *pipe_mutex, int *pipe_fd, int *database_fail)
{
    // Create the server socket
    char *log_message; // for send massage to child
    tcpsock_t *server = NULL;
    tcp_poll_t *server_poll = (tcp_poll_t *)malloc(sizeof(tcp_poll_t));
    mypollfd severfd;
    static mypollfd *pollfds = NULL;               // here for dynamic alloc for poll_fds
    static sensor_ts_t *client_last_record = NULL; // here for timeout check
    sensor_data_t *sensor_data = (sensor_data_t *)malloc(sizeof(sensor_data_t));
    int status = tcp_passive_open(&server, port); // open the server
    if (status != TCP_NO_ERROR)
    {
        asprintf(&log_message, "No such server port defined.\n");
        send_into_pipe(pipe_mutex, pipe_fd, log_message);
        printf("Error: Failed to create server socket.\n");
        exit(EXIT_FAILURE);
    }
    status = tcp_get_sd(server, &(severfd.fd)); // get the sd from the socket to bond
    if (status != TCP_NO_ERROR)
    {
        printf("socket not yet bonded.\n");
        exit(EXIT_FAILURE);
    }
    // initialize the server
    severfd.events = POLLIN;
    server_poll->sock_ptr = server;
    server_poll->last_record = (sensor_ts_t)time(NULL);
    server_poll->fds = severfd;
    server_poll->sensor_id = 0;
    server_poll->is_new = true;

    // Initialize the connections list
    connections = dpl_create(&client_copy, &client_free, &client_compare);
    connections = dpl_insert_at_index(connections, (void *)server_poll, 0, true);

    tcpsock_t *client_socket = NULL;
    mypollfd clientfd;
    pollfds = (mypollfd *)malloc(sizeof(mypollfd)); // Initially array for 1 element
    client_last_record = (sensor_ts_t *)malloc(sizeof(sensor_ts_t));
    // // Initialize the connections list
    pollfds[0].fd = -1;        // Set an invalid value initially
    pollfds[0].events = 0;     // No events initially
    client_last_record[0] = 0; // No effective timestamp
    pollfds[0] = severfd;      // put the server fds in the first element of the array
    client_last_record[0] = time(NULL);
    int res;
    printf("start to have data\n");
    // in this loop, it always listen to the client coming until there is no client coming for 10s
    while ((res = poll(pollfds, dpl_size(connections), TIMEOUT * 1000))) // this sentence is always listen to any change of the socket bonded with the server(include the server and client)
    {
        // everytime to check if there is new connection coming if there are new connection then add
        // it into the socket dplist.
        if (pollfds[0].revents == POLLIN)
        {
            if (tcp_wait_for_connection(server, &client_socket) != TCP_NO_ERROR)
            {
                printf("the connect with client is not get.\n");
                exit(EXIT_FAILURE);
            }
            else
            {
                // Add the new client to the connections list
                if (tcp_get_sd(client_socket, &(clientfd.fd)) != TCP_NO_ERROR)
                {
                    printf("the socket of client is not bond yet.\n");
                    exit(EXIT_FAILURE);
                }
                else
                {
                    tcp_poll_t *client_poll = (tcp_poll_t *)malloc(sizeof(tcp_poll_t));
                    // get initial the client poll information
                    clientfd.events = POLLIN | POLLRDHUP;
                    client_poll->last_record = (sensor_ts_t)time(NULL);
                    client_poll->sock_ptr = client_socket;
                    client_poll->fds = clientfd;
                    client_poll->is_new = true;
                    client_poll->sensor_id = 0;
                    connections = dpl_insert_at_index(connections, client_poll, dpl_size(connections), true);
                    int conn_size =dpl_size(connections);
                    mypollfd *new_pollfds = (mypollfd *)realloc(pollfds, sizeof(mypollfd) * (conn_size+1));
                    sensor_ts_t *new_client_last_record = (sensor_ts_t *)realloc(client_last_record, sizeof(sensor_ts_t) * (conn_size+1));

                    if (new_pollfds == NULL || new_client_last_record == NULL)
                    {
                        // Handle allocation failure
                        // Maybe free the old memory and return an error
                    }
                    else
                    {
                        pollfds = new_pollfds;
                        client_last_record = new_client_last_record;

                        // Initialize newly added memory, if any
                        for (int i = 1; i <= conn_size; i++)
                        {
                            pollfds[i].fd = -1;
                            pollfds[i].events = 0;
                            client_last_record[i] = 0;
                        }
                    }
                    free(client_poll);
                }
            }
        }
        tcp_poll_t *temp_poll = NULL;
        // get all the fds in to the array (every time when the fds get refreshed it will pass it to the array )
        for (int i = 1; i < dpl_size(connections); i++)
        {
            temp_poll = (tcp_poll_t *)dpl_get_element_at_index(connections, i);
            pollfds[i] = temp_poll->fds;
            client_last_record[i] = temp_poll->last_record;
        }
        // go through the dplist to print the poll information(the data get from the client, the index 0 is the server, and index 1 is the client)
        tcp_poll_t *pollinform = NULL;
        for (int i = 1; i < dpl_size(connections); i++)
        {
            pollinform = (tcp_poll_t *)dpl_get_element_at_index(connections, i);
            int back = poll(&pollfds[i], 2, 0);
            // which means there is new information coming
            if (back)
            {
                if (pollfds[i].revents == POLLIN)
                {
                    // get the sensor id
                    int result;
                    int byte = sizeof(sensor_data->id);
                    result = tcp_receive(pollinform->sock_ptr, (void *)&(sensor_data->id), &byte);
                    pollinform->sensor_id = sensor_data->id;
                    // get the sensor value
                    byte = sizeof(sensor_data->value);
                    result = tcp_receive(pollinform->sock_ptr, (void *)&(sensor_data->value), &byte);
                    // get the timestamp
                    byte = sizeof(sensor_data->ts);
                    result = tcp_receive(pollinform->sock_ptr, (void *)&(sensor_data->ts), &byte);
                    if ((result == TCP_NO_ERROR) && byte)
                    {
                        printf("sensor id = %" PRIu16 " - temperature = %g - timestamp = %ld\n", sensor_data->id, sensor_data->value,
                               (long int)sensor_data->ts);
                        sbuffer_insert(buffer, sensor_data, database_fail);
                    }
                    // to check if is new node
                    if (pollinform->is_new)
                    {
                        printf("new sensor node %d is open\n", pollinform->sensor_id);
                        // Send log message to child process
                        asprintf(&log_message, "new sensor node %d is open\n", pollinform->sensor_id);
                        send_into_pipe(pipe_mutex, pipe_fd, log_message);
                        pollinform->is_new = false; // set the is new to false
                    }
                    // reset the node timestamp
                    pollinform->last_record = time(NULL);
                }
                // if the connection is closed handle it
                if (pollfds[i].revents & POLLRDHUP)
                {
                    // Send log message to child process
                    asprintf(&log_message, "the sensor node id: %d is closed connection\n", pollinform->sensor_id);
                    send_into_pipe(pipe_mutex, pipe_fd, log_message);
                    printf("the sensor node id: %d is closed connection\n", pollinform->sensor_id);
                    // if the connection is closed then delete the node from the dplist
                    // close the tcpconncetion
                    tcp_close(&(pollinform->sock_ptr));
                    dpl_remove_at_index(connections, i, true);
                }
            }
            // here if there is a timeout will be get if poll don't get any change of this client
            if (difftime((sensor_ts_t)time(NULL), client_last_record[i]) > TIMEOUT)
            { // we should check if this sensor is active in timeout period
                // send to child
                asprintf(&log_message, "the sensor node id: %d is closed connection\n", pollinform->sensor_id);
                send_into_pipe(pipe_mutex, pipe_fd, log_message);
                printf("this sensor node id:%d is timeout\n", pollinform->sensor_id);
                tcp_close(&(pollinform->sock_ptr));
                dpl_remove_at_index(connections, i, true);
            }
        }
    }
    // close the connection after the timeout of the server
    printf("the server is not active, it closed\n");
    tcp_close(&server);
    dpl_free(&connections, true);
    free(server_poll);
    free(sensor_data);
    free(pollfds);
    free(client_last_record);
}

void connmgr_free()
{
    // Close all client connections and free the memory used by client connections
    while (dpl_size(connections) > 0)
    {
        tcp_poll_t *client = (tcp_poll_t *)dpl_get_element_at_index(connections, 0);
        tcp_close(&(client->sock_ptr));
        dpl_remove_at_index(connections, 0, true);
    }
    // Free the memory used by the connections list
    dpl_free(&connections, true);
}