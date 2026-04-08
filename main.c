#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "config.h"
#include "connmgr.h"
#include "datamgr.h"

typedef struct {
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

static int run_connmgr_child(const app_context_t *context, int pipe_write_fd)
{
    if (context == NULL) return EXIT_FAILURE;

    signal(SIGPIPE, SIG_IGN);
    return connmgr_listen(pipe_write_fd, context->port, context->timeout_seconds);
}

static int run_datamgr_child(const app_context_t *context, int pipe_read_fd)
{
    FILE *map_file = fopen("room_sensor.map", "r");

    if (context == NULL) return EXIT_FAILURE;
    if (map_file == NULL) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    datamgr_set_listen_port(context->port);
    /* Child side: pipe input -> running averages -> gateway.log. */
    if (datamgr_parse_sensor_pipe(pipe_read_fd, map_file) != 0) {
        fclose(map_file);
        datamgr_free();
        return EXIT_FAILURE;
    }
    fclose(map_file);
    datamgr_free();
    return EXIT_SUCCESS;
}

static int wait_for_child(pid_t pid, const char *label)
{
    int status;

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return EXIT_FAILURE;
    }
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);

        if (exit_code != EXIT_SUCCESS) {
            fprintf(stderr, "%s exited with status %d\n", label, exit_code);
        }
        return exit_code;
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "%s terminated by signal %d\n", label, WTERMSIG(status));
    }
    return EXIT_FAILURE;
}

int main(int argc, char *argv[])
{
    app_context_t context = {0};
    int data_pipe[2];
    pid_t datamgr_pid;
    pid_t connmgr_pid;
    FILE *log_file;
    int connmgr_status;
    int datamgr_status;

    if (parse_runtime_args(argc, argv, &context) != 0) {
        return EXIT_FAILURE;
    }
    if (access("room_sensor.map", R_OK) != 0) {
        perror("room_sensor.map");
        return EXIT_FAILURE;
    }

    log_file = fopen(FIFO_LOG, "w");
    if (log_file != NULL) {
        fclose(log_file);
    }

    if (pipe(data_pipe) != 0) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    /* Start the consumer child first so the pipe reader is ready. */
    datamgr_pid = fork();
    if (datamgr_pid < 0) {
        perror("fork");
        close(data_pipe[0]);
        close(data_pipe[1]);
        return EXIT_FAILURE;
    }
    if (datamgr_pid == 0) {
        close(data_pipe[1]);
        exit(run_datamgr_child(&context, data_pipe[0]));
    }

    /* Then start the TCP receiver child that feeds the pipe. */
    connmgr_pid = fork();
    if (connmgr_pid < 0) {
        perror("fork");
        close(data_pipe[0]);
        close(data_pipe[1]);
        kill(datamgr_pid, SIGTERM);
        waitpid(datamgr_pid, NULL, 0);
        return EXIT_FAILURE;
    }
    if (connmgr_pid == 0) {
        close(data_pipe[0]);
        exit(run_connmgr_child(&context, data_pipe[1]));
    }

    close(data_pipe[0]);
    close(data_pipe[1]);

    connmgr_status = wait_for_child(connmgr_pid, "connmgr child");
    datamgr_status = wait_for_child(datamgr_pid, "datamgr child");

    if (connmgr_status != EXIT_SUCCESS || datamgr_status != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
