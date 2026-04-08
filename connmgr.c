#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "connmgr.h"
#include "lib/tcpsock.h"
#include "sbuffer.h"

#ifndef TIMEOUT
#error TIMEOUT not defined
#endif

/* One detached worker thread per active TCP sender connection. */
typedef struct client_ctx {
    tcpsock_t *client;
    struct client_ctx *next;
} client_ctx_t;

/* In-memory copy of room_sensor.map entries (room -> sensor). */
typedef struct {
    uint16_t room_id;
    sensor_id_t sensor_id;
} sensor_map_entry_t;

static sbuffer_t **shared_buffer = NULL;
/*
 * Protects shared runtime state:
 * - client linked list and active client count
 * - counters and last_data_timestamp
 * - receiver raw data file append section
 */
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static client_ctx_t *client_list = NULL;
static int active_clients = 0;
static unsigned long long total_received = 0;
static unsigned long long total_dropped = 0;
static unsigned long long total_rejected = 0;
static time_t last_data_timestamp = 0;
static FILE *receiver_data_file = NULL;
static sensor_map_entry_t *sensor_map_entries = NULL;
static size_t sensor_map_count = 0;

static void free_sensor_map(void)
{
    free(sensor_map_entries);
    sensor_map_entries = NULL;
    sensor_map_count = 0;
}

static int load_sensor_map(void)
{
    FILE *map_file;
    sensor_map_entry_t *entries;
    size_t capacity = 16;
    size_t count = 0;
    uint16_t room_id;
    uint16_t sensor_id;

    map_file = fopen("room_sensor.map", "r");
    if (map_file == NULL) {
        return -1;
    }

    entries = malloc(capacity * sizeof(*entries));
    if (entries == NULL) {
        fclose(map_file);
        return -1;
    }

    /* Input format: <room_id> <sensor_id> per line. */
    while (fscanf(map_file, "%hu %hu", &room_id, &sensor_id) == 2) {
        if (count == capacity) {
            sensor_map_entry_t *resized;

            capacity *= 2;
            resized = realloc(entries, capacity * sizeof(*entries));
            if (resized == NULL) {
                free(entries);
                fclose(map_file);
                return -1;
            }
            entries = resized;
        }

        entries[count].room_id = room_id;
        entries[count].sensor_id = (sensor_id_t)sensor_id;
        count++;
    }

    fclose(map_file);
    if (count == 0) {
        free(entries);
        return -1;
    }

    free_sensor_map();
    sensor_map_entries = entries;
    sensor_map_count = count;
    return 0;
}

static bool is_valid_sensor_pair(uint16_t room_id, sensor_id_t sensor_id)
{
    /* Linear scan is acceptable for this assignment-size map. */
    for (size_t i = 0; i < sensor_map_count; i++) {
        if (sensor_map_entries[i].room_id == room_id &&
            sensor_map_entries[i].sensor_id == sensor_id) {
            return true;
        }
    }
    return false;
}

static void log_rejected_pair(uint16_t room_id, sensor_id_t sensor_id)
{
    /*
     * Keep this independent from datamgr log handle:
     * connmgr can reject a pair before it enters the shared buffer pipeline.
     */
    FILE *log_file = fopen(FIFO_LOG, "a");

    if (log_file == NULL) return;
    fprintf(
        log_file,
        "REJECT_INVALID_PAIR room=%hu sensor=%hu reason=not_in_map\n",
        room_id,
        sensor_id
    );
    fflush(log_file);
    fclose(log_file);
}

static void reset_runtime_stats(void)
{
    pthread_mutex_lock(&state_mutex);
    client_list = NULL;
    active_clients = 0;
    total_received = 0;
    total_dropped = 0;
    total_rejected = 0;
    last_data_timestamp = time(NULL);
    pthread_mutex_unlock(&state_mutex);
}

