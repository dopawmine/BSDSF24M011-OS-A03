#include "shell.h"

static char* history[HISTORY_SIZE];
static int history_count = 0;

static Job job_list[MAX_JOBS];
static int next_job_id = 1;

static ShellVar* var_head = NULL;

static const char* builtin_cmds[] = {
    "cd",
    "exit",
    "help",
    "history",
    "jobs",
    "set",
    "if",
    "then",
    "else",
    "fi",
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

    // 1. Match built-in commands and keywords first
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

    int is_command_pos = 1;
    for (int i = 0; i < start; i++) {
        if (rl_line_buffer[i] != ' ' && rl_line_buffer[i] != '\t') {
            is_command_pos = 0;
            break;
        }
    }

    if (is_command_pos) {
        return rl_completion_matches(text, command_generator);
    }

    return NULL;
}

void init_readline(void) {
    rl_attempted_completion_function = shell_completion;
    using_history();
}

int is_valid_var_name(const char* name) {
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (!isalpha((unsigned char)name[0]) && name[0] != '_') {
        return 0;
    }
    for (int i = 1; name[i] != '\0'; i++) {
        if (!isalnum((unsigned char)name[i]) && name[i] != '_') {
            return 0;
        }
    }
    return 1;
}

int is_var_assignment(const char* token) {
    if (token == NULL) {
        return 0;
    }
    const char* eq = strchr(token, '=');
    if (eq == NULL || eq == token) {
        return 0;
    }

    size_t name_len = (size_t)(eq - token);
    char* name = (char*)malloc(name_len + 1);
    if (name == NULL) {
        return 0;
    }
    memcpy(name, token, name_len);
    name[name_len] = '\0';

    int valid = is_valid_var_name(name);
    free(name);
    return valid;
}

void set_shell_var(const char* name, const char* value) {
    if (!is_valid_var_name(name)) {
        return;
    }

    const char* val_str = (value != NULL) ? value : "";

    // Update existing variable if already defined
    ShellVar* curr = var_head;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            char* new_val = strdup(val_str);
            if (new_val == NULL) {
                perror("strdup");
                return;
            }
            free(curr->value);
            curr->value = new_val;
            return;
        }
        curr = curr->next;
    }

    // Insert new variable node at the head of the linked list
    ShellVar* node = (ShellVar*)malloc(sizeof(ShellVar));
    if (node == NULL) {
        perror("malloc");
        return;
    }
    node->name = strdup(name);
    node->value = strdup(val_str);
    if (node->name == NULL || node->value == NULL) {
        perror("strdup");
        free(node->name);
        free(node->value);
        free(node);
        return;
    }
    node->next = var_head;
    var_head = node;
}

const char* get_shell_var(const char* name) {
    if (name == NULL || name[0] == '\0') {
        return NULL;
    }

    ShellVar* curr = var_head;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            return curr->value;
        }
        curr = curr->next;
    }

    // Fallback to process environment variables (e.g., $HOME, $PATH)
    return getenv(name);
}

int handle_var_assignment(const char* assignment) {
    const char* eq = strchr(assignment, '=');
    if (eq == NULL) {
        return 1;
    }

    size_t name_len = (size_t)(eq - assignment);
    char* name = (char*)malloc(name_len + 1);
    if (name == NULL) {
        perror("malloc");
        return 1;
    }
    memcpy(name, assignment, name_len);
    name[name_len] = '\0';

    const char* raw_val = eq + 1;
    // Allow right-hand side to reference another variable (e.g., COPY=$MSG)
    if (raw_val[0] == '$' && raw_val[1] != '\0') {
        const char* expanded = get_shell_var(raw_val + 1);
        set_shell_var(name, expanded != NULL ? expanded : "");
    } else {
        set_shell_var(name, raw_val);
    }

    free(name);
    return 0;
}

void print_shell_vars(void) {
    if (var_head == NULL) {
        printf("No shell variables defined.\n");
        return;
    }

    ShellVar* curr = var_head;
    while (curr != NULL) {
        printf("%s=%s\n", curr->name, curr->value);
        curr = curr->next;
    }
}

void free_shell_vars(void) {
    ShellVar* curr = var_head;
    while (curr != NULL) {
        ShellVar* next = curr->next;
        free(curr->name);
        free(curr->value);
        free(curr);
        curr = next;
    }
    var_head = NULL;
}

