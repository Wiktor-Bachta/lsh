#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

char *lsh_read_line();
char **lsh_split_line(char *line);
int lsh_exec(char **args);
void lsh_loop();
int lsh_check_builtin(char **args);
int lsh_cd(char **args);
int lsh_exit();
int is_background(char **args);
int *split_pipe(char **args);
void lsh_wait();
void handle_sigint(int sig);
void redirect_input_output(char **args, int in_fd, int out_fd);

int main()
{
    signal(SIGINT, handle_sigint);
    lsh_loop();
    return 0;
}

void handle_sigint(int sig)
{
    printf("\nCaught Ctrl-C. Press Enter to continue.\n");
    fflush(stdout);
}

void lsh_loop()
{
    char *line;
    char **args;
    int status;

    do
    {
        printf("> ");
        line = lsh_read_line();
        args = lsh_split_line(line);
        status = lsh_check_builtin(args);

        free(line);
        free(args);
        // usuwa zombi
        lsh_wait();
    } while (status);
    return;
}

char *lsh_read_line()
{
    char *line = NULL;
    ssize_t bufsize = 0;

    if (getline(&line, &bufsize, stdin) == -1)
    {
        if (feof(stdin))
        {
            exit(EXIT_SUCCESS);
        }
        else
        {
            perror("readline");
            exit(EXIT_FAILURE);
        }
    }

    return line;
}

char **lsh_split_line(char *line)
{
    char **args;
    char *arg;
    int position = 0;
    int num_args = 64;
    args = malloc(num_args * sizeof(char *));
    if (!args)
    {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    arg = strtok(line, " \n");
    while (arg != NULL)
    {
        args[position] = arg;
        position++;
        arg = strtok(NULL, " \n");
        if (position >= num_args)
        {
            num_args += 64;
            args = realloc(args, num_args * sizeof(char *));
            if (!args)
            {
                fprintf(stderr, "lsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
    }
    args[position] = NULL;
    return args;
}

int lsh_check_builtin(char **args)
{
    if (args[0] == NULL)
    {
        return 1;
    }
    else if (!strcmp(args[0], "cd"))
    {
        return lsh_cd(args);
    }
    else if (!strcmp(args[0], "exit"))
    {
        return lsh_exit();
    }
    return lsh_exec(args);
}

int lsh_exec(char **args)
{
    pid_t child_pid;
    int fd[2];
    int in_fd = 0; // Input file descriptor for the first command
    int status;
    int bg = is_background(args);
    int *command_pos = split_pipe(args);
    int file_in = -1, file_out = -1, file_err = -1;

    for (int i = 0; command_pos[i] != -1; i++)
    {
        pipe(fd);
        child_pid = fork();

        if (child_pid == 0)
        {
            // Child process
            if (i != 0)
            {
                // If not the first command, redirect input from the previous command
                dup2(in_fd, 0);
                close(in_fd);
            }
            else
            {
                // Redirecting input
                for (int j = command_pos[i]; args[j] != NULL; j++)
                {
                    if (!strcmp(args[j], "<") && args[j + 1] != NULL)
                    {
                        file_in = open(args[j + 1], O_RDONLY);
                        if (file_in == -1)
                        {
                            perror("lsh");
                            exit(EXIT_FAILURE);
                        }
                        dup2(file_in, 0);
                        close(file_in);
                        args[j] = NULL;
                        args[j + 1] = NULL;
                    }
                }
            }

            if (command_pos[i + 1] != -1)
            {
                // If not the last command, redirect output to the next command
                dup2(fd[1], 1);
                close(fd[1]);
            }
            else
            {
                // Redirecting output
                for (int j = command_pos[i]; args[j] != NULL; j++)
                {
                    if (!strcmp(args[j], ">") && args[j + 1] != NULL)
                    {
                        file_out = open(args[j + 1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
                        if (file_out == -1)
                        {
                            perror("lsh");
                            exit(EXIT_FAILURE);
                        }
                        dup2(file_out, 1);
                        close(file_out);
                        args[j] = NULL;
                        args[j + 1] = NULL;
                    }
                    // Redirecting error output
                    else if (!strcmp(args[j], "2>") && args[j + 1] != NULL)
                    {
                        file_err = open(args[j + 1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
                        if (file_err == -1)
                        {
                            perror("lsh");
                            exit(EXIT_FAILURE);
                        }
                        dup2(file_err, 2);
                        close(file_err);
                        args[j] = NULL;
                        args[j + 1] = NULL;
                    }
                }
            }

            close(fd[0]); // Close the unused read end of the pipe
            execvp(args[command_pos[i]], &args[command_pos[i]]);
            perror("lsh");
            exit(EXIT_FAILURE);
        }
        else
        {
            // Parent process
            close(fd[1]); // Close the unused write end of the pipe
            if (!bg)
            {
                // If not running in the background, wait for the child process
                waitpid(child_pid, &status, 0);
            }

            in_fd = fd[0]; // Save the read end of the pipe for the next command
        }
    }

    free(command_pos);
    return 1;
}

int is_background(char **args)
{
    int i = 0;
    while (args[i] != NULL)
    {
        i++;
    }
    i--;
    if (!strcmp(args[i], "&"))
    {
        args[i] = NULL;
        return 1;
    }
    return 0;
}

int lsh_cd(char **args)
{
    if (args[1] == NULL)
    {
        fprintf(stderr, "lsh: expected argument to \"cd\"\n");
    }
    else
    {
        if (chdir(args[1]) != 0)
        {
            perror("lsh");
        }
    }
    return 1;
}

int lsh_exit()
{
    return 0;
}

int *split_pipe(char **args)
{
    int *command_positions;
    int position = 0;
    int num_args = 64;
    command_positions = malloc(num_args * sizeof(int));
    if (!command_positions)
    {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    command_positions[0] = 0;
    position++;

    for (int i = 0; args[i] != NULL; i++)
    {
        if (!strcmp(args[i], "|"))
        {
            args[i] = NULL;
            command_positions[position] = i + 1;
            position++;
            if (position >= num_args)
            {
                num_args += 64;
                command_positions = realloc(command_positions, num_args * sizeof(int));
                if (!command_positions)
                {
                    fprintf(stderr, "lsh: allocation error\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    command_positions[position] = -1;

    return command_positions;
}

void lsh_wait()
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}