static void register_client(client_ctx_t *ctx)
{
    pthread_mutex_lock(&state_mutex);
    ctx->next = client_list;
    client_list = ctx;
    active_clients++;
    pthread_mutex_unlock(&state_mutex);
}

static void unregister_client(client_ctx_t *ctx)
{
    client_ctx_t **cursor;

    pthread_mutex_lock(&state_mutex);
    cursor = &client_list;
    while (*cursor != NULL && *cursor != ctx) {
        cursor = &(*cursor)->next;
    }
    if (*cursor == ctx) {
        *cursor = ctx->next;
    }
    if (active_clients > 0) {
        active_clients--;
    }
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

static time_t get_last_data_timestamp(void)
{
    time_t timestamp;

    pthread_mutex_lock(&state_mutex);
    timestamp = last_data_timestamp;
    pthread_mutex_unlock(&state_mutex);
    return timestamp;
}

static void update_receive_stats(bool dropped)
{
    pthread_mutex_lock(&state_mutex);
    total_received++;
    if (dropped) total_dropped++;
    last_data_timestamp = time(NULL);
    pthread_mutex_unlock(&state_mutex);
}

static void update_rejected_stats(void)
{
    pthread_mutex_lock(&state_mutex);
    total_rejected++;
    pthread_mutex_unlock(&state_mutex);
}

static void append_receiver_measurement(const sensor_data_t *data)
{
    if (data == NULL) return;

    /* Serialized append to avoid interleaved writes from multiple workers. */
    pthread_mutex_lock(&state_mutex);
    if (receiver_data_file != NULL) {
        fprintf(
            receiver_data_file,
            "%hu %hu %.2f %ld\n",
            data->room_id,
            data->sensor_id,
            data->value,
            (long)data->timestamp
        );
    }
    pthread_mutex_unlock(&state_mutex);
}

static void shutdown_all_clients(void)
{
    client_ctx_t *ctx;

    pthread_mutex_lock(&state_mutex);
    for (ctx = client_list; ctx != NULL; ctx = ctx->next) {
        int sd;

        if (tcp_get_sd(ctx->client, &sd) == TCP_NO_ERROR) {
            shutdown(sd, SHUT_RDWR);
        }
    }
    pthread_mutex_unlock(&state_mutex);
}

static int receive_measurement(tcpsock_t *client, sensor_data_t *data)
{
    int bytes;
    int rc;

    /* Wire format order must match sender:
     * sensor_id -> room_id -> value -> timestamp.
     */
    bytes = sizeof(data->sensor_id);
    rc = tcp_receive(client, &data->sensor_id, &bytes);
    if (rc != TCP_NO_ERROR) return rc;

    bytes = sizeof(data->room_id);
    rc = tcp_receive(client, &data->room_id, &bytes);
    if (rc != TCP_NO_ERROR) return rc;

    bytes = sizeof(data->value);
    rc = tcp_receive(client, &data->value, &bytes);
    if (rc != TCP_NO_ERROR) return rc;

    bytes = sizeof(data->timestamp);
    rc = tcp_receive(client, &data->timestamp, &bytes);
    if (rc != TCP_NO_ERROR) return rc;

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
    sensor_data_t data;

    while (receive_measurement(ctx->client, &data) == TCP_NO_ERROR) {
        int insert_rc;

        /* Reject invalid map pairs early and drop this connection. */
        if (!is_valid_sensor_pair(data.room_id, data.sensor_id)) {
            log_rejected_pair(data.room_id, data.sensor_id);
            update_rejected_stats();
            break;
        }

        append_receiver_measurement(&data);
        insert_rc = sbuffer_insert(*shared_buffer, &data);
        if (insert_rc == SBUFFER_FAILURE) break;
        update_receive_stats(insert_rc == SBUFFER_DROPPED);
    }

    tcp_close(&ctx->client);
    unregister_client(ctx);
    free(ctx);
    return NULL;
}

void *connmgr_listen(sbuffer_t **sbuffer, int port, int timeout_seconds)
{
    tcpsock_t *server = NULL;
    int server_sd;

    if (sbuffer == NULL || *sbuffer == NULL) return NULL;
    if (port <= 0) return NULL;
    if (timeout_seconds < 0) timeout_seconds = TIMEOUT;

    shared_buffer = sbuffer;
    reset_runtime_stats();
    if (load_sensor_map() != 0) {
        fprintf(stderr, "Unable to load room_sensor.map for validation\n");
        sbuffer_close(*shared_buffer);
        return NULL;
    }

    receiver_data_file = fopen(RECEIVER_DATA_LOG, "w");
    if (receiver_data_file != NULL) {
        setvbuf(receiver_data_file, NULL, _IOLBF, 0);
    } else {
        perror("fopen sensor_data_recv.txt");
    }

    if (tcp_passive_open(&server, port) != TCP_NO_ERROR) {
        fprintf(stderr, "Unable to start TCP server on port %d\n", port);
        if (receiver_data_file != NULL) {
            fclose(receiver_data_file);
            receiver_data_file = NULL;
        }
        free_sensor_map();
        sbuffer_close(*shared_buffer);
        return NULL;
    }

    if (tcp_get_sd(server, &server_sd) != TCP_NO_ERROR) {
        tcp_close(&server);
        if (receiver_data_file != NULL) {
            fclose(receiver_data_file);
            receiver_data_file = NULL;
        }
        free_sensor_map();
        sbuffer_close(*shared_buffer);
        return NULL;
    }

    if (timeout_seconds == 0) {
        printf("Connection manager listening on port %d (idle timeout: disabled)\n", port);
    } else {
        printf(
            "Connection manager listening on port %d (idle timeout after %d sec without data)\n",
            port,
            timeout_seconds
        );
    }

    while (1) {
        fd_set readfds;
        struct timeval poll_timeout;
        int ready;

        FD_ZERO(&readfds);
        FD_SET(server_sd, &readfds);
        poll_timeout.tv_sec = 1;
        poll_timeout.tv_usec = 0;

        ready = select(server_sd + 1, &readfds, NULL, NULL, &poll_timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (ready > 0 && FD_ISSET(server_sd, &readfds)) {
            tcpsock_t *client = NULL;
            client_ctx_t *ctx;
            pthread_t worker;

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
            ctx->next = NULL;
            register_client(ctx);

            if (pthread_create(&worker, NULL, client_thread, ctx) != 0) {
                perror("pthread_create");
                unregister_client(ctx);
                tcp_close(&client);
                free(ctx);
                break;
            }
            pthread_detach(worker);
        }

        if (timeout_seconds > 0) {
            time_t now = time(NULL);
            time_t last_data = get_last_data_timestamp();

            /* Idle timeout is based on "last accepted data time", not connects. */
            if (now - last_data >= timeout_seconds) {
                printf(
                    "Connection manager idle timeout reached: %d seconds without incoming data\n",
                    timeout_seconds
                );
                break;
            }
        }
    }

    /* Stop producing and wake consumers before waiting workers to drain. */
    shutdown_all_clients();
    sbuffer_close(*shared_buffer);

    while (get_active_clients() > 0) {
        usleep(100000);
    }

    if (server != NULL) {
        tcp_close(&server);
    }

    pthread_mutex_lock(&state_mutex);
    if (receiver_data_file != NULL) {
        fclose(receiver_data_file);
        receiver_data_file = NULL;
    }
    printf(
        "Connection manager stopped. total received=%llu, dropped=%llu, rejected=%llu (queue dropped=%llu)\n",
        total_received,
        total_dropped,
        total_rejected,
        sbuffer_get_drop_count(*shared_buffer)
    );
    pthread_mutex_unlock(&state_mutex);
    free_sensor_map();
    return NULL;
}

void connmgr_free(tcpsock_t *point)
{
    tcp_close(&point);
}
