#include "shell.h"

static int is_blank(const char* str) {
    if (str == NULL) return 1;
    while (*str != '\0') {
        if (*str != ' ' && *str != '\t' && *str != '\n') {
            return 0;
        }
        str++;
    }
    return 1;
}

static char* trim_whitespace(char* str) {
    while (*str == ' ' || *str == '\t' || *str == '\n') {
        str++;
    }
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
    return str;
}

// Checks whether a trimmed line starts with a specific keyword token (e.g., "if", "then", "else", "fi")
static int line_has_keyword(const char* line, const char* kw) {
    while (*line == ' ' || *line == '\t') line++;
    size_t kw_len = strlen(kw);
    if (strncmp(line, kw, kw_len) != 0) {
        return 0;
    }
    char next = line[kw_len];
    return (next == '\0' || next == ' ' || next == '\t' || next == ';' || next == '\n');
}

int starts_with_if(const char* line) {
    if (line == NULL) return 0;
    return line_has_keyword(line, "if");
}

static void free_line_buffer(char** lines, int count) {
    for (int i = 0; i < count; i++) {
        free(lines[i]);
        lines[i] = NULL;
    }
}

static int execute_block_lines(char** lines, int count) {
    int last_status = 0;
    int i = 0;

    while (i < count) {
        if (starts_with_if(lines[i])) {
            // Collect nested if ... fi lines from the buffered block
            char* nested_cond = strdup(lines[i] + 2);
            char* nested_then[MAX_BLOCK_LINES];
            char* nested_else[MAX_BLOCK_LINES];
            int n_then = 0, n_else = 0;
            int depth = 1;
            int state = 0; // 0: before then, 1: in then, 2: in else

            i++;
            while (i < count && depth > 0) {
                if (starts_with_if(lines[i])) {
                    depth++;
                } else if (line_has_keyword(lines[i], "fi")) {
                    depth--;
                    if (depth == 0) {
                        i++;
                        break;
                    }
                }

                if (depth == 1 && state == 0 && line_has_keyword(lines[i], "then")) {
                    state = 1;
                } else if (depth == 1 && state == 1 && line_has_keyword(lines[i], "else")) {
                    state = 2;
                } else if (state == 1 && n_then < MAX_BLOCK_LINES) {
                    nested_then[n_then++] = strdup(lines[i]);
                } else if (state == 2 && n_else < MAX_BLOCK_LINES) {
                    nested_else[n_else++] = strdup(lines[i]);
                }
                i++;
            }

            int cond_status = process_command_line(nested_cond);
            free(nested_cond);

            if (cond_status == 0) {
                last_status = execute_block_lines(nested_then, n_then);
            } else {
                last_status = execute_block_lines(nested_else, n_else);
            }

            free_line_buffer(nested_then, n_then);
            free_line_buffer(nested_else, n_else);
        } else {
            last_status = process_command_line(lines[i]);
            i++;
        }
    }

    return last_status;
}

int handle_if_statement(const char* first_line) {
    const char* p = first_line;
    while (*p == ' ' || *p == '\t') p++;
    p += 2; // Skip "if"
    while (*p == ' ' || *p == '\t') p++;

    if (*p == '\0') {
        fprintf(stderr, "Syntax error: expected command after `if'\n");
        return 1;
    }

    char* cond_cmd = strdup(p);
    char* then_lines[MAX_BLOCK_LINES];
    char* else_lines[MAX_BLOCK_LINES];
    int then_count = 0;
    int else_count = 0;

    // state: 0 = waiting for 'then', 1 = reading 'then' block, 2 = reading 'else' block
    int state = 0;
    int depth = 1;
    int syntax_error = 0;

    while (depth > 0) {
        char* next_line = readline(CONT_PROMPT);
        if (next_line == NULL) {
            fprintf(stderr, "Syntax error: unexpected EOF while looking for matching `fi'\n");
            syntax_error = 1;
            break;
        }

        if (!is_blank(next_line)) {
            add_history(next_line);
            add_to_history(next_line);
        }

        char* trimmed = trim_whitespace(next_line);
        if (*trimmed == '\0') {
            free(next_line);
            continue;
        }

        if (starts_with_if(trimmed)) {
            depth++;
            if (state == 1 && then_count < MAX_BLOCK_LINES) {
                then_lines[then_count++] = strdup(trimmed);
            } else if (state == 2 && else_count < MAX_BLOCK_LINES) {
                else_lines[else_count++] = strdup(trimmed);
            } else {
                fprintf(stderr, "Syntax error: unexpected `if' before `then'\n");
                syntax_error = 1;
                free(next_line);
                break;
            }
        } else if (depth == 1 && line_has_keyword(trimmed, "then")) {
            if (state != 0) {
                fprintf(stderr, "Syntax error near unexpected token `then'\n");
                syntax_error = 1;
                free(next_line);
                break;
            }
            state = 1;
        } else if (depth == 1 && line_has_keyword(trimmed, "else")) {
            if (state != 1) {
                fprintf(stderr, "Syntax error near unexpected token `else'\n");
                syntax_error = 1;
                free(next_line);
                break;
            }
            state = 2;
        } else if (line_has_keyword(trimmed, "fi")) {
            depth--;
            if (depth == 0) {
                if (state == 0) {
                    fprintf(stderr, "Syntax error: `fi' without `then'\n");
                    syntax_error = 1;
                }
                free(next_line);
                break;
            }
            if (state == 1 && then_count < MAX_BLOCK_LINES) {
                then_lines[then_count++] = strdup(trimmed);
            } else if (state == 2 && else_count < MAX_BLOCK_LINES) {
                else_lines[else_count++] = strdup(trimmed);
            }
        } else {
            if (state == 0) {
                fprintf(stderr, "Syntax error: expected `then', got `%s'\n", trimmed);
                syntax_error = 1;
                free(next_line);
                break;
            } else if (state == 1) {
                if (then_count < MAX_BLOCK_LINES) {
                    then_lines[then_count++] = strdup(trimmed);
                }
            } else if (state == 2) {
                if (else_count < MAX_BLOCK_LINES) {
                    else_lines[else_count++] = strdup(trimmed);
                }
            }
        }

        free(next_line);
    }

    int result_status = 1;
    if (!syntax_error) {
        int cond_status = process_command_line(cond_cmd);
        if (cond_status == 0) {
            result_status = execute_block_lines(then_lines, then_count);
        } else if (else_count > 0) {
            result_status = execute_block_lines(else_lines, else_count);
        } else {
            result_status = 0;
        }
    }

    free(cond_cmd);
    free_line_buffer(then_lines, then_count);
    free_line_buffer(else_lines, else_count);
    return result_status;
}

