/**
 * \author Yongkai Zhang
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "datamgr.h"

static dplist_t *list = NULL;
static int listen_port = 0;

enum {
    ALERT_STATE_COLD = -1,
    ALERT_STATE_NORMAL = 0,
    ALERT_STATE_HOT = 1
};

static const char *alert_state_to_text(int8_t state)
{
    if (state == ALERT_STATE_COLD) return "COLD";
    if (state == ALERT_STATE_HOT) return "HOT";
    return "NORMAL";
}

static void *element_copy(void *element)
{
    sensor_data_t *sensor = element;
    sensor_data_t *sensor_copy = malloc(sizeof(*sensor_copy));

    if (sensor_copy == NULL) return NULL;

    *sensor_copy = *sensor;
    return sensor_copy;
}

static void element_free(void **element)
{
    free(*element);
    *element = NULL;
}

static int element_compare(void *x, void *y)
{
    sensor_id_t sensor_id = *(sensor_id_t *)x;
    sensor_data_t *sensor = y;

    if (sensor_id == sensor->sensor_id) return 0;
    return (sensor_id < sensor->sensor_id) ? -1 : 1;
}

static void write_log_message(FILE *file, const char *message)
{
    if (file == NULL) return;
    fputs(message, file);
    fflush(file);
}

static void write_data_log(FILE *file, const sensor_data_t *sensor)
{
    const char *status = "WARMUP";

    if (file == NULL || sensor == NULL) return;
    if (sensor->sample_count >= RUN_AVG_LENGTH) {
        status = alert_state_to_text(sensor->alert_state);
    }

    fprintf(
        file,
        "DATA port=%d room=%hu sensor=%hu temp=%.2f avg=%.2f status=%s ts=%ld\n",
        listen_port,
        sensor->room_id,
        sensor->sensor_id,
        sensor->value,
        sensor->RUN_AVG,
        status,
        (long)sensor->timestamp
    );
}

void datamgr_set_listen_port(int port)
{
    listen_port = port;
}

static sensor_data_t *datamgr_get_sensor(sensor_id_t sensor_id)
{
    for (int index = 0; index < dpl_size(list); index++) {
        sensor_data_t *sensor = dpl_get_element_at_index(list, index);
        if (sensor != NULL && sensor->sensor_id == sensor_id) {
            return sensor;
        }
    }

    return NULL;
}

static int load_sensor_map(FILE *fp_sensor_map)
{
    uint16_t room_id;
    sensor_id_t sensor_id;

    if (fp_sensor_map == NULL) return -1;

    if (list != NULL) {
        dpl_free(&list, true);
    }

    list = dpl_create(element_copy, element_free, element_compare);
    if (list == NULL) return -1;

    while (fscanf(fp_sensor_map, "%hu %hu", &room_id, &sensor_id) == 2) {
        sensor_data_t sensor = {0};

        sensor.sensor_id = sensor_id;
        sensor.room_id = room_id;
        sensor.alert_state = ALERT_STATE_NORMAL;

        if (dpl_insert_at_index(list, &sensor, dpl_size(list), true) == NULL) {
            return -1;
        }
    }

    return 0;
}

static void update_running_average(sensor_data_t *sensor, sensor_value_t new_value)
{
    double total = 0;
    size_t window_size;

    if (sensor->sample_count < RUN_AVG_LENGTH) {
        sensor->temperatures[sensor->sample_count] = new_value;
        sensor->sample_count++;
    } else {
        for (size_t index = 0; index < RUN_AVG_LENGTH - 1; index++) {
            sensor->temperatures[index] = sensor->temperatures[index + 1];
        }
        sensor->temperatures[RUN_AVG_LENGTH - 1] = new_value;
        sensor->sample_count++;
    }

    window_size = sensor->sample_count < RUN_AVG_LENGTH ? sensor->sample_count : RUN_AVG_LENGTH;
    for (size_t index = 0; index < window_size; index++) {
        total += sensor->temperatures[index];
    }
    sensor->RUN_AVG = total / (double)window_size;
}

void datamgr_parse_sensor_sbuffer(sbuffer_t *sbuffer, FILE *fp_sensor_map)
{
    FILE *log_file;
    size_t pending_data_lines = 0;
    char startup_msg[192];

    if (listen_port <= 0) {
#ifdef PORT
        listen_port = PORT;
#else
        listen_port = 0;
#endif
    }

    if (sbuffer == NULL || fp_sensor_map == NULL) return;
    if (load_sensor_map(fp_sensor_map) != 0) return;
    log_file = fopen(FIFO_LOG, "a");
    if (log_file != NULL) {
        // Flush each line so logs are observable in real time while debugging.
        setvbuf(log_file, NULL, _IOLBF, 0);
        snprintf(
            startup_msg,
            sizeof(startup_msg),
            "START port=%d min=%.2f max=%.2f avg_window=%d\n",
            listen_port,
            (double)SET_MIN_TEMP,
            (double)SET_MAX_TEMP,
            RUN_AVG_LENGTH
        );
        write_log_message(log_file, startup_msg);
    }

    while (true) {
        sensor_data_t measurement;
        sensor_data_t *sensor;
        int new_alert_state;
        int rc;

        rc = sbuffer_remove(sbuffer, &measurement);
        if (rc == SBUFFER_CLOSED) break;
        if (rc != SBUFFER_SUCCESS) {
            break;
        }

        sensor = datamgr_get_sensor(measurement.sensor_id);
        if (sensor == NULL) {
            char buffer[160];

            snprintf(
                buffer,
                sizeof(buffer),
                "INVALID_SENSOR port=%d room=%hu sensor=%hu\n",
                listen_port,
                measurement.room_id,
                measurement.sensor_id
            );
            write_log_message(log_file, buffer);
            continue;
        }
        if (sensor->room_id != measurement.room_id) {
            char buffer[192];

            snprintf(
                buffer,
                sizeof(buffer),
                "INVALID_PAIR port=%d sensor=%hu room=%hu expected_room=%hu\n",
                listen_port,
                measurement.sensor_id,
                measurement.room_id,
                sensor->room_id
            );
            write_log_message(log_file, buffer);
            continue;
        }

        sensor->value = measurement.value;
        sensor->timestamp = measurement.timestamp;
        update_running_average(sensor, measurement.value);
        new_alert_state = ALERT_STATE_NORMAL;

        if (sensor->sample_count >= RUN_AVG_LENGTH) {
            char buffer[160];

            if (sensor->RUN_AVG < SET_MIN_TEMP) {
                new_alert_state = ALERT_STATE_COLD;
            } else if (sensor->RUN_AVG > SET_MAX_TEMP) {
                new_alert_state = ALERT_STATE_HOT;
            }

            if (new_alert_state != sensor->alert_state) {
                if (new_alert_state == ALERT_STATE_COLD) {
                    snprintf(
                        buffer,
                        sizeof(buffer),
                        "ALERT room=%hu sensor=%hu status=COLD avg=%.2f\n",
                        sensor->room_id,
                        sensor->sensor_id,
                        sensor->RUN_AVG
                    );
                    write_log_message(log_file, buffer);
                } else if (new_alert_state == ALERT_STATE_HOT) {
                    snprintf(
                        buffer,
                        sizeof(buffer),
                        "ALERT room=%hu sensor=%hu status=HOT avg=%.2f\n",
                        sensor->room_id,
                        sensor->sensor_id,
                        sensor->RUN_AVG
                    );
                    write_log_message(log_file, buffer);
                } else {
                    snprintf(
                        buffer,
                        sizeof(buffer),
                        "RECOVERY room=%hu sensor=%hu status=NORMAL avg=%.2f\n",
                        sensor->room_id,
                        sensor->sensor_id,
                        sensor->RUN_AVG
                    );
                    write_log_message(log_file, buffer);
                }
            }
        }

        sensor->alert_state = new_alert_state;
        write_data_log(log_file, sensor);
        pending_data_lines++;
        if (log_file != NULL && pending_data_lines >= 128) {
            fflush(log_file);
            pending_data_lines = 0;
        }
    }

    if (log_file != NULL) {
        fflush(log_file);
        write_log_message(log_file, "STOP receiver drained queue and exited\n");
        fclose(log_file);
    }
}

void datamgr_free(void)
{
    if (list != NULL) {
        dpl_free(&list, true);
    }
}
