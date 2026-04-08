#include <assert.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "sensor_db.h"

#define MALLOC_NO_ERROR 0
#define MALLOC_MEMORY_ERROR 1
#define MALLOC_INVALID_ERROR 2

#ifdef DEBUG
#define DEBUG_PRINTF(...)                                                                    \
    do                                                                                       \
    {                                                                                        \
        fprintf(stderr, "\nIn %s - function %s at line %d: ", __FILE__, __func__, __LINE__); \
        fprintf(stderr, __VA_ARGS__);                                                        \
        fflush(stderr);                                                                      \
    } while (0)
#else
#define DEBUG_PRINTF(...) (void)0
#endif

#define MALLOC_ERR_HANDLER(condition, err_code)   \
    do                                            \
    {                                             \
        if ((condition))                          \
            DEBUG_PRINTF(#condition " failed\n"); \
        assert(!(condition));                     \
    } while (0)

static int exec_sql(DBCONN *conn, char *sql, callback_t callback)
{
    char *error_message = NULL;
    int rc = sqlite3_exec(conn, sql, callback, NULL, &error_message);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", error_message == NULL ? "unknown error" : error_message);
        sqlite3_free(error_message);
        return -1;
    }

    sqlite3_free(error_message);
    return 0;
}

DBCONN *init_connection(char *clear_up_flag)
{
    sqlite3 *db = NULL;
    char *sql = NULL;

    if (sqlite3_open(TO_STRING(DB_NAME), &db) != SQLITE_OK) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }

    sql = sqlite3_mprintf(
        "CREATE TABLE IF NOT EXISTS %s ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sensor_id INTEGER NOT NULL,"
        "sensor_value REAL NOT NULL,"
        "timestamp INTEGER NOT NULL);",
        TO_STRING(TABLE_NAME)
    );
    if (exec_sql(db, sql, NULL) != 0) {
        sqlite3_free(sql);
        sqlite3_close(db);
        return NULL;
    }
    sqlite3_free(sql);

    if (clear_up_flag != NULL && atoi(clear_up_flag) != 0) {
        sql = sqlite3_mprintf("DELETE FROM %s;", TO_STRING(TABLE_NAME));
        if (exec_sql(db, sql, NULL) != 0) {
            sqlite3_free(sql);
            sqlite3_close(db);
            return NULL;
        }
        sqlite3_free(sql);
    }

    return db;
}

void disconnect(DBCONN *conn)
{
    sqlite3_close(conn);
}

int insert_sensor(DBCONN *conn, sensor_id_t id, sensor_value_t value, sensor_ts_t ts)
{
    char *sql = sqlite3_mprintf(
        "INSERT INTO %s (sensor_id, sensor_value, timestamp) VALUES (%u, %f, %lld);",
        TO_STRING(TABLE_NAME),
        (unsigned)id,
        value,
        (long long)ts
    );
    int rc;

    rc = exec_sql(conn, sql, NULL);
    sqlite3_free(sql);
    return rc;
}

int insert_sensor_from_file(DBCONN *conn, FILE *sensor_data)
{
    sensor_id_t sensor_id;
    sensor_value_t temperature;
    sensor_ts_t timestamp;

    while (fread(&sensor_id, sizeof(sensor_id), 1, sensor_data) == 1 &&
           fread(&temperature, sizeof(temperature), 1, sensor_data) == 1 &&
           fread(&timestamp, sizeof(timestamp), 1, sensor_data) == 1) {
        if (insert_sensor(conn, sensor_id, temperature, timestamp) != 0) {
            return -1;
        }
    }

    return ferror(sensor_data) ? -1 : 0;
}

int insert_from_sbuffer(DBCONN *conn, sbuffer_t *sbuffer)
{
    sbuffer_node_t *node;

    if (conn == NULL || sbuffer == NULL) return -1;

    for (node = sbuffer->head; node != NULL; node = node->next) {
        if (insert_sensor(conn, node->data.sensor_id, node->data.value, node->data.timestamp) != 0) {
            return -1;
        }
    }

    return 0;
}

int find_sensor_all(DBCONN *conn, callback_t f)
{
    char *sql = sqlite3_mprintf("SELECT * FROM %s;", TO_STRING(TABLE_NAME));
    int rc = exec_sql(conn, sql, f);

    sqlite3_free(sql);
    return rc;
}

int find_sensor_by_value(DBCONN *conn, sensor_value_t value, callback_t f)
{
    char *sql = sqlite3_mprintf(
        "SELECT * FROM %s WHERE sensor_value = %f;",
        TO_STRING(TABLE_NAME),
        value
    );
    int rc = exec_sql(conn, sql, f);

    sqlite3_free(sql);
    return rc;
}

int find_sensor_exceed_value(DBCONN *conn, sensor_value_t value, callback_t f)
{
    char *sql = sqlite3_mprintf(
        "SELECT * FROM %s WHERE sensor_value > %f;",
        TO_STRING(TABLE_NAME),
        value
    );
    int rc = exec_sql(conn, sql, f);

    sqlite3_free(sql);
    return rc;
}

int find_sensor_by_timestamp(DBCONN *conn, sensor_ts_t ts, callback_t f)
{
    char *sql = sqlite3_mprintf(
        "SELECT * FROM %s WHERE timestamp = %lld;",
        TO_STRING(TABLE_NAME),
        (long long)ts
    );
    int rc = exec_sql(conn, sql, f);

    sqlite3_free(sql);
    return rc;
}

int find_sensor_after_timestamp(DBCONN *conn, sensor_ts_t ts, callback_t f)
{
    char *sql = sqlite3_mprintf(
        "SELECT * FROM %s WHERE timestamp > %lld;",
        TO_STRING(TABLE_NAME),
        (long long)ts
    );
    int rc = exec_sql(conn, sql, f);

    sqlite3_free(sql);
    return rc;
}
