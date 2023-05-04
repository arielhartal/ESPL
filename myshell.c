#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/wait.h>
#include "LineParser.h"
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>


#define BUFFER_SIZE 2048

typedef struct process {
    cmdLine *cmd;
    pid_t pid;
    int status;
    struct process *next;
} process;

#define TERMINATED -1
#define RUNNING 1
#define SUSPENDED 0
#define HISTLEN 20

char *history[HISTLEN];
int newest = -1;
int oldest = -1;
int history_counter = 0;

void printHistory() {
    int i;
    for (i = 0; i < history_counter; i++) {
        if (history[i][0] != '\0') {
            printf("%d: %s\n", i+1, history[i]);
        }
    }
}

void addHistory(char *command) {
    // Allocate space for the new command
    char *newCommand = malloc(strlen(command) + 1);
    strcpy(newCommand, command);

    // Update the newest index
    newest = (newest + 1) % HISTLEN;

    // If the history array is full, remove the oldest command
    if (oldest == newest) {
        free(history[oldest]);
        oldest = (oldest + 1) % HISTLEN;
    }

    // Add the new command to the history array
    history[newest] = newCommand;
}



void addProcess(process **process_list, cmdLine *cmd, pid_t pid) {

    

    process *new_process = malloc(sizeof(process));
    new_process->cmd = cmd;
    new_process->pid = pid;
    new_process->status = RUNNING;
    new_process->next = *process_list;
    *process_list = new_process;
}

void freeProcessList(process* process_list) {
    process *current = process_list;
    process *next;

    while (current != NULL) {
        next = current->next;
        free(current->cmd);
        free(current);
        current = next;
    }
}

void updateProcessStatus(process* process_list, int pid, int status) {
    process *current = process_list;
    process *previous = NULL;
    while (current != NULL) {
    //    printf("%d\n", current->status);
        if (current->pid == pid) {
            if (status == -1) {
                if (previous != NULL) {
                    previous->next = current->next;
                } else {
                    // This means current is the first element in the list
                    process_list = current->next;
                }
            }
            current->status = status;
            break;
        }
        previous = current;
        current = current->next;
    }
}

void updateProcessList(process **process_list) {
    process *proc = *process_list;
    int status;
    int pid;

    while (proc != NULL) {
        pid = waitpid(proc->pid, &status, WNOHANG | WUNTRACED | WCONTINUED);
        if (pid >= -1) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                // The process has terminated
                updateProcessStatus(*process_list, proc->pid, TERMINATED);
          //      printf("here");
            } else if (WIFSTOPPED(status)) {
                // The process has been stopped
                updateProcessStatus(*process_list, proc->pid, SUSPENDED);
            } else if (WIFCONTINUED(status)) {
                // The process has been resumed
                updateProcessStatus(*process_list, proc->pid, RUNNING);
                printf("here RUNNING");
            }
            else
            {
                printf("here nothing");
            }
            
        }
        proc = proc->next;
    }
}



void printProcessList(process **process_list) {

    // Update process list
    updateProcessList(process_list);


    if (*process_list != NULL) {
        printf("PID\tStatus\t\tCommand\n");
    }

    
    process *current = *process_list;
    process *prev = NULL;
    int index = 1;

    while (current != NULL) {
        // Skip the current shell process
        if (current->pid == getpid()) {
            prev = current;
            current = current->next;
            continue;
        }

        printf("%d\t", current->pid);

        if (current->status == RUNNING) {
            printf("Running\t\t");
        } else if (current->status == SUSPENDED) {
            printf("Suspended\t");
        } else if (current->status == TERMINATED) {
            printf("Terminated\t");
        }

        for (int i = 0; i < current->cmd->argCount; i++) {
            printf("%s ", current->cmd->arguments[i]);
        }
        printf("\n");

        index++;
        prev = current;
        current = current->next;
    }


}



void displayPrompt() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, PATH_MAX) != NULL) {
        printf("%s$ ", cwd);
    }
    else {
        perror("Error getting current working directory");
    }
}


