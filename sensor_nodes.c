/**
 * \author Luc Vandeurzen
 */

#include <inttypes.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "lib/tcpsock.h"

#ifndef LOOPS
#define LOOPS 0
#endif

#ifdef LOG_SENSOR_DATA

#define LOG_FILE "sensor_log"

#define LOG_OPEN()                      \
    FILE *fp_log;                      \
    do {                               \
        fp_log = fopen(LOG_FILE, "w"); \
        if (fp_log == NULL) {          \
            printf("%s\n", "couldn't create log file"); \
            exit(EXIT_FAILURE);        \
        }                              \
    } while (0)

#define LOG_PRINTF(sensor_id, temperature, timestamp)                                    \
    do {                                                                                 \
        fprintf(fp_log, "%" PRIu16 " %g %ld\n", (sensor_id), (temperature), (long)(timestamp)); \
        fflush(fp_log);                                                                  \
    } while (0)

#define LOG_CLOSE() fclose(fp_log)

#else
#define LOG_OPEN(...) (void)0
#define LOG_PRINTF(...) (void)0
#define LOG_CLOSE(...) (void)0
#endif

#define INITIAL_TEMPERATURE 20
#define TEMP_DEV 5

static void print_help(void);
static int parse_nonnegative_seconds(const char *text, double *seconds_out);
static int parse_nonnegative_loops(const char *text, long *loops_out);
static int parse_u16(const char *text, uint16_t *value_out);
static int parse_port(const char *text, int *port_out);
static int looks_like_ipv4(const char *text);
static void sleep_for_seconds(double seconds);

int main(int argc, char *argv[])
{
    sensor_data_t data = {0};
    tcpsock_t *client = NULL;
    char server_ip[16] = {0};
    int server_port;
    int bytes;
    double sleep_time = 0;
    long configured_loops = LOOPS;
    long sent_count = 0;
    struct timespec seed_ts;
    long seed;

    LOG_OPEN();

    if (argc != 6 && argc != 7) {
        print_help();
        LOG_CLOSE();
        return EXIT_FAILURE;
    }

    if (parse_u16(argv[1], &data.room_id) != 0) {
        fprintf(stderr, "Invalid room ID: %s\n", argv[1]);
        print_help();
        LOG_CLOSE();
        return EXIT_FAILURE;
    }
    if (parse_u16(argv[2], &data.sensor_id) != 0) {
        fprintf(stderr, "Invalid sensor ID: %s\n", argv[2]);
        print_help();
        LOG_CLOSE();
        return EXIT_FAILURE;
    }
    if (parse_nonnegative_seconds(argv[3], &sleep_time) != 0) {
        fprintf(stderr, "Invalid sleep time: %s\n", argv[3]);
        print_help();
        LOG_CLOSE();
        return EXIT_FAILURE;
    }
    if (!looks_like_ipv4(argv[4])) {
        fprintf(stderr, "Invalid server IP: %s\n", argv[4]);
        print_help();
        LOG_CLOSE();
        return EXIT_FAILURE;
    }
    strncpy(server_ip, argv[4], sizeof(server_ip) - 1);
    server_ip[sizeof(server_ip) - 1] = '\0';
    if (parse_port(argv[5], &server_port) != 0) {
        fprintf(stderr, "Invalid server port: %s\n", argv[5]);
        print_help();
        LOG_CLOSE();
        return EXIT_FAILURE;
    }
    if (argc == 7 && parse_nonnegative_loops(argv[6], &configured_loops) != 0) {
        fprintf(stderr, "Invalid loops: %s\n", argv[6]);
        print_help();
        LOG_CLOSE();
        return EXIT_FAILURE;
    }

    if (timespec_get(&seed_ts, TIME_UTC) != TIME_UTC) {
        seed_ts.tv_sec = time(NULL);
        seed_ts.tv_nsec = 0;
    }
    seed = (long)seed_ts.tv_nsec
         ^ (long)seed_ts.tv_sec
         ^ (long)getpid()
         ^ ((long)data.sensor_id << 16)
         ^ (long)data.room_id;
    srand48(seed);

    if (tcp_active_open(&client, server_port, server_ip) != TCP_NO_ERROR) {
        fprintf(
            stderr,
            "sender connect failed: room=%hu sensor=%hu target=%s:%d (receiver not running or wrong port)\n",
            data.room_id,
            data.sensor_id,
            server_ip,
            server_port
        );
        LOG_CLOSE();
        return EXIT_FAILURE;
    }

    if (configured_loops == 0) {
        printf(
            "sender started: room=%hu sensor=%hu sleep=%.6f target=%s:%d loops=infinite\n",
            data.room_id,
            data.sensor_id,
            sleep_time,
            server_ip,
            server_port
        );
    } else {
        printf(
            "sender started: room=%hu sensor=%hu sleep=%.6f target=%s:%d loops=%ld\n",
            data.room_id,
            data.sensor_id,
            sleep_time,
            server_ip,
            server_port,
            configured_loops
        );
    }

    data.value = INITIAL_TEMPERATURE;
    while (configured_loops == 0 || sent_count < configured_loops) {
        data.value = data.value + TEMP_DEV * ((drand48() - 0.5) / 10);
        time(&data.timestamp);

        bytes = sizeof(data.sensor_id);
        if (tcp_send(client, &data.sensor_id, &bytes) != TCP_NO_ERROR) {
            fprintf(
                stderr,
                "sender stopped: room=%hu sensor=%hu field=sensor_id send failed "
                "(receiver may close invalid pair)\n",
                data.room_id,
                data.sensor_id
            );
            tcp_close(&client);
            LOG_CLOSE();
            return EXIT_FAILURE;
        }

        bytes = sizeof(data.room_id);
        if (tcp_send(client, &data.room_id, &bytes) != TCP_NO_ERROR) {
            fprintf(
                stderr,
                "sender stopped: room=%hu sensor=%hu field=room_id send failed "
                "(receiver may close invalid pair)\n",
                data.room_id,
                data.sensor_id
            );
            tcp_close(&client);
            LOG_CLOSE();
            return EXIT_FAILURE;
        }

        bytes = sizeof(data.value);
        if (tcp_send(client, &data.value, &bytes) != TCP_NO_ERROR) {
            fprintf(
                stderr,
                "sender stopped: room=%hu sensor=%hu field=value send failed "
                "(receiver may close invalid pair)\n",
                data.room_id,
                data.sensor_id
            );
            tcp_close(&client);
            LOG_CLOSE();
            return EXIT_FAILURE;
        }

        bytes = sizeof(data.timestamp);
        if (tcp_send(client, &data.timestamp, &bytes) != TCP_NO_ERROR) {
            fprintf(
                stderr,
                "sender stopped: room=%hu sensor=%hu field=timestamp send failed "
                "(receiver may close invalid pair)\n",
                data.room_id,
                data.sensor_id
            );
            tcp_close(&client);
            LOG_CLOSE();
            return EXIT_FAILURE;
        }

        LOG_PRINTF(data.sensor_id, data.value, data.timestamp);
        sleep_for_seconds(sleep_time);
        sent_count++;
    }

    if (configured_loops == 0) {
        printf("sender stopped by external signal: room=%hu sensor=%hu\n", data.room_id, data.sensor_id);
    } else {
        printf(
            "sender completed loops and closed: room=%hu sensor=%hu sent=%ld\n",
            data.room_id,
            data.sensor_id,
            sent_count
        );
    }

    tcp_close(&client);
    LOG_CLOSE();
    return EXIT_SUCCESS;
}

