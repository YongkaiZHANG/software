/**
 * \author Luc Vandeurzen
 */

#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "lib/tcpsock.h"

#ifndef LOOPS
#define LOOPS 5
#endif

#if (LOOPS == 0)
#define UPDATE(i) ((void)0)
#else
#define UPDATE(i) (--(i))
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
static void sleep_for_seconds(double seconds);

int main(int argc, char *argv[])
{
    sensor_data_t data = {0};
    tcpsock_t *client = NULL;
    char server_ip[16] = {0};
    int server_port;
    int bytes;
    double sleep_time = 0;
    int loop_count = (LOOPS == 0) ? 1 : LOOPS;

    LOG_OPEN();

    if (argc != 5) {
        print_help();
        LOG_CLOSE();
        return EXIT_FAILURE;
    }

    data.sensor_id = (sensor_id_t)atoi(argv[1]);
    if (parse_nonnegative_seconds(argv[2], &sleep_time) != 0) {
        fprintf(stderr, "Invalid sleep time: %s\n", argv[2]);
        print_help();
        LOG_CLOSE();
        return EXIT_FAILURE;
    }
    strncpy(server_ip, argv[3], sizeof(server_ip) - 1);
    server_ip[sizeof(server_ip) - 1] = '\0';
    server_port = atoi(argv[4]);

    srand48(time(NULL));

    if (tcp_active_open(&client, server_port, server_ip) != TCP_NO_ERROR) {
        LOG_CLOSE();
        return EXIT_FAILURE;
    }

    data.value = INITIAL_TEMPERATURE;
    while (loop_count) {
        data.value = data.value + TEMP_DEV * ((drand48() - 0.5) / 10);
        time(&data.timestamp);

        bytes = sizeof(data.sensor_id);
        if (tcp_send(client, &data.sensor_id, &bytes) != TCP_NO_ERROR) {
            tcp_close(&client);
            LOG_CLOSE();
            return EXIT_FAILURE;
        }

        bytes = sizeof(data.value);
        if (tcp_send(client, &data.value, &bytes) != TCP_NO_ERROR) {
            tcp_close(&client);
            LOG_CLOSE();
            return EXIT_FAILURE;
        }

        bytes = sizeof(data.timestamp);
        if (tcp_send(client, &data.timestamp, &bytes) != TCP_NO_ERROR) {
            tcp_close(&client);
            LOG_CLOSE();
            return EXIT_FAILURE;
        }

        LOG_PRINTF(data.sensor_id, data.value, data.timestamp);
        sleep_for_seconds(sleep_time);
        UPDATE(loop_count);
    }

    tcp_close(&client);
    LOG_CLOSE();
    return EXIT_SUCCESS;
}

static void print_help(void)
{
    printf("Use this program with 4 command line options:\n");
    printf("\t%-15s : a unique sensor node ID\n", "'ID'");
    printf("\t%-15s : node sleep time in seconds (supports decimals, e.g. 0.001)\n", "'sleep time'");
    printf("\t%-15s : TCP server IP address\n", "'server IP'");
    printf("\t%-15s : TCP server port number\n", "'server port'");
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