void suspendCmd(char* pidStr, process **process_list) {
    pid_t pid = atoi(pidStr);
    int res = kill(pid, SIGTSTP);
    if (res != 0) {
        perror("kill failed");
    }
    updateProcessStatus(*process_list, pid, SUSPENDED);
}

void killCmd(char* pidStr, process** process_list) {
    pid_t pid = atoi(pidStr);
    int res = kill(pid, SIGINT);
    int status;
    waitpid(pid, &status, 0);
    if (res != 0) {
        perror("kill failed");
    } else {
        updateProcessStatus(*process_list, pid, TERMINATED);
    }
}

void wakeCmd(char* pidStr, process** process_list) {
    pid_t pid = atoi(pidStr);
    int res = kill(pid, SIGCONT);
    if (res != 0) {
        perror("kill failed");
    } else {
        updateProcessStatus(*process_list, pid, RUNNING);
    }
}



void print_fd_flags(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        perror("fcntl");
        return;
    }
    printf("File descriptor %d flags: ", fd);
    if (flags & O_RDONLY) printf("O_RDONLY ");
    if (flags & O_WRONLY) printf("O_WRONLY ");
    if (flags & O_RDWR) printf("O_RDWR ");
    printf("\n");
}



void execute(cmdLine *pCmdLine, process** process_list) {


    
    int is_pipeline = pCmdLine->next != NULL;

    if (is_pipeline) {
        if (pCmdLine->outputRedirect != NULL) {
            fprintf(stderr, "Error: Invalid output redirection for left-hand side process.\n");
            return;
        }

        if (pCmdLine->next->inputRedirect != NULL) {
            fprintf(stderr, "Error: Invalid input redirection for right-hand side process.\n");
            return;
        }
    }

    if (strcmp(pCmdLine->arguments[0], "cd") == 0) {
        if (chdir(pCmdLine->arguments[1]) != 0) {
            fprintf(stderr, "Error changing directory: %s\n", strerror(errno));
        }
    }
    else if (strcmp(pCmdLine->arguments[0], "suspend") == 0) {
        char* pid = pCmdLine->arguments[1];
        suspendCmd(pid, process_list);
    }
    else if (strcmp(pCmdLine->arguments[0], "wake") == 0) {
        char* pid = pCmdLine->arguments[1];
        wakeCmd(pid, process_list);
    }
    else if (strcmp(pCmdLine->arguments[0], "kill") == 0) {
        char* pid = pCmdLine->arguments[1];
        killCmd(pid, process_list);
    }
    else {
        int pipefd[2];
        int is_pipeline = pCmdLine->next != NULL;

        if (is_pipeline) {
            if (pipe(pipefd) == -1) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }
        pid_t pid = fork();
        
        if (pid == -1) {
            perror("Error forking");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0) {
            // Child process
            if (pCmdLine->inputRedirect != NULL) {
                int fd = open(pCmdLine->inputRedirect, O_RDONLY);
                if (fd == -1) {
                    perror("Error opening input file");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDIN_FILENO) == -1) {
                    perror("Error redirecting input");
                    exit(EXIT_FAILURE);
                }
                close(fd);

            
            }
            if (pCmdLine->outputRedirect != NULL) {
                int fd = open(pCmdLine->outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd == -1) {
                    perror("Error opening output file");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    perror("Error redirecting output");
                    exit(EXIT_FAILURE);
                }
                close(fd);
            }

            if (is_pipeline) {
                close(STDOUT_FILENO);
                dup(pipefd[1]);
                close(pipefd[1]);
                close(pipefd[0]);
                
            }
            if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1) {
                perror("Error executing command");
                _exit(EXIT_FAILURE);
            }
        }
        else {
            // Parent process
         //   printf("%s\n", pCmdLine->arguments[0]);
          //  printf("%d\n", pid);
            
            addProcess(process_list, pCmdLine, pid);
           // printf("%d\n", process_list[0]->status);
            if (is_pipeline) {
                close(pipefd[1]);
                pid_t pid2 = fork();
                if (pid2 == -1) {
                    perror("Error forking");
                    exit(EXIT_FAILURE);
                }
                else if (pid2 == 0) {
                    // Second child process
                    close(STDIN_FILENO);
                    dup(pipefd[0]);
                    close(pipefd[0]);
                    close(pipefd[1]);
                  

                if (is_pipeline) {
                    if (pCmdLine->next->inputRedirect != NULL) {
                        int fd = open(pCmdLine->next->inputRedirect, O_RDONLY);
                        if (fd == -1) {
                            perror("Error opening input file");
                            exit(EXIT_FAILURE);
                        }
                        if (dup2(fd, STDIN_FILENO) == -1) {
                            perror("Error redirecting input");
                            exit(EXIT_FAILURE);
                        }
                        close(fd);
                    }
                    if (pCmdLine->next->outputRedirect != NULL) {
                        int fd = open(pCmdLine->next->outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                        if (fd == -1) {
                            perror("Error opening output file");
                            exit(EXIT_FAILURE);
                        }
                        if (dup2(fd, STDOUT_FILENO) == -1) {
                            perror("Error redirecting output");
                            exit(EXIT_FAILURE);
                        }
                        close(fd);
                    }
                }


                    if (execvp(pCmdLine->next->arguments[0], pCmdLine->next->arguments) == -1) {
                        perror("Error executing command");
                        _exit(EXIT_FAILURE);
                    }
                }
                else {
                    close(pipefd[0]);
                }

                waitpid(pid2, NULL, 0);
            }

            if (pCmdLine->blocking) {
                int status;
                if (waitpid(pid, &status, 0) == -1) {
                    perror("Error waiting for child process");
                }
            }
        }
    }
}




