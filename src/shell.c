#include "shell.h"

static char* history[HISTORY_SIZE];
static int history_count = 0;

void add_to_history(const char* cmd) {
    if (cmd == NULL) {
        return;
    }

    // Ignore empty or whitespace-only commands
    const char* p = cmd;
    while (*p == ' ' || *p == '\t' || *p == '\n') {
        p++;
    }
    if (*p == '\0') {
        return;
    }

    char* copy = strdup(cmd);
    if (copy == NULL) {
        perror("strdup");
        return;
    }

    if (history_count < HISTORY_SIZE) {
        history[history_count++] = copy;
    } else {
        // History is full: free the oldest command and shift remaining entries left
        free(history[0]);
        for (int i = 1; i < HISTORY_SIZE; i++) {
            history[i - 1] = history[i];
        }
        history[HISTORY_SIZE - 1] = copy;
    }
}

const char* get_from_history(int index) {
    if (index < 1 || index > history_count) {
        return NULL;
    }
    return history[index - 1];
}

void print_history(void) {
    for (int i = 0; i < history_count; i++) {
        printf("%d  %s\n", i + 1, history[i]);
    }
}

void free_history(void) {
    for (int i = 0; i < history_count; i++) {
        free(history[i]);
        history[i] = NULL;
    }
    history_count = 0;
}

char* read_cmd(char* prompt, FILE* fp) {
    printf("%s", prompt);
    char* cmdline = (char*) malloc(sizeof(char) * MAX_LEN);
    int c, pos = 0;

    while ((c = getc(fp)) != EOF) {
        if (c == '\n') break;
        if (pos < MAX_LEN - 1) {
            cmdline[pos++] = c;
        }
    }

    if (c == EOF && pos == 0) {
        free(cmdline);
        return NULL; // Handle Ctrl+D
    }

    cmdline[pos] = '\0';
    return cmdline;
}

char** tokenize(char* cmdline) {
    // Edge case: empty command line
    if (cmdline == NULL || cmdline[0] == '\0' || cmdline[0] == '\n') {
        return NULL;
    }

    char** arglist = (char**)malloc(sizeof(char*) * (MAXARGS + 1));
    for (int i = 0; i < MAXARGS + 1; i++) {
        arglist[i] = (char*)malloc(sizeof(char) * ARGLEN);
        bzero(arglist[i], ARGLEN);
    }

    char* cp = cmdline;
    char* start;
    int len;
    int argnum = 0;

    while (*cp != '\0' && argnum < MAXARGS) {
        while (*cp == ' ' || *cp == '\t') cp++; // Skip leading whitespace

        if (*cp == '\0') break; // Line was only whitespace

        start = cp;
        len = 1;
        while (*++cp != '\0' && !(*cp == ' ' || *cp == '\t')) {
            len++;
        }
        if (len >= ARGLEN) {
            len = ARGLEN - 1;
        }
        strncpy(arglist[argnum], start, len);
        arglist[argnum][len] = '\0';
        argnum++;
    }

    if (argnum == 0) { // No arguments were parsed
        for (int i = 0; i < MAXARGS + 1; i++) free(arglist[i]);
        free(arglist);
        return NULL;
    }

    // Free unused pre-allocated argument slots before NULL-terminating
    for (int i = argnum; i < MAXARGS + 1; i++) {
        free(arglist[i]);
    }
    arglist[argnum] = NULL;
    return arglist;
}

int handle_builtin(char** arglist) {
    if (arglist == NULL || arglist[0] == NULL) {
        return 0;
    }

    if (strcmp(arglist[0], "exit") == 0) {
        free_history();
        printf("Shell exited.\n");
        exit(0);
    }

    if (strcmp(arglist[0], "cd") == 0) {
        const char* target_dir = arglist[1];
        if (target_dir == NULL) {
            target_dir = getenv("HOME");
            if (target_dir == NULL) {
                fprintf(stderr, "cd: HOME not set\n");
                return 1;
            }
        }
        if (chdir(target_dir) != 0) {
            perror("cd");
        }
        return 1;
    }

    if (strcmp(arglist[0], "help") == 0) {
        printf("Built-in commands:\n");
        printf("  cd <directory>  Change the current working directory\n");
        printf("  history         Display the command history list\n");
        printf("  !n              Re-execute the n-th command from history\n");
        printf("  help            Display this help message\n");
        printf("  jobs            List active background jobs\n");
        printf("  exit            Exit the shell\n");
        return 1;
    }

    if (strcmp(arglist[0], "jobs") == 0) {
        printf("Job control not yet implemented.\n");
        return 1;
    }

    if (strcmp(arglist[0], "history") == 0) {
        print_history();
        return 1;
    }

    return 0; // Not a built-in command
}
