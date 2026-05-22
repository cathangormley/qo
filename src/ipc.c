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
    switch (qo_type(q)) {
        case QO_SHORT: return 4;
        case QO_INT: return 6;
        case QO_LONG:
        case QO_FLOAT: return 10;
        case QO_CHAR:
        case QO_BOOL:
        case QO_BYTE: return 3;
        case QO_SYMBOL: return 10;
        case QO_OPERATOR:
        case QO_BUILTIN: return 3;
        case QO_CHAR_VEC: return (size_t)(10 + qo_count(q));
        case QO_SHORT_VEC: return (size_t)(10 + qo_count(q) * 2);
        case QO_INT_VEC: return (size_t)(10 + qo_count(q) * 4);
        case QO_LONG_VEC:
        case QO_FLOAT_VEC: return (size_t)(10 + qo_count(q) * 8);
        case QO_BOOL_VEC:
        case QO_BYTE_VEC: return (size_t)(10 + qo_count(q));
        default: return 0;
    }
}

Qo ipc_serialize(Qo arg) {
    if (arg == NULL) return alloc_data_vec(QO_BYTE_VEC, 0);
    uint8_t t = qo_type(arg);
    size_t sz = ipc_block_size(arg);
    if (sz == 0) { EVAL_ERROR("cannot serialize this type"); }
    Qo result = alloc_data_vec(QO_BYTE_VEC, (int64_t)sz);
    uint8_t *out = qo_byte_data(result);
    int64_t n;

    out[0] = t;
    out[1] = qo_attrs(arg);

    switch (t) {
        case QO_SHORT: { int16_t v = qo_short(arg); memcpy(out + 2, &v, 2); break; }
        case QO_INT: { int32_t v = qo_int(arg); memcpy(out + 2, &v, 4); break; }
        case QO_LONG:
        case QO_SYMBOL: {
            int64_t v = (t == QO_SYMBOL) ? qo_symbol_id(arg) : qo_long(arg);
            memcpy(out + 2, &v, 8); break;
        }
        case QO_FLOAT: { double v = qo_float(arg); memcpy(out + 2, &v, 8); break; }
        case QO_CHAR: out[2] = (uint8_t)qo_char(arg); break;
        case QO_BOOL: out[2] = qo_bool(arg); break;
        case QO_BYTE: out[2] = qo_byte(arg); break;
        case QO_OPERATOR:
            out[2] = QO_OPERATOR_OP(arg);
            break;
        case QO_BUILTIN:
            out[2] = QO_BUILTIN_ID(arg);
            break;
        case QO_CHAR_VEC:
            n = qo_count(arg);
            memcpy(out + 2, &n, 8);
            memcpy(out + 10, qo_char_data(arg), (size_t)n);
            break;
        case QO_SHORT_VEC:
            n = qo_count(arg);
            memcpy(out + 2, &n, 8);
            memcpy(out + 10, qo_short_data(arg), (size_t)n * 2);
            break;
        case QO_INT_VEC:
            n = qo_count(arg);
            memcpy(out + 2, &n, 8);
            memcpy(out + 10, qo_int_data(arg), (size_t)n * 4);
            break;
        case QO_LONG_VEC:
            n = qo_count(arg);
            memcpy(out + 2, &n, 8);
            memcpy(out + 10, qo_long_data(arg), (size_t)n * 8);
            break;
        case QO_FLOAT_VEC:
            n = qo_count(arg);
            memcpy(out + 2, &n, 8);
            memcpy(out + 10, qo_float_data(arg), (size_t)n * 8);
            break;
        case QO_BOOL_VEC:
            n = qo_count(arg);
            memcpy(out + 2, &n, 8);
            memcpy(out + 10, qo_bool_data(arg), (size_t)n);
            break;
        case QO_BYTE_VEC:
            n = qo_count(arg);
            memcpy(out + 2, &n, 8);
            memcpy(out + 10, qo_byte_data(arg), (size_t)n);
            break;
        default:
            qo_release(result);
            EVAL_ERROR("cannot serialize this type");
    }
    return result;
}