void expand_variables(char** tokens) {
    if (tokens == NULL) {
        return;
    }

    for (int i = 0; tokens[i] != NULL; i++) {
        // Skip variable expansion on the LHS of a standalone assignment
        if (i == 0 && is_var_assignment(tokens[i])) {
            continue;
        }

        if (tokens[i][0] == '$' && tokens[i][1] != '\0') {
            const char* var_name = tokens[i] + 1;
            const char* val = get_shell_var(var_name);
            char* replacement = strdup(val != NULL ? val : "");
            if (replacement != NULL) {
                free(tokens[i]);
                tokens[i] = replacement;
            }
        }
    }
}

void add_job(pid_t pid, const char* cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!job_list[i].active) {
            job_list[i].job_id = next_job_id++;
            job_list[i].pid = pid;
            job_list[i].cmd = strdup(cmd != NULL ? cmd : "");
            job_list[i].active = 1;
            printf("[%d] %d\n", job_list[i].job_id, (int)pid);
            return;
        }
    }
    fprintf(stderr, "Warning: maximum background jobs limit reached\n");
}

void reap_background_jobs(void) {
    int status;
    pid_t pid;

    // Non-blocking reap of all terminated child processes to prevent zombies
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (job_list[i].active && job_list[i].pid == pid) {
                printf("\n[%d] Done\t%s\n", job_list[i].job_id, job_list[i].cmd);
                free(job_list[i].cmd);
                job_list[i].cmd = NULL;
                job_list[i].active = 0;
                break;
            }
        }
    }

    // Reset job ID counter when no active jobs remain
    int any_active = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_list[i].active) {
            any_active = 1;
            break;
        }
    }
    if (!any_active) {
        next_job_id = 1;
    }
}

void print_jobs(void) {
    reap_background_jobs();

    int found = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_list[i].active) {
            printf("[%d] Running\tPID: %d\t%s &\n",
                   job_list[i].job_id, (int)job_list[i].pid, job_list[i].cmd);
            found = 1;
        }
    }

    if (!found) {
        printf("No active background jobs.\n");
    }
}

void free_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_list[i].active) {
            free(job_list[i].cmd);
            job_list[i].cmd = NULL;
            job_list[i].active = 0;
        }
    }
}

void add_to_history(const char* cmd) {
    if (cmd == NULL) {
        return;
    }

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

void free_tokens(char** tokens) {
    if (tokens == NULL) return;
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

char** tokenize(char* cmdline) {
    if (cmdline == NULL || cmdline[0] == '\0' || cmdline[0] == '\n') {
        return NULL;
    }

    char** arglist = (char**)malloc(sizeof(char*) * (MAXARGS + 1));
    if (arglist == NULL) {
        perror("malloc");
        return NULL;
    }

    char* cp = cmdline;
    int argnum = 0;

    while (*cp != '\0' && argnum < MAXARGS) {
        while (*cp == ' ' || *cp == '\t') cp++;
        if (*cp == '\0') break;

        // Treat <, >, and | as distinct operator tokens
        if (*cp == '<' || *cp == '>' || *cp == '|') {
            char* op = (char*)malloc(2);
            op[0] = *cp;
            op[1] = '\0';
            arglist[argnum++] = op;
            cp++;
            continue;
        }

        // Read regular word (respecting single/double quotes)
        char buf[ARGLEN];
        int len = 0;
        char quote = '\0';

        while (*cp != '\0') {
            if (quote == '\0') {
                if (*cp == ' ' || *cp == '\t' || *cp == '<' || *cp == '>' || *cp == '|') {
                    break;
                }
                if (*cp == '"' || *cp == '\'') {
                    quote = *cp++;
                    continue;
                }
            } else {
                if (*cp == quote) {
                    quote = '\0';
                    cp++;
                    continue;
                }
            }

            if (len < ARGLEN - 1) {
                buf[len++] = *cp;
            }
            cp++;
        }

        buf[len] = '\0';
        arglist[argnum++] = strdup(buf);
    }

    if (argnum == 0) {
        free(arglist);
        return NULL;
    }

    arglist[argnum] = NULL;
    return arglist;
}

int parse_pipeline(char** tokens, Command* cmds, int* num_cmds) {
    if (tokens == NULL || tokens[0] == NULL) {
        *num_cmds = 0;
        return -1;
    }

    int cmd_idx = 0;
    int argc = 0;

    cmds[0].argv = (char**)calloc(MAXARGS + 1, sizeof(char*));
    cmds[0].input_file = NULL;
    cmds[0].output_file = NULL;

    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            if (argc == 0 || tokens[i + 1] == NULL || strcmp(tokens[i + 1], "|") == 0) {
                fprintf(stderr, "Syntax error near unexpected token `|'\n");
                cmds[cmd_idx].argv[argc] = NULL;
                *num_cmds = cmd_idx + 1;
                return -1;
            }
            cmds[cmd_idx].argv[argc] = NULL;
            cmd_idx++;
            if (cmd_idx >= MAX_CMDS) {
                fprintf(stderr, "Error: too many piped commands\n");
                *num_cmds = cmd_idx;
                return -1;
            }
            argc = 0;
            cmds[cmd_idx].argv = (char**)calloc(MAXARGS + 1, sizeof(char*));
            cmds[cmd_idx].input_file = NULL;
            cmds[cmd_idx].output_file = NULL;
        } else if (strcmp(tokens[i], "<") == 0) {
            if (tokens[i + 1] == NULL || strcmp(tokens[i + 1], "<") == 0 ||
                strcmp(tokens[i + 1], ">") == 0 || strcmp(tokens[i + 1], "|") == 0) {
                fprintf(stderr, "Syntax error: expected filename after `<'\n");
            cmds[cmd_idx].argv[argc] = NULL;
            *num_cmds = cmd_idx + 1;
            return -1;
                }
                free(cmds[cmd_idx].input_file);
                cmds[cmd_idx].input_file = strdup(tokens[++i]);
        } else if (strcmp(tokens[i], ">") == 0) {
            if (tokens[i + 1] == NULL || strcmp(tokens[i + 1], "<") == 0 ||
                strcmp(tokens[i + 1], ">") == 0 || strcmp(tokens[i + 1], "|") == 0) {
                fprintf(stderr, "Syntax error: expected filename after `>'\n");
            cmds[cmd_idx].argv[argc] = NULL;
            *num_cmds = cmd_idx + 1;
            return -1;
                }
                free(cmds[cmd_idx].output_file);
                cmds[cmd_idx].output_file = strdup(tokens[++i]);
        } else {
            cmds[cmd_idx].argv[argc++] = strdup(tokens[i]);
        }
    }

    if (argc == 0) {
        fprintf(stderr, "Syntax error: missing command\n");
        cmds[cmd_idx].argv[argc] = NULL;
        *num_cmds = cmd_idx + 1;
        return -1;
    }

    cmds[cmd_idx].argv[argc] = NULL;
    *num_cmds = cmd_idx + 1;
    return 0;
}

