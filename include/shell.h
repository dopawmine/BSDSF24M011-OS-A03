#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_LEN 512
#define MAXARGS 32
#define ARGLEN 64
#define MAX_CMDS 16
#define HISTORY_SIZE 20
#define PROMPT "FCIT> "

typedef struct {
    char** argv;       // NULL-terminated argument vector
    char* input_file;  // Target file for '<' redirection (or NULL)
    char* output_file; // Target file for '>' redirection (or NULL)
} Command;

// Initialization and parsing prototypes
void init_readline(void);
char** tokenize(char* cmdline);
void free_tokens(char** tokens);
int parse_pipeline(char** tokens, Command* cmds, int* num_cmds);
void free_commands(Command* cmds, int num_cmds);

// Execution and built-in prototypes
int execute(char** arglist);
int execute_pipeline(Command* cmds, int num_cmds);
int handle_builtin(char** arglist);

// History management prototypes
void add_to_history(const char* cmd);
const char* get_from_history(int index);
void print_history(void);
void free_history(void);

#endif // SHELL_H
