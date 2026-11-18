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

// Splits a command line on unquoted ';' and '&' delimiters and executes each segment
static void process_command_line(char* cmdline) {
    char* line_copy = strdup(cmdline);
    if (line_copy == NULL) {
        perror("strdup");
        return;
    }

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
                        execute_pipeline(cmds, num_cmds, background, segment);
                    }

                    free_commands(cmds, num_cmds);
                    free_tokens(tokens);
                }
            } else if (ch == '&') {
                fprintf(stderr, "Syntax error near unexpected token `&'\n");
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
