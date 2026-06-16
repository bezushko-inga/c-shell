#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/file.h>

int exit_status;

// structure that stores input/output files and process parameters

struct process {

    char* input;
    char* output;
    char mode;

    char** argv;
    int argc;
    char* branched;

};

void cd(char* name) {
    if (chdir(name) != 0) {
        perror("\033[31mcd error\033[0m");
        exit_status = 1;
    } else {
        exit_status = 0;
    }
}

void add_process(char* first_char, struct process* proc) {
    char* cur;
    char* fbegin;

    // process input redirection
    cur = first_char;

    // check whether input redirection exists
    while (*cur != '\0' && *cur != '<') cur += 1;

    if (*cur == '\0') proc->input = NULL; // no input redirection
    else {
        *cur = ' ';
        while (*cur == ' ') cur += 1;

        fbegin = cur;
        while (*cur != ' ' && *cur != '\0') cur += 1;
        char* input = (char*)malloc(cur - fbegin + 1);

        if (input == NULL) {
            perror("\033[31mmemory error\033[0m");
            exit_status = 1;
            return;
        }

        memcpy(input, fbegin, cur - fbegin);
        input[cur - fbegin] = '\0';

        while (fbegin != cur) {
            *fbegin = ' ';
            fbegin++;
        }

        proc->input = input;
    }

    // process output redirection
    cur = first_char;
    char mode;

    // check whether output redirection exists
    while (*cur != '\0' && *cur != '>') cur += 1;

    if (*cur == '\0') proc->output = NULL; // no output redirection
    else {
        *cur = ' ';
        cur++;

        if (*cur == '>') { // no output redirection
            *cur = ' ';
            mode = 'a';
        } else mode = 'w';

        while (*cur == ' ') cur += 1;

        fbegin = cur;
        while (*cur != ' ' && *cur != '\0') cur += 1;
        char* output = (char*)malloc(cur - fbegin + 1);

        if (output == NULL) {
            perror("\033[31mmemory error\033[0m");
            exit_status = 1;
            return;
        }

        memcpy(output, fbegin, cur - fbegin);
        output[cur - fbegin] = '\0';

        while (fbegin != cur) {
            *fbegin = ' ';
            fbegin++;
        }

        proc->output = output;
        proc->mode = mode;

    }

    // parse command arguments

    if (strchr(first_char, '(')) {
        proc -> argv = NULL;
        proc -> branched = strdup(first_char);
        return;
    }

    cur = first_char;
    int arg_count = 0;
    // count arguments
    while (1) {
        while (*cur == ' ') cur += 1;
        if (*cur == '\0') break;
        while (*cur != ' ' && *cur != '\0') cur += 1;
        arg_count += 1;
    }

    char** argv = (char**)malloc(sizeof(char*) * (arg_count + 1));

    if (argv == NULL) {
        perror("\033[31mmemory error\033[0m");
        exit_status = 1;
        return;
    }

    argv[arg_count] = NULL; // required for execvp

    cur = first_char;

    // fill argv array

    for (int i = 0; i < arg_count; i++) {
        while (*cur == ' ') cur += 1;
        char* argv0 = cur;
        while (*cur != ' ' && *cur != '\0') cur += 1;

        char tmp = *cur;
        *cur = '\0';
        argv[i] = strdup(argv0);
        *cur = tmp;
        cur += 1;
    }

    proc->argv = argv;
    proc->branched = NULL;
    proc->argc = arg_count;
}

