#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_COMMAND_LENGTH 1024
#define MAX_ARGUMENTS 64

// Function to parse the input command
void parseCommand(char* command, char** arguments, int* numArgs) {
    char* token;
    int i = 0;
    int inQuotes = 0; // Flag to track if inside quotes
    int escape = 0; // Flag to track if escape character encountered

    while ((token = strtok_r(command, " \t\n", &command))) {
        int len = strlen(token);

        // Check for quotes at the beginning and end of the token
        if ((token[0] == '"' && token[len - 1] == '"') ||
            (token[0] == '\'' && token[len - 1] == '\'')) {
            token[len - 1] = '\0'; // Remove the closing quote
            token++; // Move past the opening quote
        }

        // Process nested quotes
        if (token[0] == '"' && token[len - 1] != '"') {
            // Nested double quotes, concatenate tokens until closing quote is found
            char* nestedToken;
            while ((nestedToken = strtok_r(NULL, "\"", &command))) {
                int nestedLen = strlen(nestedToken);
                if (nestedToken[nestedLen - 1] == '"') {
                    nestedToken[nestedLen - 1] = '\0'; // Remove the closing quote
                    break;
                }
                len += nestedLen;
                token = realloc(token, (len + 1) * sizeof(char));
                strcat(token, " ");
                strcat(token, nestedToken);
            }
        } else if (token[0] == '\'' && token[len - 1] != '\'') {
            // Nested single quotes, concatenate tokens until closing quote is found
            char* nestedToken;
            while ((nestedToken = strtok_r(NULL, "'", &command))) {
                int nestedLen = strlen(nestedToken);
                if (nestedToken[nestedLen - 1] == '\'') {
                    nestedToken[nestedLen - 1] = '\0'; // Remove the closing quote
                    break;
                }
                len += nestedLen;
                token = realloc(token, (len + 1) * sizeof(char));
                strcat(token, " ");
                strcat(token, nestedToken);
            }
        }

        // Process escape characters
        char* arg = malloc((len + 1) * sizeof(char));
        int j = 0;

        for (int k = 0; k < len; k++) {
            if (escape) {
                // Handle escape character
                if (token[k] == 'n')
                    arg[j++] = '\n';
                else if (token[k] == 't')
                    arg[j++] = '\t';
                else
                    arg[j++] = token[k];

                escape = 0; // Reset escape flag
            } else if (token[k] == '\\') {
                // Set escape flag for next character
                escape = 1;
            } else {
                arg[j++] = token[k];
            }
        }

        arg[j] = '\0';
        arguments[i] = arg;
        i++;
    }

    arguments[i] = NULL;
    *numArgs = i;
}


// Function to expand variables in arguments
void expandVariables(char** arguments, int numArgs) {
    for (int i = 0; i < numArgs; i++) {
        char* argument = arguments[i];
        if (argument[0] == '$') {
            char* variable = getenv(argument + 1);
            if (variable != NULL) {
                arguments[i] = variable;
            }
        }
    }
}

// Function to execute a command
void executeCommand(char** arguments) {
    pid_t pid = fork();

    if (pid == 0) {
        // Child process
        execvp(arguments[0], arguments);
        perror("execvp"); // This will print an error if the command is not found
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // Error forking
        perror("fork");
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
    }
}

// Function to execute commands with pipes
void executeCommandWithPipes(char** arguments1, char** arguments2) {
    int pipefd[2];
    pid_t pid1, pid2;

    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    pid1 = fork();
    if (pid1 < 0) {
        perror("fork");
        return;
    }

    if (pid1 == 0) {
        // Child process 1 (writes to pipe)
        close(pipefd[0]); // Close unused read end
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to the write end of the pipe
        close(pipefd[1]); // Close the original write end of the pipe

        execvp(arguments1[0], arguments1);
        perror("execvp"); // This will print an error if the command is not found
        exit(EXIT_FAILURE);
    }

    pid2 = fork();
    if (pid2 < 0) {
        perror("fork");
        return;
    }

    if (pid2 == 0) {
        // Child process 2 (reads from pipe)
        close(pipefd[1]); // Close unused write end
        dup2(pipefd[0], STDIN_FILENO); // Redirect stdin to the read end of the pipe
        close(pipefd[0]); // Close the original read end of the pipe

        execvp(arguments2[0], arguments2);
        perror("execvp"); // This will print an error if the command is not found
        exit(EXIT_FAILURE);
    }
        // Parent process
    close(pipefd[0]); // Close unused read end
    close(pipefd[1]); // Close unused write end

    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

int main() {
    char command[MAX_COMMAND_LENGTH];
    char* arguments[MAX_ARGUMENTS];
    int numArgs;

    while (1) {
        printf(">> ");
        fgets(command, sizeof(command), stdin);

        // Remove trailing newline character
        command[strcspn(command, "\n")] = '\0';

        // Check for exit command
        if (strcmp(command, "exit") == 0) {
            break;
        }

        // Parse the command
        parseCommand(command, arguments, &numArgs);

        // Expand variables
        expandVariables(arguments, numArgs);

        // Check for redirection or pipes
        int i = 0;
        int redirection = 0;
        int pipeIndex = -1;

        while (arguments[i] != NULL) {
            if (strcmp(arguments[i], ">") == 0) {
                // Redirection
                arguments[i] = NULL; // Terminate arguments before '>'
                redirection = 1;
                break;
            } else if (strcmp(arguments[i], "|") == 0) {
                // Pipe
                arguments[i] = NULL; // Terminate arguments before '|'
                pipeIndex = i;
                break;
            }
            i++;
        }

        if (redirection) {
            // Redirection
            char* outputFile = arguments[i + 1];
            freopen(outputFile, "w", stdout); // Redirect stdout to file

            // Execute the command
            executeCommand(arguments);
        } else if (pipeIndex != -1) {
            // Pipe
            char** arguments1 = arguments;
            char** arguments2 = &arguments[pipeIndex + 1];

            // Execute the command with pipes
            executeCommandWithPipes(arguments1, arguments2);
        } else {
            // No redirection or pipes
            // Execute the command
            executeCommand(arguments);
        }
    }

    return 0;
}

