#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include "config.h"
#include "connmgr.h"
#include "lib/tcpsock.h"
#include "sbuffer.h"

#ifndef TIMEOUT
#error TIMEOUT not defined
#endif

typedef struct {
    tcpsock_t *client;
} client_ctx_t;

static sbuffer_t **shared_buffer = NULL;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static int active_clients = 0;
static unsigned long long total_received = 0;
static unsigned long long total_dropped = 0;

static void reset_runtime_stats(void)
{
    pthread_mutex_lock(&state_mutex);
    active_clients = 0;
    total_received = 0;
    total_dropped = 0;
    pthread_mutex_unlock(&state_mutex);
}

static void update_active_clients(int delta)
{
    pthread_mutex_lock(&state_mutex);
    active_clients += delta;
    pthread_mutex_unlock(&state_mutex);
}

static int get_active_clients(void)
{
    int count;

    pthread_mutex_lock(&state_mutex);
    count = active_clients;
    pthread_mutex_unlock(&state_mutex);
    return count;
}

static void update_receive_stats(bool dropped)
{
    pthread_mutex_lock(&state_mutex);
    total_received++;
    if (dropped) total_dropped++;
    pthread_mutex_unlock(&state_mutex);
}

static int receive_measurement(tcpsock_t *client, sensor_data_t *data)
{
    int bytes;
    int rc;

    bytes = sizeof(data->sensor_id);
    rc = tcp_receive(client, &data->sensor_id, &bytes);
    if (rc != TCP_NO_ERROR) return rc;

    bytes = sizeof(data->value);
    rc = tcp_receive(client, &data->value, &bytes);
    if (rc != TCP_NO_ERROR) return rc;

    bytes = sizeof(data->timestamp);
    rc = tcp_receive(client, &data->timestamp, &bytes);
    if (rc != TCP_NO_ERROR) return rc;

    data->room_id = 0;
    data->RUN_AVG = 0;
    data->sample_count = 0;
    data->alert_state = 0;
    for (size_t i = 0; i < RUN_AVG_LENGTH; i++) {
        data->temperatures[i] = 0;
    }
    return TCP_NO_ERROR;
}

static void *client_thread(void *arg)
{
    client_ctx_t *ctx = arg;
    tcpsock_t *client = ctx->client;
    sensor_data_t data;

    free(ctx);

    while (receive_measurement(client, &data) == TCP_NO_ERROR) {
        int insert_rc = sbuffer_insert(*shared_buffer, &data);

        if (insert_rc == SBUFFER_FAILURE) break;
        update_receive_stats(insert_rc == SBUFFER_DROPPED);
    }

    tcp_close(&client);
    update_active_clients(-1);
    return NULL;
}

void *connmgr_listen(sbuffer_t **sbuffer, int port, int timeout_seconds)
{
    tcpsock_t *server = NULL;
    int server_sd;

    if (sbuffer == NULL || *sbuffer == NULL) return NULL;
    if (port <= 0) return NULL;
    if (timeout_seconds <= 0) timeout_seconds = TIMEOUT;

    shared_buffer = sbuffer;
    reset_runtime_stats();

    if (tcp_passive_open(&server, port) != TCP_NO_ERROR) {
        fprintf(stderr, "Unable to start TCP server on port %d\n", port);
        sbuffer_close(*shared_buffer);
        return NULL;
    }

    if (tcp_get_sd(server, &server_sd) != TCP_NO_ERROR) {
        tcp_close(&server);
        sbuffer_close(*shared_buffer);
        return NULL;
    }

    printf(
        "Connection manager listening on port %d (idle timeout: %d sec)\n",
        port,
        timeout_seconds
    );

    while (1) {
        fd_set readfds;
        struct timeval accept_timeout;
        int ready;
        tcpsock_t *client = NULL;
        client_ctx_t *ctx;
        pthread_t worker;

        FD_ZERO(&readfds);
        FD_SET(server_sd, &readfds);
        accept_timeout.tv_sec = timeout_seconds;
        accept_timeout.tv_usec = 0;

        ready = select(server_sd + 1, &readfds, NULL, NULL, &accept_timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (ready == 0) break;

        if (tcp_wait_for_connection(server, &client) != TCP_NO_ERROR) {
            fprintf(stderr, "Failed to accept an incoming connection\n");
            continue;
        }

        ctx = malloc(sizeof(*ctx));
        if (ctx == NULL) {
            tcp_close(&client);
            perror("malloc");
            break;
        }
        ctx->client = client;
        update_active_clients(1);

        if (pthread_create(&worker, NULL, client_thread, ctx) != 0) {
            perror("pthread_create");
            update_active_clients(-1);
            tcp_close(&client);
            free(ctx);
            break;
        }

        pthread_detach(worker);
    }

    while (get_active_clients() > 0) {
        usleep(100000);
    }

    if (server != NULL) {
        tcp_close(&server);
    }
    sbuffer_close(*shared_buffer);

    pthread_mutex_lock(&state_mutex);
    printf(
        "Connection manager stopped. total received=%llu, dropped=%llu (queue dropped=%llu)\n",
        total_received,
        total_dropped,
        sbuffer_get_drop_count(*shared_buffer)
    );
    pthread_mutex_unlock(&state_mutex);
    return NULL;
}

void connmgr_free(tcpsock_t *point)
{
    tcp_close(&point);
}
