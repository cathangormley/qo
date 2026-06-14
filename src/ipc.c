#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "ipc.h"
#include "evaluator_internal.h"
#include "internal.h"
#include "symbol_intern.h"

static int g_server_fd = -1;
static int g_conn_fd = -1;

static int write_all(int fd, const void *buf, size_t count) {
    const char *p = (const char *)buf;
    size_t remaining = count;
    while (remaining > 0) {
        ssize_t n = send(fd, p, remaining, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        p += n;
        remaining -= (size_t)n;
    }
    return 0;
}

static int read_all(int fd, void *buf, size_t count) {
    char *p = (char *)buf;
    size_t remaining = count;
    while (remaining > 0) {
        ssize_t n = recv(fd, p, remaining, 0);
        if (n <= 0) { return -1; }
        p += n;
        remaining -= (size_t)n;
    }
    return 0;
}

static size_t ipc_block_size(Qo q) {
    if (q == NULL) return 0;
    uint8_t t = qo_type(q);
    if (t == QO_SYMBOL) return 2 + strlen(qo_symbol_name(q)) + 1;
    if (t == QO_SYM_VEC) {
        int64_t n = qo_count(q);
        size_t sz = 10; /* 2 header + 8 count */
        for (int64_t i = 0; i < n; i++) {
            sz += strlen(qo_symbol_name(QO_LIST_DATA(q)[i])) + 1;
        }
        return sz;
    }
    if (t == QO_DICT) {
        int64_t n = QO_DICT_COUNT(q);
        size_t sz = 12; /* 2 header + 8 count + 1 ktype + 1 vtype */
        for (int64_t i = 0; i < n; i++) {
            sz += ipc_block_size(QO_DICT_KEYS(q)[i]);
            sz += ipc_block_size(QO_DICT_VALS(q)[i]);
        }
        return sz;
    }
    if (type_has_flag(t, TF_SCALAR)) return 2 + type_elem_size(t);
    if (type_has_flag(t, TF_VECTOR) && !type_has_flag(t, TF_COMPLEX))
        return 10 + (size_t)qo_count(q) * type_elem_size(t);
    return 0;
}

Qo ipc_serialize(Qo arg) {
    if (arg == NULL) return alloc_data_vec(QO_BYTE_VEC, 0);
    uint8_t t = qo_type(arg);
    size_t sz = ipc_block_size(arg);
    if (sz == 0) { EVAL_ERROR("cannot serialize this type"); }
    Qo result = alloc_data_vec(QO_BYTE_VEC, (int64_t)sz);
    uint8_t *out = qo_byte_data(result);
    out[0] = t;
    out[1] = qo_attrs(arg);
    if (t == QO_SYMBOL) {
        memcpy(out + 2, qo_symbol_name(arg), sz - 2);
    } else if (t == QO_SYM_VEC) {
        int64_t n = qo_count(arg);
        memcpy(out + 2, &n, 8);
        size_t offset = 10;
        for (int64_t i = 0; i < n; i++) {
            const char *name = qo_symbol_name(QO_LIST_DATA(arg)[i]);
            size_t name_len = strlen(name) + 1;
            memcpy(out + offset, name, name_len);
            offset += name_len;
        }
    } else if (t == QO_DICT) {
        int64_t n = QO_DICT_COUNT(arg);
        memcpy(out + 2, &n, 8);
        out[10] = QO_DICT_KTYPE(arg);
        out[11] = QO_DICT_VTYPE(arg);
        size_t offset = 12;
        for (int64_t i = 0; i < n; i++) {
            Qo ser = ipc_serialize(QO_DICT_KEYS(arg)[i]);
            size_t ser_len = (size_t)qo_count(ser);
            memcpy(out + offset, qo_byte_data(ser), ser_len);
            offset += ser_len;
            qo_release(ser);
        }
        for (int64_t i = 0; i < n; i++) {
            Qo ser = ipc_serialize(QO_DICT_VALS(arg)[i]);
            size_t ser_len = (size_t)qo_count(ser);
            memcpy(out + offset, qo_byte_data(ser), ser_len);
            offset += ser_len;
            qo_release(ser);
        }
    } else if (type_has_flag(t, TF_SCALAR)) {
        memcpy(out + 2, &arg->long_val, type_elem_size(t));
    } else {
        int64_t n = qo_count(arg);
        memcpy(out + 2, &n, 8);
        memcpy(out + 10, qo_byte_data(arg), (size_t)n * type_elem_size(t));
    }
    return result;
}

static Qo deserialize_scalar(uint8_t t, const uint8_t *data, size_t len) {
    (void)len;
    return make_scalar_value(t, data);
}

/*
 * Compute the total byte length of a single serialized value in the wire
 * (type + attrs + payload).  Returns 0 for unknown or malformed input.
 * This is needed for compound types (dicts) whose serialization interleaves
 * recursively-serialized sub-values: the parser must know each sub-value's
 * extent in order to advance the data pointer.
 */
static size_t ipc_wire_parse_size(const uint8_t *data, size_t max_len) {
    if (max_len < 2) return 0;
    uint8_t t = data[0];
    if (t == QO_SYMBOL) {
        size_t name_len = strnlen((const char *)(data + 2), max_len - 2);
        if (name_len + 2 >= max_len) return 0;
        return 2 + name_len + 1;
    }
    if (t == QO_SYM_VEC) {
        if (max_len < 10) return 0;
        int64_t n;
        memcpy(&n, data + 2, 8);
        size_t offset = 10;
        for (int64_t i = 0; i < n && offset < max_len; i++) {
            size_t name_len = strnlen((const char *)(data + offset), max_len - offset);
            if (name_len + offset >= max_len) return 0;
            offset += name_len + 1;
        }
        return offset;
    }
    if (type_has_flag(t, TF_SCALAR)) {
        return 2 + type_elem_size(t);
    }
    if (type_has_flag(t, TF_VECTOR) && !type_has_flag(t, TF_COMPLEX)) {
        if (max_len < 10) return 0;
        int64_t n;
        memcpy(&n, data + 2, 8);
        return 10 + (size_t)n * type_elem_size(t);
    }
    if (t == QO_DICT) {
        if (max_len < 12) return 0;
        int64_t n;
        memcpy(&n, data + 2, 8);
        size_t offset = 12;
        for (int64_t i = 0; i < n && offset < max_len; i++) {
            size_t sz = ipc_wire_parse_size(data + offset, max_len - offset);
            if (sz == 0) return 0;
            offset += sz;
        }
        for (int64_t i = 0; i < n && offset < max_len; i++) {
            size_t sz = ipc_wire_parse_size(data + offset, max_len - offset);
            if (sz == 0) return 0;
            offset += sz;
        }
        return offset;
    }
    return 0;
}

static Qo deserialize_dict(const uint8_t *data, size_t len) {
    if (len < 10) return NULL; /* 8 count + 1 ktype + 1 vtype */
    int64_t n;
    memcpy(&n, data, 8);
    uint8_t ktype = data[8];
    uint8_t vtype = data[9];
    data += 10;
    len -= 10;
    Qo result = alloc_dict_block(n);
    QO_DICT_KTYPE(result) = ktype;
    QO_DICT_VTYPE(result) = vtype;
    for (int64_t i = 0; i < n; i++) {
        size_t sz = ipc_wire_parse_size(data, len);
        if (sz == 0 || sz > len) { qo_release(result); return NULL; }
        Qo key = ipc_deserialize(data, sz);
        if (key == NULL) { qo_release(result); return NULL; }
        QO_DICT_KEYS(result)[i] = key;
        data += sz;
        len -= sz;
    }
    for (int64_t i = 0; i < n; i++) {
        size_t sz = ipc_wire_parse_size(data, len);
        if (sz == 0 || sz > len) { qo_release(result); return NULL; }
        Qo val = ipc_deserialize(data, sz);
        if (val == NULL) { qo_release(result); return NULL; }
        QO_DICT_VALS(result)[i] = val;
        data += sz;
        len -= sz;
    }
    return result;
}

static Qo deserialize_symbol_vector(const uint8_t *data, size_t len) {
    int64_t n;
    if (len < 8) return NULL;
    memcpy(&n, data, 8);
    data += 8;
    len -= 8;
    Qo result = alloc_ptr_vec(QO_SYM_VEC, n);
    for (int64_t i = 0; i < n; i++) {
        size_t name_len = strnlen((const char *)data, len);
        if (name_len == len) { qo_release(result); return NULL; }
        QO_LIST_DATA(result)[i] = qo_symbol_intern((const char *)data);
        size_t skip = name_len + 1;
        data += skip;
        len -= skip;
    }
    return result;
}

static Qo deserialize_vector(uint8_t t, const uint8_t *data, size_t len) {
    int64_t n;
    if (len < 8) return NULL;
    memcpy(&n, data, 8);
    if (t == QO_CHAR_VEC) {
        Qo result = alloc_charlike(QO_CHAR_VEC, n);
        memcpy(qo_char_data(result), data + 8, (size_t)n);
        return result;
    }
    Qo result = alloc_data_vec(t, n);
    memcpy(qo_byte_data(result), data + 8, (size_t)n * type_elem_size(t));
    return result;
}

Qo ipc_deserialize(const uint8_t *data, size_t len) {
    if (len < 2) return make_null_value();
    uint8_t t = data[0];
    data += 2;
    len -= 2;
    if (t == QO_SYMBOL) {
        size_t name_len = strnlen((const char *)data, len);
        if (name_len == len) return NULL;
        return qo_symbol_intern((const char *)data);
    }
    if (type_has_flag(t, TF_SCALAR)) {
        if (len < type_elem_size(t)) return NULL;
        return deserialize_scalar(t, data, len);
    }
    if (t == QO_SYM_VEC)
        return deserialize_symbol_vector(data, len);
    if (t == QO_DICT)
        return deserialize_dict(data, len);
    if (type_has_flag(t, TF_VECTOR) && !type_has_flag(t, TF_COMPLEX))
        return deserialize_vector(t, data, len);
    return NULL;
}

void ipc_init(void) {
    g_server_fd = -1;
}

int ipc_listen(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); return -1;
    }
    if (listen(fd, 5) < 0) {
        perror("listen"); close(fd); return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    g_server_fd = fd;
    return 0;
}