// Splits a command line on unquoted ';' and '&' delimiters and executes each segment
int process_command_line(char* cmdline) {
    if (starts_with_if(cmdline)) {
        return handle_if_statement(cmdline);
    }

    char* line_copy = strdup(cmdline);
    if (line_copy == NULL) {
        perror("strdup");
        return 1;
    }

    int last_status = 0;
    char* p = line_copy;
    char* seg_start = p;
    char quote = '\0';

    while (1) {
        char ch = *p;

        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            if (ch == '\0') break;
            p++;
            continue;
        }

        if (ch == '"' || ch == '\'') {
            quote = ch;
            p++;
            continue;
        }

        if (ch == ';' || ch == '&' || ch == '\0') {
            int background = (ch == '&') ? 1 : 0;
            *p = '\0';

            char* segment = trim_whitespace(seg_start);
            if (*segment != '\0') {
                char** tokens = tokenize(segment);
                if (tokens != NULL) {
                    Command cmds[MAX_CMDS];
                    int num_cmds = 0;

                    if (parse_pipeline(tokens, cmds, &num_cmds) == 0) {
                        last_status = execute_pipeline(cmds, num_cmds, background, segment);
                    } else {
                        last_status = 1;
                    }

                    free_commands(cmds, num_cmds);
                    free_tokens(tokens);
                }
            } else if (ch == '&') {
                fprintf(stderr, "Syntax error near unexpected token `&'\n");
                last_status = 1;
                break;
            }

            if (ch == '\0') {
                break;
            }
            seg_start = p + 1;
        }

        p++;
    }

    free(line_copy);
    return last_status;
}

int main() {
    char* cmdline;

    init_readline();

    while (1) {
        // Reap any finished background processes before displaying the prompt
        reap_background_jobs();

        cmdline = readline(PROMPT);
        if (cmdline == NULL) {
            break;
        }

        // Handle !n re-execution before adding to history or splitting
        if (cmdline[0] == '!') {
            char* endptr = NULL;
            errno = 0;
            long n = strtol(cmdline + 1, &endptr, 10);

            while (endptr != NULL && (*endptr == ' ' || *endptr == '\t')) {
                endptr++;
            }

            if (cmdline[1] == '\0' || !isdigit((unsigned char)cmdline[1]) ||
                errno != 0 || *endptr != '\0') {
                fprintf(stderr, "%s: invalid history event specification\n", cmdline);
            free(cmdline);
            continue;
                }

                const char* recalled = get_from_history((int)n);
                if (recalled == NULL) {
                    fprintf(stderr, "!%ld: event not found\n", n);
                    free(cmdline);
                    continue;
                }

                printf("%s\n", recalled);
                char* expanded = strdup(recalled);
                free(cmdline);
                if (expanded == NULL) {
                    perror("strdup");
                    continue;
                }
                cmdline = expanded;
        }

        if (!is_blank(cmdline)) {
            add_history(cmdline);
            add_to_history(cmdline);
            process_command_line(cmdline);
        }

        free(cmdline);
    }

    free_jobs();
    free_history();
    printf("\nShell exited.\n");
    return 0;
}