static Qo deserialize_scalar(uint8_t t, const uint8_t *data, size_t len) {
    (void)len;
    switch (t) {
        case QO_SHORT: { int16_t v; memcpy(&v, data, 2); return make_short_value(v); }
        case QO_INT: { int32_t v; memcpy(&v, data, 4); return make_int_value(v); }
        case QO_LONG: { int64_t v; memcpy(&v, data, 8); return make_long_value(v); }
        case QO_FLOAT: { double v; memcpy(&v, data, 8); return make_float_value(v); }
        case QO_CHAR: return make_char_value((char)data[0]);
        case QO_BOOL: return make_bool_value(data[0]);
        case QO_BYTE: return make_byte_value(data[0]);
        case QO_SYMBOL: { int64_t id; memcpy(&id, data, 8); return qo_symbol_by_id(id); }
        default: return NULL;
    }
}

static Qo deserialize_vector(uint8_t t, const uint8_t *data, size_t len) {
    int64_t n;
    if (len < 8) return NULL;
    memcpy(&n, data, 8);
    Qo result;
    switch (t) {
        case QO_CHAR_VEC:
            result = alloc_charlike(QO_CHAR_VEC, n);
            memcpy(qo_char_data(result), data + 8, (size_t)n);
            break;
        case QO_SHORT_VEC:
            result = alloc_data_vec(QO_SHORT_VEC, n);
            memcpy(qo_short_data(result), data + 8, (size_t)n * 2);
            break;
        case QO_INT_VEC:
            result = alloc_data_vec(QO_INT_VEC, n);
            memcpy(qo_int_data(result), data + 8, (size_t)n * 4);
            break;
        case QO_LONG_VEC:
            result = alloc_data_vec(QO_LONG_VEC, n);
            memcpy(qo_long_data(result), data + 8, (size_t)n * 8);
            break;
        case QO_FLOAT_VEC:
            result = alloc_data_vec(QO_FLOAT_VEC, n);
            memcpy(qo_float_data(result), data + 8, (size_t)n * 8);
            break;
        case QO_BOOL_VEC:
            result = alloc_data_vec(QO_BOOL_VEC, n);
            memcpy(qo_bool_data(result), data + 8, (size_t)n);
            break;
        case QO_BYTE_VEC:
            result = alloc_data_vec(QO_BYTE_VEC, n);
            memcpy(qo_byte_data(result), data + 8, (size_t)n);
            break;
        default:
            return NULL;
    }
    return result;
}

Qo ipc_deserialize(const uint8_t *data, size_t len) {
    if (len < 2) return make_null_value();
    uint8_t t = data[0];
    data += 2;
    len -= 2;

    switch (t) {
        case QO_SHORT: return (len < 2)  ? NULL : deserialize_scalar(t, data, len);
        case QO_INT:   return (len < 4)  ? NULL : deserialize_scalar(t, data, len);
        case QO_LONG:
        case QO_FLOAT:
        case QO_SYMBOL: return (len < 8)  ? NULL : deserialize_scalar(t, data, len);
        case QO_CHAR:
        case QO_BOOL:
        case QO_BYTE: return (len < 1)  ? NULL : deserialize_scalar(t, data, len);
        case QO_OPERATOR: return (len < 1) ? NULL : make_operator_value((TokenType)data[0]);
        case QO_BUILTIN:  return (len < 1) ? NULL : make_builtin_value(data[0]);
        case QO_CHAR_VEC:
        case QO_SHORT_VEC:
        case QO_INT_VEC:
        case QO_LONG_VEC:
        case QO_FLOAT_VEC:
        case QO_BOOL_VEC:
        case QO_BYTE_VEC:
            return (len < 8) ? NULL : deserialize_vector(t, data, len);
        default:
            return NULL;
    }
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
        return value_copy(arg);
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
