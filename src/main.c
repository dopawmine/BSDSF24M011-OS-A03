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

int main() {
    char* cmdline;
    char** arglist;

    init_readline();

    while ((cmdline = readline(PROMPT)) != NULL) {
        // Handle !n re-execution before adding to history or tokenizing
        if (cmdline[0] == '!') {
            char* endptr = NULL;
            errno = 0;
            long n = strtol(cmdline + 1, &endptr, 10);

            // Skip optional trailing spaces after the number
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

        // Record non-empty command in both Readline history (Up/Down arrows) and shell history
        if (!is_blank(cmdline)) {
            add_history(cmdline);
            add_to_history(cmdline);
        }

        if ((arglist = tokenize(cmdline)) != NULL) {
            if (!handle_builtin(arglist)) {
                execute(arglist);
            }

            // Free the memory allocated by tokenize()
            for (int i = 0; arglist[i] != NULL; i++) {
                free(arglist[i]);
            }
            free(arglist);
        }
        free(cmdline);
    }

    free_history();
    printf("\nShell exited.\n");
    return 0;
}
