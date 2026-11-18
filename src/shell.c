#include "shell.h"

static char* history[HISTORY_SIZE];
static int history_count = 0;

static const char* builtin_cmds[] = {
    "cd",
    "exit",
    "help",
    "history",
    "jobs",
    NULL
};

// Generator for built-in commands and executables in $PATH
static char* command_generator(const char* text, int state) {
    static int builtin_idx;
    static size_t len;
    static char* path_copy;
    static char* current_dir;
    static DIR* dir;

    if (state == 0) {
        builtin_idx = 0;
        len = strlen(text);
        if (dir != NULL) {
            closedir(dir);
            dir = NULL;
        }
        free(path_copy);
        path_copy = NULL;
        current_dir = NULL;

        const char* path_env = getenv("PATH");
        if (path_env != NULL) {
            path_copy = strdup(path_env);
            if (path_copy != NULL) {
                current_dir = strtok(path_copy, ":");
            }
        }
    }

    // 1. Match built-in commands first
    while (builtin_cmds[builtin_idx] != NULL) {
        const char* name = builtin_cmds[builtin_idx++];
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    // 2. Match executable files in $PATH directories
    while (current_dir != NULL || dir != NULL) {
        if (dir == NULL) {
            dir = opendir(current_dir);
            current_dir = strtok(NULL, ":");
            if (dir == NULL) {
                continue;
            }
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') {
                continue;
            }
            if (strncmp(entry->d_name, text, len) == 0) {
                return strdup(entry->d_name);
            }
        }

        closedir(dir);
        dir = NULL;
    }

    free(path_copy);
    path_copy = NULL;
    return NULL;
}

// Custom completion hook: command completion for first word, filename completion otherwise
static char** shell_completion(const char* text, int start, int end) {
    (void)end;

    // Check if only whitespace precedes `start` (meaning we are completing the command name)
    int is_command_pos = 1;
    for (int i = 0; i < start; i++) {
        if (rl_line_buffer[i] != ' ' && rl_line_buffer[i] != '\t') {
            is_command_pos = 0;
            break;
        }
    }

    if (is_command_pos) {
        // Also allow default filename completion if no command matches (e.g., ./script)
        return rl_completion_matches(text, command_generator);
    }

    // Returning NULL lets Readline perform default filename completion
    return NULL;
}

void init_readline(void) {
    rl_attempted_completion_function = shell_completion;
    using_history();
}

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
    clear_history();
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
