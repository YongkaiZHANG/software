/**
 * \author Yongkai Zhang
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sqlite3.h>
#include "sbuffer.h"
#include <string.h>
#include "sensor_db.h"
/**
 * Make a connection to the database server
 * Create (open) a database with name DB_NAME having 1 table named TABLE_NAME
 * \param clear_up_flag if the table existed, clear up the existing data when clear_up_flag is set to 1
 * \return the connection for success, NULL if an error occurs
 */
DBCONN *init_connection(char clear_up_flag, pthread_mutex_t *pipe_mutex, int *pipe_fd)
{
  sqlite3 *db;
  int rc;
  char *zErrMsg = NULL;
  char *sql = NULL;
  char* log_message; // for send the infromation

  rc = sqlite3_open(TO_STRING(DB_NAME), &db);
  if (rc)
  {
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }
  else
  {
    // here successfully have connection
    asprintf(&log_message, "Connection to SQL server established.\n");
    send_into_pipe(pipe_mutex, pipe_fd, log_message);
    // Create SQL statement
    sql = sqlite3_mprintf("CREATE TABLE IF NOT EXISTS %s("
                          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                          "sensor_id INTEGER NOT NULL,"
                          "sensor_value DECIMAL(4,2) NOT NULL,"
                          "timestamp TIMESTAMP);",
                          TO_STRING(TABLE_NAME));

    // Execute SQL statement
    rc = sqlite3_exec(db, sql, 0, 0, &zErrMsg);
    sqlite3_free(sql);
    if (rc != SQLITE_OK)
    {
      fprintf(stderr, "SQL error: %s\n", zErrMsg);
      sqlite3_free(zErrMsg);
      sqlite3_close(db);
      return NULL;
    }
    // here successfully establish the table
    asprintf(&log_message, "New table <%s> created.\n", TO_STRING(TABLE_NAME));
    usleep(500);
    send_into_pipe(pipe_mutex, pipe_fd, log_message);
    printf("Opened database successfully\n");
  }

  if (clear_up_flag == 1)
  {
    sql = sqlite3_mprintf("UPDATE sqlite_sequence SET seq = 0 WHERE name = '%q';"
                          "DELETE FROM sqlite_sequence WHERE name = '%q';"
                          "DELETE FROM %q;",
                          TO_STRING(TABLE_NAME), TO_STRING(TABLE_NAME), TO_STRING(TABLE_NAME));

    rc = sqlite3_exec(db, sql, 0, 0, &zErrMsg);
    sqlite3_free(sql);
    if (rc != SQLITE_OK)
    {
      fprintf(stderr, "SQL error while clearing data: %s\n", zErrMsg);
      sqlite3_free(zErrMsg);
    }
  }

  return db;
}

/**
 * Disconnect from the database server
 * \param conn pointer to the current connection
 */
void disconnect(DBCONN *conn, pthread_mutex_t *pipe_mutex, int *pipe_fd)
{
  char *log_message;
  // here successfully establish the table
  asprintf(&log_message, " Connection to SQL server lost.\n");
  send_into_pipe(pipe_mutex, pipe_fd, log_message);
  sqlite3_close(conn);
}

/**
 * Write an INSERT query to insert a single sensor measurement
 * \param conn pointer to the current connection
 * \param id the sensor id
 * \param value the measurement value
 * \param ts the measurement timestamp
 * \return zero for success, and non-zero if an error occurs
 */
int insert_sensor(DBCONN *conn, sensor_id_t id, sensor_value_t value, sensor_ts_t ts)
{
  char *sql = NULL;
  char *zErrMsg = NULL;
  int rc;
  asprintf(&sql, "INSERT INTO " TO_STRING(TABLE_NAME) "(sensor_id, sensor_value, timestamp) VALUES(%hu, %e, %ld);", id, value, ts);
  rc = sqlite3_exec(conn, sql, 0, 0, &zErrMsg);
  free(sql);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "SQL error: %s\n", zErrMsg);
    sqlite3_free(zErrMsg);
    return -1;
  }
  else
  {
    fprintf(stdout, "Records created successfully\n");
  }
  return 0;
}

/**
 * Write an INSERT query to insert all sensor measurements available in the file 'sensor_data'
 * \param conn pointer to the current connection
 * \param buffer is the sbuffer pointer
 * \param all_data_read is the flag to show if the data in the buffer read
 * \param pipe_mutex is the pointer to lock the write into fifo
 * \param pipe_fd is the pointer to the process file discriptor
 * \param database_fail is a flag for database fail
 * \return zero for success, and non-zero if an error occurs
 */
int insert_sensor_from_sbuffer(DBCONN *conn, sbuffer_t *buffer,const int *all_data_read, pthread_mutex_t *pipe_mutex, int *pipe_fd,int* database_fail)
{
  int rc;
  sensor_data_t data;
  char *log_message;

  while (!*all_data_read || !sbuffer_is_empty(buffer))
  {
    if (sbuffer_remove(buffer, &data, DATABASE_READ,database_fail) == SBUFFER_SUCCESS)
    {
      rc = insert_sensor(conn, data.id, data.value, data.ts);
      if (rc != 0)
      {
        // Send log message to child process
        asprintf(&log_message," Connection to SQL server lost.");
        send_into_pipe(pipe_mutex, pipe_fd, log_message);
        return -1;
      }
    }
  }
  return 0;
}