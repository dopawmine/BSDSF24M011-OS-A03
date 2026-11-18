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
            return 0;
    }
}

int execute_pipeline(Command* cmds, int num_cmds) {
    if (cmds == NULL || num_cmds <= 0) {
        return -1;
    }

    // Run single unredirected built-in directly in the parent shell process
    if (num_cmds == 1 &&
        cmds[0].input_file == NULL &&
        cmds[0].output_file == NULL &&
        is_builtin_cmd(cmds[0].argv[0])) {
        handle_builtin(cmds[0].argv);
    return 0;
        }

        int prev_read_fd = -1;
        int pipefd[2];
        pid_t pids[MAX_CMDS];

        for (int i = 0; i < num_cmds; i++) {
            // Create a pipe for every stage except the last
            if (i < num_cmds - 1) {
                if (pipe(pipefd) < 0) {
                    perror("pipe");
                    if (prev_read_fd != -1) close(prev_read_fd);
                    return -1;
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
                return -1;
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
                    close(pipefd[0]); // Close unused read end in writer
                    if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                        perror("dup2 next pipe");
                        exit(1);
                    }
                    close(pipefd[1]);
                }

                // Apply explicit '<' or '>' file redirections (overrides pipe ends if specified)
                apply_redirections(&cmds[i]);

                // Allow built-ins (e.g., history | grep ls) to run inside a pipeline child
                if (is_builtin_cmd(cmds[i].argv[0])) {
                    handle_builtin(cmds[i].argv);
                    exit(0);
                }

                execvp(cmds[i].argv[0], cmds[i].argv);
                perror("Command not found");
                exit(1);
            }

            // Parent process: record child PID and close used pipe descriptors
            pids[i] = pid;

            if (prev_read_fd != -1) {
                close(prev_read_fd);
            }
            if (i < num_cmds - 1) {
                close(pipefd[1]);        // Parent never writes to the pipe
                prev_read_fd = pipefd[0]; // Save read end for the next stage
            }
        }

        // Wait for all children in the pipeline to finish
        int status;
        for (int i = 0; i < num_cmds; i++) {
            waitpid(pids[i], &status, 0);
        }

        return 0;
}