int ipc_accept_connection(void) {
    if (g_server_fd < 0) { return -1; }

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int conn = accept(g_server_fd, (struct sockaddr*)&addr, &addrlen);
    if (conn < 0) { return -1; }

    if (g_conn_fd >= 0) close(g_conn_fd);
    g_conn_fd = conn;
    return 0;
}

int ipc_connection_fd(void) {
    return g_conn_fd;
}

int ipc_process_connection(Environment *env) {
    (void)env;
    if (g_conn_fd < 0) { return -1; }
    int conn = g_conn_fd;
    g_conn_fd = -1;

    int opt = 1;
    setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    uint32_t net_len;
    if (read_all(conn, &net_len, 4) < 0) { close(conn); return -1; }
    uint32_t payload_len = ntohl(net_len);

    uint8_t *payload = xmalloc(payload_len);
    if (read_all(conn, payload, payload_len) < 0) { free(payload); close(conn); return -1; }

    Qo value = ipc_deserialize(payload, payload_len);
    free(payload);

    Qo echo_val = value != NULL ? value : make_null_value();
    Qo resp = ipc_serialize(echo_val);
    if (resp != NULL && !evaluator_error_requested()) {
        int64_t data_len = qo_count(resp);
        uint8_t *data = qo_byte_data(resp);
        uint32_t net_resp_len = htonl((uint32_t)data_len);
        write_all(conn, &net_resp_len, 4);
        write_all(conn, data, (size_t)data_len);
    }
    qo_release(resp);

    if (value != NULL && !evaluator_error_requested()) {
        qo_print_with_limits(value, 25, 80);
        printf("\n");
        fflush(stdout);
    }
    qo_release(value);

    g_conn_fd = conn;
    return 0;
}

