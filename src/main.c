#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>
#include "evaluator.h"
#include "internal.h"
#include "lexer.h"
#include "parser.h"
#include "ipc.h"

static int eval_string(const char *input, Environment *env, int print_result) {
    TokenBuffer buffer = tokenize_input(input);
    Parser *parser = parser_new(buffer.tokens, buffer.count, input);
    Qo tree = parser_parse(parser);
    int ok = 0;
    
    if (!is_parse_error(tree)) {
        Qo result = eval_value(tree, env);
        if (print_result && !evaluator_exit_requested() && !evaluator_error_requested()) {
            if (result != NULL) {
                qo_print_with_limits(result, 25, 80);
            }
            printf("\n");
        }
        value_free(result);
        value_free(tree);
        ok = 1;
    } else {
        fprintf(stderr, "Error: Failed to parse expression\n");
    }
    
    parser_free(parser);
    free_token_buffer(buffer);
    return ok;
}

static int run_file(const char *path, Environment *env) {
    char *source = read_file_contents(path);
    if (!source) {
        return 1;
    }

    int ok = eval_string(source, env, 1);
    free(source);
    return ok ? 0 : 1;
}

static int run_repl(Environment *env) {
    char input[1024];
    int is_tty = isatty(STDIN_FILENO);

    if (is_tty) {
        printf("Welcome to qo - Simple Expression Evaluator\n");
        printf("Type Ctrl-D to exit, or run 'exit <value>'\n\n");
    }

    int show_prompt = 1;

    while (!evaluator_exit_requested()) {
        if (is_tty && show_prompt) {
            printf("qo>");
            fflush(stdout);
        }
        show_prompt = 0;

        struct pollfd fds[3];
        nfds_t nfds = 0;
        int server_fd = ipc_server_fd();
        int conn_fd = ipc_connection_fd();

        fds[nfds].fd = 0;
        fds[nfds].events = POLLIN;
        nfds++;

        if (server_fd >= 0) {
            fds[nfds].fd = server_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        if (conn_fd >= 0) {
            fds[nfds].fd = conn_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        if (poll(fds, nfds, -1) < 0) break;

        if (fds[0].revents & (POLLIN | POLLHUP)) {
            if (!fgets(input, sizeof(input), stdin)) {
                break;
            }

            size_t len = strlen(input);
            if (len > 0 && input[len - 1] == '\n') {
                input[len - 1] = '\0';
            }

            if (strlen(input) == 0) {
                show_prompt = 1;
                continue;
            }

            eval_string(input, env, 1);
            evaluator_reset_error();
            show_prompt = 1;
        }

        // fds[1] is server_fd if present, else conn_fd
        int idx = 1;
        if (server_fd >= 0) {
            if (fds[idx].revents & POLLIN) {
                ipc_accept_connection();
            }
            idx++;
        }

        if (conn_fd >= 0 && (fds[idx].revents & POLLIN)) {
            ipc_process_connection(env);
        }
    }

    if (evaluator_exit_requested()) {
        return (int)evaluator_exit_code();
    }
    return 0;
}

int main(int argc, char **argv) {
    int exit_code = 0;

    if (argc > 2) {
        fprintf(stderr, "Usage: qo [file.qo]\n");
        return 1;
    }

    evaluator_reset_exit();
    ipc_init();
    Environment *env = env_new();
    // File execution and the REPL share one environment so script-defined values remain available.
    if (argc == 2) {
        exit_code = run_file(argv[1], env);
        if (!exit_code && evaluator_exit_requested()) {
            exit_code = (int)evaluator_exit_code();
        }
    }

    if (!evaluator_exit_requested()) {
        int repl_code = run_repl(env);
        if (evaluator_exit_requested()) {
            exit_code = repl_code;
        } else if (!exit_code) {
            exit_code = repl_code;
        }
    } else if (!exit_code) {
        exit_code = (int)evaluator_exit_code();
    }

    ipc_cleanup();
    env_free(env);
    return exit_code;
}