void redirection(int i, struct process *proc, int in_dir, int out_dir, int argc) {
    int fd;

    if (i != 0) dup2(in_dir, 0);

    if (proc->input) {
        fd = open(proc->input, O_RDONLY);
        if (fd == -1) {
            fd = open("/dev/null", O_RDONLY);
            dup2(fd, 0);
            close(fd);
            return;
        }
        dup2(fd, 0);
        close(fd);
    }

    if (i != argc - 1) dup2(out_dir, 1);

    if (proc->output) {
        if (proc->mode == 'w') fd = open(proc->output, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        else fd = open(proc->output, O_WRONLY | O_CREAT | O_APPEND, 0666);;

        if (fd == -1) {
            fd = open("/dev/null", O_WRONLY);
            dup2(fd, 1);
            close(fd);
            return;
        }
        dup2(fd, 1);
        close(fd);
    }
}

void first_level(char* line);
void third_level(char* line) { // third precedence level: pipelines and redirections
    // split the command line by |, >, >>, <

    // count the number of processes in the pipeline
    int cmd_count = 1;
    char* cur = line;
    int brackets = 0;

    while (*cur != '\0') {
        if (*cur == '(') brackets += 1;
        if (*cur == ')') brackets -= 1;

        // count only pipeline elements outside parentheses
        if (brackets == 0 && *cur == '|') cmd_count += 1;
        cur += 1;
    }

    if (cmd_count == 1) { // single command (no pipeline)
        char* cur1 = line;
        while (*cur1 == ' ') cur1 += 1;

        if (*cur1 == 'c') {
            cur1 += 1;
            if (*cur1 == 'd') {
                cur1 += 1;
                if (*cur1 == ' ') {
                    while (*cur1 == ' ') cur1 += 1;
                    char* name = cur1;
                    while (*cur1 != ' ' && *cur1 != '\0') cur1 += 1;
                    char tmp = *cur1;
                    *cur1 = '\0';
                    name = strdup(name);
                    cd(name);
                    free(name);
                    *cur1 = tmp;
                    return;
                }
            }
        }
    }
        // process each command in the pipeline

        struct process processes[cmd_count];
        char* first = line;
        cur = line;
        brackets = 0;

        int i = 0;
        while (i < cmd_count) {
            if (*cur == '(') brackets += 1;
            if (*cur == ')') brackets -= 1;

            if (brackets == 0 && (*cur == '|' || *cur == '\0')) {
                char tmp = *cur;
                *cur = '\0';
                add_process(first, &processes[i]);
                *cur = tmp;

                cur += 1;
                first = cur;
                i++;
            }
            cur += 1;
        }

        int fd[2], lastfd = -1;
        pid_t pids[cmd_count];

        for (int i = 0; i < cmd_count; i++) {
            if (i < cmd_count - 1) {
                if (pipe(fd) == -1) {
                    perror("\033[31mpipe error\033[0m");
                    exit_status = 1;
                    return;
                }
            }

            if ((pids[i] = fork()) < 0) {
                perror("\033[31mpid error\033[0m");
                exit_status = 1;
                return;
            } else if (pids[i] == 0) {
                // apply input/output redirections
                redirection(i, &processes[i], lastfd, fd[1], cmd_count);

                if (processes[i].branched) {
                    cur = processes[i].branched;
                    while (*cur == ' ') cur += 1;
                    if (*cur == '(') {
                        char* in_brackets = cur + 1;
                        while (*cur != '\0') cur += 1;
                        while (*cur != ')') cur -= 1;
                        *cur = '\0';
                        first_level(in_brackets);
                        exit(exit_status);
                    }
                    exit(1);
                }

                execvp(processes[i].argv[0], processes[i].argv);
                perror("\033[31mexecuting error\033[0m");
                exit_status = 1;
                exit(1);
                return;
            }
            if (cmd_count > 1) {
                if (i == 0) {
                    lastfd = fd[0];
                    close(fd[1]);
                }
                else if (i == cmd_count - 1) close(lastfd);
                else {
                    close(lastfd);
                    lastfd = fd[0];
                    close(fd[1]);
                }
            }
            if (processes[i].branched) free(processes[i].branched);
            free(processes[i].input);
            free(processes[i].output);
            if (processes[i].argv) {
                for (int j = 0; j < processes[i].argc; j++)
                    free(processes[i].argv[j]);
                free(processes[i].argv);
            }
        }

    for (int i = 0; i < cmd_count; i++) {
        int status;
        waitpid(pids[i], &status, 0);

        if (i == cmd_count - 1) {
            if (WIFEXITED(status))
                exit_status = WEXITSTATUS(status);
            else
                exit_status = 1;
        }
    }
}

void second_level(char* line) { // second precedence level: && and ||
    // split the command line by && and ||

    char* cur = line;
    char* newline;
    char separator;
    int brackets = 0;

    while (*cur != '\0') {
        newline = cur;

        while (*cur == ' ') cur += 1;
        if (*cur == '\0') break;

        separator = '\0';
        while (*cur != '\0') {

            if (*cur == '(') brackets += 1;
            if (*cur == ')') brackets -= 1;

            if (brackets == 0) { // all parentheses are currently closed
                if (*cur == '|' && *(cur + 1) == '|') {
                    separator = '|';
                    break;
                }
                if (*cur == '&' && *(cur + 1) == '&') {
                    separator = '&';
                    break;
                }
            }
            cur += 1;
        }
        *cur = '\0';

        third_level(newline);

        if (separator == '\0') break;
        if (exit_status != 0 && separator == '&') break;
        if (exit_status == 0 && separator == '|') break;
        *cur = separator;
        cur += 2;
    }
}

void first_level(char* line) { // first precedence level: ; and &
    // split the command line by ; and &

    char *cur = line;
    char *newline;
    char separator;
    int brackets = 0;

    while (*cur != '\0') {
        newline = cur;
        char* first = cur;

        while (*cur == ' ') cur += 1;
        if (*cur == '\0') break;

        separator = '\0';
        while (*cur != '\0') {

            if (*cur == '(') brackets += 1;
            if (*cur == ')') brackets -= 1;

            if (brackets == 0) { // all parentheses are currently closed

                if (*cur == ';') {
                    separator = ';';
                    break;
                }

                if (*cur == '&') {
                    if (*(cur + 1) != '&') {
                        separator = '&';
                        break;
                    }
                    if (cur != first) {
                        if (*(cur - 1) != '&') {
                            separator = '&';
                            break;
                        }
                    }
                }

            }
            cur += 1;
        }
        *cur = '\0';

        if (separator == '&') { // background execution
            pid_t pid, pid1;
            int fd;

            if ((pid1 = fork()) < 0) {
                perror("\033[31mpid error\033[0m");
                exit_status = 1;
                return;
            }
            else if (pid1 == 0) { // create child process
                if ((pid = fork()) < 0) {
                    perror("\033[31mpid error\033[0m");
                    exit_status = 1;
                    return;
                }
                else if (pid == 0) { // create grandchild process
                    signal(SIGINT, SIG_IGN); // ignore SIGINT
                    fd = open("/dev/null", O_RDONLY); // redirect stdin to /dev/null
                    dup2(fd, 0);
                    close(fd);
                    second_level(newline); // proceed to the second precedence level
                    exit(exit_status);
                }
                exit(0);
            }
            waitpid(pid1, NULL, 0); // wait to avoid a zombie process
        } else second_level(newline); // sequential execution or end of command line
        // proceed to the second precedence level

        if (separator == '\0') break;
        *cur = separator;
        cur += 1;
    }
}

int main() {

    char cur_path[1024];
    char symbol;
    char* line;

    while (1) {

        exit_status = 0;
        getcwd(cur_path, sizeof(cur_path));
        printf("\033[96m%s>\033[0m", cur_path);

        // read command line

        int line_len = 0;
        line = malloc(1);
        if (line == NULL) {
            perror("\033[31mmemory error\033[0m");
            exit_status = 1;
        }

        symbol = getchar();

        while (symbol != '\n' && symbol != EOF) {
            char* tmp = realloc(line, line_len + 2);
            if (tmp == NULL) {
                perror("\033[31mmemory error\033[0m");
                free(line);
                exit(1);
            }
            line = tmp;
            line[line_len++] = symbol;
            symbol = getchar();
        }
        char* tmp = realloc(line, line_len + 2);
        if (tmp == NULL) {
            perror("\033[31mmemory error\033[0m");
            free(line);
            exit(1);
        }
        line = tmp;
        line[line_len] = '\0';

        // exit shell

        if ((symbol == EOF) || (strcmp(line, "exit") == 0)) {
            if (symbol == EOF) printf("\n");
            printf("logout\n");
            free(line);
            return 0;
        }

        // proceed to the first precedence level

        first_level(line);
        free(line);
    }
}