int ipc_is_valid_handle(int fd) {
    int ret = fcntl(fd, F_GETFD);
    return ret != -1 || errno != EBADF;
}

int ipc_connect(const char *host, int port) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int gai_err = getaddrinfo(host, port_str, &hints, &res);
    if (gai_err != 0) { return -1; }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    freeaddrinfo(res);
    return fd;
}

void ipc_close(int fd) {
    close(fd);
}

int ipc_server_fd(void) {
    return g_server_fd;
}

void ipc_cleanup(void) {
    if (g_conn_fd >= 0) { close(g_conn_fd); g_conn_fd = -1; }
    if (g_server_fd >= 0) { close(g_server_fd); g_server_fd = -1; }
}

Qo ipc_handle_apply(int fd, Qo arg) {
    if (!ipc_is_valid_handle(fd)) EVAL_ERROR("not a valid connection handle");

    /* Same-process shortcut: server socket exists, echo directly */
    if (g_conn_fd >= 0) {
        close(g_conn_fd);
        g_conn_fd = -1;
        return qo_clone(arg);
    }

    Qo payload = ipc_serialize(arg);
    if (evaluator_error_requested()) { qo_release(payload); return NULL; }

    int64_t data_len = qo_count(payload);
    uint8_t *data = qo_byte_data(payload);

    uint32_t net_len = htonl((uint32_t)data_len);
    if (write_all(fd, &net_len, 4) < 0 || write_all(fd, data, (size_t)data_len) < 0) {
        qo_release(payload);
        ipc_close(fd);
        EVAL_ERROR("connection error on send");
    }
    qo_release(payload);

    uint32_t resp_len;
    if (read_all(fd, &resp_len, 4) < 0) {
        ipc_close(fd);
        EVAL_ERROR("connection error on recv");
    }
    resp_len = ntohl(resp_len);

    uint8_t *resp = xmalloc(resp_len);
    if (read_all(fd, resp, resp_len) < 0) {
        free(resp);
        ipc_close(fd);
        EVAL_ERROR("connection error on recv");
    }

    Qo result = ipc_deserialize(resp, resp_len);
    free(resp);
    if (result == NULL) { ipc_close(fd); EVAL_ERROR("failed to deserialize response"); }
    return result;
}
