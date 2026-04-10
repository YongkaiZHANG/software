#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "config.h"
#include "connmgr.h"
#include "datamgr.h"
#include "sbuffer.h"
#include "sensor_db.h"

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

    connmgr_listen(context->buffer, context->port, context->timeout_seconds);
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

static void *storagemgr_start(void *arg)
{
    app_context_t *context = arg;
    DBCONN *db = init_connection("1");

    if (db == NULL) {
        fprintf(stderr, "Unable to connect to database\n");
        return NULL;
    }

    while (true) {
        sensor_data_t data;
        int rc = sbuffer_remove(context->buffer, &data, READER_STORE);
        if (rc == SBUFFER_CLOSED) break;
        if (rc == SBUFFER_SUCCESS) {
            insert_sensor(db, data.sensor_id, data.value, data.timestamp);
        }
    }

    disconnect(db);
    return NULL;
}

void run_logger(void)
{
    FILE *fifo_fp;
    FILE *log_fp;
    char buffer[1024];
    int sequence = 0;

    fifo_fp = fopen(FIFO_LOG, "r");
    if (fifo_fp == NULL) {
        perror("Logger: Failed to open FIFO for reading");
        exit(EXIT_FAILURE);
    }

    log_fp = fopen("gateway.log_txt", "w");
    if (log_fp == NULL) {
        perror("Logger: Failed to open gateway.log_txt");
        fclose(fifo_fp);
        exit(EXIT_FAILURE);
    }

    while (fgets(buffer, sizeof(buffer), fifo_fp) != NULL) {
        time_t now = time(NULL);
        char *ts = ctime(&now);
        ts[strlen(ts) - 1] = '\0'; // Remove newline

        fprintf(log_fp, "%d - %s - %s", sequence++, ts, buffer);
        fflush(log_fp);
    }

    fclose(log_fp);
    fclose(fifo_fp);
}

int main(int argc, char *argv[])
{
    app_context_t context = {0};
    pid_t connmgr_pid;
    pid_t datamgr_pid;
    pid_t storagemgr_pid;
    pid_t logger_pid;

    // Ignore SIGPIPE to prevent crashes when writing to closed FIFO/socket
    signal(SIGPIPE, SIG_IGN);

    if (parse_runtime_args(argc, argv, &context) != 0) {
        return EXIT_FAILURE;
    }

    // Create the log FIFO if it doesn't exist
    unlink(FIFO_LOG);
    if (mkfifo(FIFO_LOG, 0666) == -1) {
        perror("mkfifo");
        return EXIT_FAILURE;
    }

    logger_pid = fork();
    if (logger_pid < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (logger_pid == 0) {
        // Child process: Logger
        run_logger();
        return EXIT_SUCCESS;
    }

    // Parent process
    datamgr_set_listen_port(context.port);

    if (sbuffer_init(&context.buffer) != SBUFFER_SUCCESS) {
        fprintf(stderr, "Unable to initialize shared buffer\n");
        return EXIT_FAILURE;
    }

    datamgr_pid = fork();
    if (datamgr_pid < 0) {
        perror("fork");
        sbuffer_free(&context.buffer);
        return EXIT_FAILURE;
    }
    if (datamgr_pid == 0) {
        datamgr_start(&context);
        datamgr_free();
        exit(EXIT_SUCCESS);
    }

    storagemgr_pid = fork();
    if (storagemgr_pid < 0) {
        perror("fork");
        kill(datamgr_pid, SIGTERM);
        waitpid(datamgr_pid, NULL, 0);
        sbuffer_free(&context.buffer);
        return EXIT_FAILURE;
    }
    if (storagemgr_pid == 0) {
        storagemgr_start(&context);
        exit(EXIT_SUCCESS);
    }

    connmgr_pid = fork();
    if (connmgr_pid < 0) {
        perror("fork");
        kill(datamgr_pid, SIGTERM);
        kill(storagemgr_pid, SIGTERM);
        waitpid(datamgr_pid, NULL, 0);
        waitpid(storagemgr_pid, NULL, 0);
        sbuffer_free(&context.buffer);
        return EXIT_FAILURE;
    }
    if (connmgr_pid == 0) {
        connmgr_start(&context);
        connmgr_free();
        exit(EXIT_SUCCESS);
    }

    waitpid(connmgr_pid, NULL, 0);
    waitpid(datamgr_pid, NULL, 0);
    waitpid(storagemgr_pid, NULL, 0);

    sbuffer_free(&context.buffer);

    // Close the writing end of the FIFO by the parent
    // (all threads that could have opened it are joined)
    
    // Wait for logger to finish
    waitpid(logger_pid, NULL, 0);

    return EXIT_SUCCESS;
}
