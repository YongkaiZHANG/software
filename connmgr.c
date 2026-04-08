#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "connmgr.h"
#include "lib/tcpsock.h"

#ifndef TIMEOUT
#error TIMEOUT not defined
#endif

typedef struct worker_proc {
    pid_t pid;
    struct worker_proc *next;
} worker_proc_t;

typedef struct {
    uint16_t room_id;
    sensor_id_t sensor_id;
} sensor_map_entry_t;

static int datamgr_pipe_fd = -1;
static int stats_pipe_read_fd = -1;
static int stats_pipe_write_fd = -1;
static int receiver_data_fd = -1;
static int server_socket_fd = -1;
static worker_proc_t *worker_list = NULL;
static sensor_map_entry_t *sensor_map_entries = NULL;
static size_t sensor_map_count = 0;
static unsigned long long total_received = 0;
static unsigned long long total_rejected = 0;
static time_t last_data_timestamp = 0;

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
    for (size_t i = 0; i < sensor_map_count; i++) {
        if (sensor_map_entries[i].room_id == room_id &&
            sensor_map_entries[i].sensor_id == sensor_id) {
            return true;
        }
    }
    return false;
}

static int write_atomic_message(int fd, const void *buffer, size_t size)
{
    ssize_t written;

    do {
        written = write(fd, buffer, size);
    } while (written < 0 && errno == EINTR);

    if (written < 0 || (size_t)written != size) {
        return -1;
    }
    return 0;
}

static int append_line_to_fd(int fd, const char *line)
{
    size_t len = 0;

    if (fd < 0 || line == NULL) return -1;
    while (line[len] != '\0') {
        len++;
    }
    return write_atomic_message(fd, line, len);
}

static void log_rejected_pair(uint16_t room_id, sensor_id_t sensor_id)
{
    char line[128];
    int log_fd;

    snprintf(
        line,
        sizeof(line),
        "REJECT_INVALID_PAIR room=%hu sensor=%hu reason=not_in_map\n",
        room_id,
        sensor_id
    );
    log_fd = open(FIFO_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) return;
    (void)append_line_to_fd(log_fd, line);
    close(log_fd);
}

static int append_receiver_measurement(const sensor_data_t *data)
{
    char line[128];

    if (receiver_data_fd < 0 || data == NULL) return -1;
    snprintf(
        line,
        sizeof(line),
        "%hu %hu %.2f %ld\n",
        data->room_id,
        data->sensor_id,
        data->value,
        (long)data->timestamp
    );
    return append_line_to_fd(receiver_data_fd, line);
}

static int notify_parent(char event_code)
{
    if (stats_pipe_write_fd < 0) return -1;
    return write_atomic_message(stats_pipe_write_fd, &event_code, sizeof(event_code));
}

static int forward_measurement(const sensor_data_t *data)
{
    if (datamgr_pipe_fd < 0 || data == NULL) return -1;
    return write_atomic_message(datamgr_pipe_fd, data, sizeof(*data));
}

static int receive_measurement(tcpsock_t *client, sensor_data_t *data)
{
    int bytes;
    int rc;

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

static void tcp_close_local_copy(tcpsock_t **socket)
{
    if (socket == NULL || *socket == NULL) return;
    if ((*socket)->ip_addr != NULL) {
        free((*socket)->ip_addr);
    }
    if ((*socket)->sd >= 0) {
        close((*socket)->sd);
    }
    free(*socket);
    *socket = NULL;
}

static void shutdown_client_socket(tcpsock_t *client)
{
    int sd;

    if (client == NULL) return;
    if (tcp_get_sd(client, &sd) == TCP_NO_ERROR) {
        shutdown(sd, SHUT_RDWR);
    }
}

static void worker_process(tcpsock_t *client)
{
    sensor_data_t data;

    if (stats_pipe_read_fd >= 0) {
        close(stats_pipe_read_fd);
        stats_pipe_read_fd = -1;
    }
    if (server_socket_fd >= 0) {
        close(server_socket_fd);
        server_socket_fd = -1;
    }

    while (receive_measurement(client, &data) == TCP_NO_ERROR) {
        if (!is_valid_sensor_pair(data.room_id, data.sensor_id)) {
            log_rejected_pair(data.room_id, data.sensor_id);
            (void)notify_parent('R');
            shutdown_client_socket(client);
            break;
        }

        if (append_receiver_measurement(&data) != 0) {
            break;
        }
        if (forward_measurement(&data) != 0) {
            shutdown_client_socket(client);
            break;
        }
        if (notify_parent('M') != 0) {
            shutdown_client_socket(client);
            break;
        }
    }

    tcp_close(&client);
    if (stats_pipe_write_fd >= 0) close(stats_pipe_write_fd);
    if (datamgr_pipe_fd >= 0) close(datamgr_pipe_fd);
    if (receiver_data_fd >= 0) close(receiver_data_fd);
    _exit(EXIT_SUCCESS);
}

static void add_worker(pid_t pid)
{
    worker_proc_t *worker = malloc(sizeof(*worker));

    if (worker == NULL) return;
    worker->pid = pid;
    worker->next = worker_list;
    worker_list = worker;
}

static void remove_worker(pid_t pid)
{
    worker_proc_t **cursor = &worker_list;

    while (*cursor != NULL) {
        if ((*cursor)->pid == pid) {
            worker_proc_t *victim = *cursor;

            *cursor = victim->next;
            free(victim);
            return;
        }
        cursor = &(*cursor)->next;
    }
}

static void reap_finished_workers(void)
{
    int status;
    pid_t pid;

    do {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            remove_worker(pid);
        }
    } while (pid > 0);
}

static void kill_all_workers(void)
{
    worker_proc_t *worker = worker_list;

    while (worker != NULL) {
        kill(worker->pid, SIGTERM);
        worker = worker->next;
    }
}

