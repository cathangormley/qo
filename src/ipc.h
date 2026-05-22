#pragma once

#include <stdint.h>
#include <stddef.h>
#include "qo_value.h"

struct Environment;
void ipc_init(void);
int ipc_listen(int port);
int ipc_accept_connection(void);
int ipc_connection_fd(void);
int ipc_process_connection(struct Environment *env);
int ipc_is_valid_handle(int fd);
int ipc_connect(const char *host, int port);
void ipc_close(int fd);
void ipc_cleanup(void);
int ipc_server_fd(void);

Qo ipc_serialize(Qo arg);
Qo ipc_deserialize(const uint8_t *data, size_t len);
Qo ipc_handle_apply(int fd, Qo arg);