int main(int argc, char **argv) {
    char buffer[BUFFER_SIZE];
    cmdLine *cmd;
    int quit = 0;

    int debug_mode = -1;
    for(int i = 1; i < argc; i++)
    {
        if(argv[i][0] == '-' && argv[i][1] == 'd' && argv[i][2] == '\0')
        {
            debug_mode = 1;
        }


    }
    memset(history, 0, sizeof(char*) * HISTLEN);
    process *process_list = NULL;
    while (!quit) {
        displayPrompt();
        fgets(buffer, BUFFER_SIZE, stdin);
        cmd = parseCmdLines(buffer);
        
    
        addHistory(cmd->arguments[0]);
        history_counter++;
        if (strcmp(cmd->arguments[0], "quit") == 0) {
            quit = 1;
            freeCmdLines(cmd);
            exit(EXIT_SUCCESS);
        }
        else if (strcmp(cmd->arguments[0], "procs") == 0) {
            
            printProcessList(&process_list);
            //freeCmdLines(cmd);
        }

        else if (strcmp(cmd->arguments[0], "history") == 0) {
            printHistory();
        }

        else if (strcmp(cmd->arguments[0], "!!\n") == 0) {
            if (history[0][0] != '\0') {
              //  strcpy(cmd, history[0]);
                execute(cmd, &process_list);
            } else {
                printf("No previous command in history.\n");
            }
        } else if (strcmp(cmd->arguments[0], "!") == 0 && isdigit(cmd->arguments[1])) {
            int index = atoi(cmd->arguments[1]) - 1;
            if (index >= 0 && index < HISTLEN && history[index][0] != '\0') {
               // strcpy(cmd, history[index]);
                execute(cmd, &process_list);
            } else {
                printf("Invalid history index.\n");
            }
        }

       
        else {
            int pid = fork();
            if (pid == -1) {
                perror("Error forking");
            }
            else if (pid == 0) {
                if(debug_mode == 1)
                {
                    fprintf(stderr, "PID: %d\n", getpid());
                    fprintf(stderr, "Command: %s\n", cmd->arguments[0]);
                }
                execute(cmd, &process_list);
                exit(EXIT_SUCCESS);
            }
            else {
               // if (strcmp(cmd->arguments[0], "procs") == 0) {
                    addProcess(&process_list, cmd, pid);
               // }
                if(cmd->blocking)
                {               
                    wait(NULL);
                }
            }

        }
        
      //  freeCmdLines(cmd);
    }

    return 0;
}