static void wait_all_workers(void)
{
    while (worker_list != NULL) {
        worker_proc_t *worker = worker_list;

        if (waitpid(worker->pid, NULL, 0) >= 0 || errno == ECHILD) {
            remove_worker(worker->pid);
        }
    }
}

static int set_nonblocking(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

static void drain_worker_events(void)
{
    char event_code;
    ssize_t rc;

    if (stats_pipe_read_fd < 0) return;

    while (true) {
        rc = read(stats_pipe_read_fd, &event_code, sizeof(event_code));
        if (rc == 0) {
            break;
        }
        if (rc < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        }

        if (event_code == 'M') {
            total_received++;
            last_data_timestamp = time(NULL);
        } else if (event_code == 'R') {
            total_rejected++;
        }
    }
}

int connmgr_listen(int pipe_write_fd, int port, int timeout_seconds)
{
    tcpsock_t *server = NULL;
    int stats_pipe[2];
    int exit_code = EXIT_SUCCESS;

    if (pipe_write_fd < 0 || port <= 0) return EXIT_FAILURE;
    if (timeout_seconds < 0) timeout_seconds = TIMEOUT;

    signal(SIGPIPE, SIG_IGN);
    datamgr_pipe_fd = pipe_write_fd;
    last_data_timestamp = time(NULL);
    total_received = 0;
    total_rejected = 0;

    if (load_sensor_map() != 0) {
        fprintf(stderr, "Unable to load room_sensor.map for validation\n");
        return EXIT_FAILURE;
    }

    receiver_data_fd = open(
        RECEIVER_DATA_LOG,
        O_WRONLY | O_CREAT | O_TRUNC | O_APPEND,
        0644
    );
    if (receiver_data_fd < 0) {
        perror("open sensor_data_recv.txt");
        free_sensor_map();
        return EXIT_FAILURE;
    }

    if (pipe(stats_pipe) != 0) {
        perror("pipe");
        close(receiver_data_fd);
        receiver_data_fd = -1;
        free_sensor_map();
        return EXIT_FAILURE;
    }
    stats_pipe_read_fd = stats_pipe[0];
    stats_pipe_write_fd = stats_pipe[1];
    if (set_nonblocking(stats_pipe_read_fd) != 0) {
        perror("fcntl");
        exit_code = EXIT_FAILURE;
        goto cleanup;
    }

    if (tcp_passive_open(&server, port) != TCP_NO_ERROR) {
        fprintf(stderr, "Unable to start TCP server on port %d\n", port);
        exit_code = EXIT_FAILURE;
        goto cleanup;
    }
    if (tcp_get_sd(server, &server_socket_fd) != TCP_NO_ERROR) {
        exit_code = EXIT_FAILURE;
        goto cleanup;
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

    while (true) {
        fd_set readfds;
        struct timeval poll_timeout;
        int max_fd;
        int ready;

        reap_finished_workers();
        FD_ZERO(&readfds);
        FD_SET(server_socket_fd, &readfds);
        FD_SET(stats_pipe_read_fd, &readfds);
        max_fd = server_socket_fd > stats_pipe_read_fd ? server_socket_fd : stats_pipe_read_fd;
        poll_timeout.tv_sec = 1;
        poll_timeout.tv_usec = 0;

        ready = select(max_fd + 1, &readfds, NULL, NULL, &poll_timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            exit_code = EXIT_FAILURE;
            break;
        }

        if (ready > 0 && FD_ISSET(stats_pipe_read_fd, &readfds)) {
            drain_worker_events();
        }

        if (ready > 0 && FD_ISSET(server_socket_fd, &readfds)) {
            tcpsock_t *client = NULL;
            pid_t pid;

            if (tcp_wait_for_connection(server, &client) != TCP_NO_ERROR) {
                fprintf(stderr, "Failed to accept an incoming connection\n");
            } else {
                pid = fork();
                if (pid < 0) {
                    perror("fork");
                    tcp_close_local_copy(&client);
                    exit_code = EXIT_FAILURE;
                    break;
                }
                if (pid == 0) {
                    tcp_close_local_copy(&server);
                    worker_process(client);
                }
                add_worker(pid);
                tcp_close_local_copy(&client);
            }
        }

        if (timeout_seconds > 0) {
            time_t now = time(NULL);

            if (now - last_data_timestamp >= timeout_seconds) {
                printf(
                    "Connection manager idle timeout reached: %d seconds without incoming data\n",
                    timeout_seconds
                );
                break;
            }
        }
    }

cleanup:
    kill_all_workers();
    if (server != NULL) {
        tcp_close(&server);
        server = NULL;
    }
    if (server_socket_fd >= 0) {
        close(server_socket_fd);
        server_socket_fd = -1;
    }
    if (datamgr_pipe_fd >= 0) {
        close(datamgr_pipe_fd);
        datamgr_pipe_fd = -1;
    }
    if (stats_pipe_write_fd >= 0) {
        close(stats_pipe_write_fd);
        stats_pipe_write_fd = -1;
    }
    wait_all_workers();
    drain_worker_events();
    if (stats_pipe_read_fd >= 0) {
        close(stats_pipe_read_fd);
        stats_pipe_read_fd = -1;
    }
    if (receiver_data_fd >= 0) {
        close(receiver_data_fd);
        receiver_data_fd = -1;
    }

    printf(
        "Connection manager stopped. total received=%llu, dropped=%llu, rejected=%llu (queue dropped=%llu)\n",
        total_received,
        0ULL,
        total_rejected,
        0ULL
    );
    free_sensor_map();
    return exit_code;
}

void connmgr_free(tcpsock_t *point)
{
    tcp_close(&point);
}
