#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "config.h"
#include "connmgr.h"
#include "datamgr.h"
#include "sbuffer.h"

typedef struct {
    sbuffer_t *buffer;
    int port;
    int timeout_seconds;
} app_context_t;

static int parse_int_in_range(const char *text, int min_value, int max_value)
{
    char *endptr = NULL;
    long value;

    errno = 0;
    value = strtol(text, &endptr, 10);
    if (errno != 0 || endptr == text || *endptr != '\0') {
        return -1;
    }
    if (value < min_value || value > max_value) {
        return -1;
    }
    return (int)value;
}

static int parse_runtime_args(int argc, char *argv[], app_context_t *context)
{
    if (context == NULL) return -1;
    context->timeout_seconds = TIMEOUT;

    if (argc == 2) {
        context->port = parse_int_in_range(argv[1], 1, 65535);
        if (context->port < 0) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            fprintf(stderr, "Usage: %s [port] [idle_timeout_seconds]\n", argv[0]);
            return -1;
        }
        return 0;
    }

    if (argc == 3) {
        context->port = parse_int_in_range(argv[1], 1, 65535);
        context->timeout_seconds = parse_int_in_range(argv[2], 0, 86400);
        if (context->port < 0 || context->timeout_seconds < 0) {
            fprintf(stderr, "Invalid arguments: port=%s timeout=%s\n", argv[1], argv[2]);
            fprintf(stderr, "Usage: %s [port] [idle_timeout_seconds]\n", argv[0]);
            fprintf(stderr, "idle_timeout_seconds=0 means listen forever\n");
            return -1;
        }
        return 0;
    }

    if (argc == 1) {
        fprintf(stderr, "Usage: %s [port] [idle_timeout_seconds]\n", argv[0]);
        return -1;
    }

    fprintf(stderr, "Usage: %s [port] [idle_timeout_seconds]\n", argv[0]);
    return -1;
}

static void *connmgr_start(void *arg)
{
    app_context_t *context = arg;

    connmgr_listen(&context->buffer, context->port, context->timeout_seconds);
    return NULL;
}

static void *datamgr_start(void *arg)
{
    app_context_t *context = arg;
    FILE *map_file = fopen("room_sensor.map", "r");

    if (map_file == NULL) {
        perror("fopen");
        return NULL;
    }

    datamgr_parse_sensor_sbuffer(context->buffer, map_file);
    fclose(map_file);
    return NULL;
}

int main(int argc, char *argv[])
{
    app_context_t context = {0};
    pthread_t connmgr_thread;
    pthread_t datamgr_thread;
    FILE *log_file;

    if (parse_runtime_args(argc, argv, &context) != 0) {
        return EXIT_FAILURE;
    }
    datamgr_set_listen_port(context.port);

    if (sbuffer_init(&context.buffer) != SBUFFER_SUCCESS) {
        fprintf(stderr, "Unable to initialize shared buffer\n");
        return EXIT_FAILURE;
    }

    log_file = fopen(FIFO_LOG, "w");
    if (log_file != NULL) {
        fclose(log_file);
    }

    if (pthread_create(&datamgr_thread, NULL, datamgr_start, &context) != 0) {
        perror("pthread_create");
        sbuffer_free(&context.buffer);
        return EXIT_FAILURE;
    }

    if (pthread_create(&connmgr_thread, NULL, connmgr_start, &context) != 0) {
        perror("pthread_create");
        pthread_join(datamgr_thread, NULL);
        datamgr_free();
        sbuffer_free(&context.buffer);
        return EXIT_FAILURE;
    }

    pthread_join(connmgr_thread, NULL);
    pthread_join(datamgr_thread, NULL);

    datamgr_free();
    sbuffer_free(&context.buffer);
    return EXIT_SUCCESS;
}
