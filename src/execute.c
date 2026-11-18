#include "shell.h"

static int is_builtin_cmd(const char* cmd) {
    if (cmd == NULL) return 0;
    return (strcmp(cmd, "exit") == 0 ||
    strcmp(cmd, "cd") == 0 ||
    strcmp(cmd, "help") == 0 ||
    strcmp(cmd, "jobs") == 0 ||
    strcmp(cmd, "history") == 0);
}

static void apply_redirections(const Command* cmd) {
    if (cmd->input_file != NULL) {
        int fd_in = open(cmd->input_file, O_RDONLY);
        if (fd_in < 0) {
            perror(cmd->input_file);
            exit(1);
        }
        if (dup2(fd_in, STDIN_FILENO) < 0) {
            perror("dup2 input");
            close(fd_in);
            exit(1);
        }
        close(fd_in);
    }

    if (cmd->output_file != NULL) {
        int fd_out = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) {
            perror(cmd->output_file);
            exit(1);
        }
        if (dup2(fd_out, STDOUT_FILENO) < 0) {
            perror("dup2 output");
            close(fd_out);
            exit(1);
        }
        close(fd_out);
    }
}

int execute(char* arglist[]) {
    int status;
    int cpid = fork();

    switch (cpid) {
        case -1:
            perror("fork failed");
            exit(1);
        case 0: // Child process
            execvp(arglist[0], arglist);
            perror("Command not found");
            exit(1);
        default: // Parent process
            waitpid(cpid, &status, 0);
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
            return 1;
    }
}

int execute_pipeline(Command* cmds, int num_cmds, int background, const char* raw_cmd) {
    if (cmds == NULL || num_cmds <= 0) {
        return 1;
    }

    // Run single unredirected, non-background built-in directly in the parent shell
    if (!background &&
        num_cmds == 1 &&
        cmds[0].input_file == NULL &&
        cmds[0].output_file == NULL &&
        is_builtin_cmd(cmds[0].argv[0])) {
        int builtin_status = 0;
    handle_builtin(cmds[0].argv, &builtin_status);
    return builtin_status;
        }

        int prev_read_fd = -1;
        int pipefd[2];
        pid_t pids[MAX_CMDS];

        for (int i = 0; i < num_cmds; i++) {
            if (i < num_cmds - 1) {
                if (pipe(pipefd) < 0) {
                    perror("pipe");
                    if (prev_read_fd != -1) close(prev_read_fd);
                    return 1;
                }
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork failed");
                if (prev_read_fd != -1) close(prev_read_fd);
                if (i < num_cmds - 1) {
                    close(pipefd[0]);
                    close(pipefd[1]);
                }
                return 1;
            }

            if (pid == 0) {
                // Child process: connect stdin to previous pipe's read end
                if (prev_read_fd != -1) {
                    if (dup2(prev_read_fd, STDIN_FILENO) < 0) {
                        perror("dup2 prev pipe");
                        exit(1);
                    }
                    close(prev_read_fd);
                }

                // Connect stdout to current pipe's write end
                if (i < num_cmds - 1) {
                    close(pipefd[0]);
                    if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                        perror("dup2 next pipe");
                        exit(1);
                    }
                    close(pipefd[1]);
                }

                // Apply explicit '<' or '>' file redirections
                apply_redirections(&cmds[i]);

                if (is_builtin_cmd(cmds[i].argv[0])) {
                    int builtin_status = 0;
                    handle_builtin(cmds[i].argv, &builtin_status);
                    exit(builtin_status);
                }

                execvp(cmds[i].argv[0], cmds[i].argv);
                perror("Command not found");
                exit(1);
            }

            // Parent process
            pids[i] = pid;

            if (prev_read_fd != -1) {
                close(prev_read_fd);
            }
            if (i < num_cmds - 1) {
                close(pipefd[1]);
                prev_read_fd = pipefd[0];
            }
        }

        if (background) {
            add_job(pids[num_cmds - 1], raw_cmd);
            return 0;
        }

        // Foreground: wait for all stages and capture the final stage's exit code
        int status = 0;
        int last_exit_code = 0;
        for (int i = 0; i < num_cmds; i++) {
            if (waitpid(pids[i], &status, 0) > 0 && i == num_cmds - 1) {
                if (WIFEXITED(status)) {
                    last_exit_code = WEXITSTATUS(status);
                } else {
                    last_exit_code = 1;
                }
            }
        }

        return last_exit_code;
}
