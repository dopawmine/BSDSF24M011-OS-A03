#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_LEN 512
#define MAXARGS 10
#define ARGLEN 30
#define HISTORY_SIZE 20
#define PROMPT "FCIT> "

// Function prototypes
void init_readline(void);
char** tokenize(char* cmdline);
int execute(char** arglist);
int handle_builtin(char** arglist);

// History management prototypes
void add_to_history(const char* cmd);
const char* get_from_history(int index);
void print_history(void);
void free_history(void);

#endif // SHELL_H