void free_commands(Command* cmds, int num_cmds) {
    for (int i = 0; i < num_cmds; i++) {
        if (cmds[i].argv != NULL) {
            for (int j = 0; cmds[i].argv[j] != NULL; j++) {
                free(cmds[i].argv[j]);
            }
            free(cmds[i].argv);
            cmds[i].argv = NULL;
        }
        free(cmds[i].input_file);
        cmds[i].input_file = NULL;
        free(cmds[i].output_file);
        cmds[i].output_file = NULL;
    }
}

int handle_builtin(char** arglist, int* exit_status) {
    if (arglist == NULL || arglist[0] == NULL) {
        return 0;
    }

    if (exit_status != NULL) {
        *exit_status = 0;
    }

    // Handle VARNAME=value assignment as a built-in action
    if (is_var_assignment(arglist[0]) && arglist[1] == NULL) {
        int status = handle_var_assignment(arglist[0]);
        if (exit_status != NULL) *exit_status = status;
        return 1;
    }

    if (strcmp(arglist[0], "exit") == 0) {
        free_shell_vars();
        free_jobs();
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
                if (exit_status != NULL) *exit_status = 1;
                return 1;
            }
        }
        if (chdir(target_dir) != 0) {
            perror("cd");
            if (exit_status != NULL) *exit_status = 1;
        }
        return 1;
    }

    if (strcmp(arglist[0], "help") == 0) {
        printf("Built-in commands and control structures:\n");
        printf("  cd <directory>          Change the current working directory\n");
        printf("  history                 Display the command history list\n");
        printf("  !n                      Re-execute the n-th command from history\n");
        printf("  jobs                    List active background jobs\n");
        printf("  if ... then ... fi      Conditional multi-line command execution\n");
        printf("  help                    Display this help message\n");
        printf("  exit                    Exit the shell\n");
        return 1;
    }

    if (strcmp(arglist[0], "jobs") == 0) {
        print_jobs();
        return 1;
    }

    if (strcmp(arglist[0], "history") == 0) {
        print_history();
        return 1;
    }

    if (strcmp(arglist[0], "set") == 0) {
        print_shell_vars();
        return 1;
    }

    return 0; // Not a built-in command
}