static void print_help(void)
{
    printf("Use this format:\n");
    printf("  ./sensor_node <ROOM> <SENSOR> <SLEEP_SEC> <SERVER_IP> <SERVER_PORT> [LOOPS]\n");
    printf("Notes:\n");
    printf("  - SLEEP_SEC supports decimals (example: 0.001)\n");
    printf("  - LOOPS default is 0 (infinite send); set a positive number for finite send\n");
}

static int parse_nonnegative_seconds(const char *text, double *seconds_out)
{
    char *endptr = NULL;
    double value;

    if (text == NULL || seconds_out == NULL) return -1;

    errno = 0;
    value = strtod(text, &endptr);
    if (errno != 0 || endptr == text || *endptr != '\0' || value < 0) {
        return -1;
    }

    *seconds_out = value;
    return 0;
}

static int parse_nonnegative_loops(const char *text, long *loops_out)
{
    char *endptr = NULL;
    long value;

    if (text == NULL || loops_out == NULL) return -1;

    errno = 0;
    value = strtol(text, &endptr, 10);
    if (errno != 0 || endptr == text || *endptr != '\0' || value < 0) {
        return -1;
    }

    *loops_out = value;
    return 0;
}

static int parse_u16(const char *text, uint16_t *value_out)
{
    char *endptr = NULL;
    long value;

    if (text == NULL || value_out == NULL) return -1;

    errno = 0;
    value = strtol(text, &endptr, 10);
    if (errno != 0 || endptr == text || *endptr != '\0' || value < 0 || value > 65535) {
        return -1;
    }

    *value_out = (uint16_t)value;
    return 0;
}

static int parse_port(const char *text, int *port_out)
{
    char *endptr = NULL;
    long value;

    if (text == NULL || port_out == NULL) return -1;

    errno = 0;
    value = strtol(text, &endptr, 10);
    if (errno != 0 || endptr == text || *endptr != '\0' || value < 1 || value > 65535) {
        return -1;
    }

    *port_out = (int)value;
    return 0;
}

static int looks_like_ipv4(const char *text)
{
    struct in_addr addr;

    if (text == NULL) return 0;
    return inet_pton(AF_INET, text, &addr) == 1;
}

static void sleep_for_seconds(double seconds)
{
    time_t sec;
    long nsec;
    struct timespec req;
    struct timespec rem;

    if (seconds <= 0) return;

    sec = (time_t)seconds;
    nsec = (long)((seconds - (double)sec) * 1000000000.0);
    if (nsec >= 1000000000L) {
        sec++;
        nsec -= 1000000000L;
    }

    req.tv_sec = sec;
    req.tv_nsec = nsec;

    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
}
