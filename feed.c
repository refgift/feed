#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#define BUFFER_SIZE (2 * 1024 * 1024)  // 2 MB - more than enough for any chat response
static void print_unescaped(const char *s, char stop) {
    printf("    ");  // 4-char left margin
    // Skip leading spaces on first line
    while (*s && isspace((unsigned char)*s) && *s != '\n') s++;
    int cols=0;
    while (*s && *s != stop) {
        if (*s == '\\') {
            ++s;
            if (!*s) break;
            switch (*s) {
                case 'n':  (void)putchar('\n'); (void)printf("    ");
                            // Skip leading spaces on new line
                            while (*s && isspace((unsigned char)*s) && *s != '\n' && *s != stop) s++;
                            break;
                case 'r':  (void)putchar('\r'); break;
                case 't':  (void)putchar('\t'); break;
                case 'b':  (void)putchar('\b'); break;
                case 'f':  (void)putchar('\f'); break;
                case '"':  (void)putchar('"');  break;
                case '\\': (void)putchar('\\'); break;
                case '/':  (void)putchar('/');  break;
                // \uXXXX is ignored for simplicity (most text is ASCII/UTF-8)
                default:   (void)putchar(*s);
            }
            ++s;
        } else {
            (void)putchar(*s);
            if (*s == '\n') (void)printf("    ");
            ++s;
        }
       	// line folding
        if (cols++ > 72 && isblank(*s)) {
        	          (void)putchar(10);
                      (void)printf("    ");
                      // Skip leading spaces on folded line
                      while (*s && isspace((unsigned char)*s) && *s != '\n' && *s != stop) s++;
                   cols=0;
        }
    }
}
void print_folded(const char *text, int width) {
    const char *p = text;
    while (*p) {
        (void)printf("    ");  // 4-char left margin
        // Skip leading spaces on this line
        while (*p && isspace((unsigned char)*p) && *p != '\n') p++;
        const char *line_start = p;
        int col = 0;
        const char *last_space = NULL;
        while (*p && *p != '\n' && col < width) {
            if (isspace((unsigned char)*p)) last_space = p;
            col++;
            p++;
        }
        if (!*p || *p == '\n') {
            // end of text or newline, print the rest
            (void)fwrite(line_start, 1, p - line_start, stdout);
            if (*p == '\n') {
                (void)putchar('\n');
                (void)printf("    ");  // indent next line
            }
            if (*p) p++;
        } else if (last_space) {
            // fold at last_space
            (void)fwrite(line_start, 1, last_space - line_start + 1, stdout);
            (void)putchar('\n');
            (void)printf("    ");  // indent next line
            p = last_space + 1;
        } else {
            // no space, print up to width
            (void)fwrite(line_start, 1, width, stdout);
            (void)putchar('\n');
            (void)printf("    ");  // indent next line
            p = line_start + width;
        }
    }
}
static char * key;
static char * value;
static char api_key[1024];
static char api_model[1024];
static char api_user[1024];
int main(int argc, char **argv, char**expv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s \"your prompt here\"\n", argv[0]);
        return 1;
    }
    for (int i=0; expv[i]!=0; i++) {
	char *env_copy = strdup(expv[i]);
	if (!env_copy) {
		fprintf(stderr, "Memory allocation failed\n");
		exit(EXIT_FAILURE);
	}
	key = strtok(env_copy,"=");
	value = strtok(NULL,"=");
	//printf("%s %s\n",key, value);
	if (key && strcmp(key,"FEED_KEY")==0) {
		if (value && strlen(value) < sizeof(api_key)) {
			strncpy(api_key, value, sizeof(api_key) - 1);
			api_key[sizeof(api_key) - 1] = '\0';
		} else {
			fprintf(stderr, "FEED_KEY value too long or null\n");
			free(env_copy);
			exit(EXIT_FAILURE);
		}
	}
	if (key && strcmp(key,"FEED_MODEL")==0) {
		if (value && strlen(value) < sizeof(api_model)) {
			strncpy(api_model, value, sizeof(api_model) - 1);
			api_model[sizeof(api_model) - 1] = '\0';
		} else {
			fprintf(stderr, "FEED_MODEL value too long or null\n");
			free(env_copy);
			exit(EXIT_FAILURE);
		}
	}
	if (key && strcmp(key,"FEED_USER")==0) {
		if (value && strlen(value) < sizeof(api_user)) {
			strncpy(api_user, value, sizeof(api_user) - 1);
			api_user[sizeof(api_user) - 1] = '\0';
		} else {
			fprintf(stderr, "FEED_USER value too long or null\n");
			free(env_copy);
			exit(EXIT_FAILURE);
		}
	}
	free(env_copy);
    }
    if (strlen(api_key)==0) {
	fprintf(stderr, "No FEED_KEY exported\n");
        exit(EXIT_FAILURE);
    }
    if (strlen(api_model)==0) {
	fprintf(stderr, "No FEED_MODEL exported\n");
        exit(EXIT_FAILURE);
    }
    if (strlen(api_user)==0) {
	fprintf(stderr, "No FEED_USER exported\n");
        exit(EXIT_FAILURE);
    }
    // === Escape user input for safe JSON ===
    char escaped[BUFFER_SIZE / 4];
    size_t j = 0;
    
    for (const char *p = argv[1]; *p && j < sizeof(escaped) - 10; ++p) {
        switch (*p) {
            case '"':  escaped[j++] = '\\'; escaped[j++] = '"'; break;
            case '\\': escaped[j++] = '\\'; escaped[j++] = '\\'; break;
            case '\n': escaped[j++] = '\\'; escaped[j++] = 'n'; break;
            case '\r': escaped[j++] = '\\'; escaped[j++] = 'r'; break;
            case '\t': escaped[j++] = '\\'; escaped[j++] = 't'; break;
            default:   escaped[j++] = *p;
        }
    }
    escaped[j] = '\0';
    // Send the response
    // === Build curl args ===
    char auth_header[2048];
    int written = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    if (written >= sizeof(auth_header)) {
        fprintf(stderr, "Auth header too long\n");
        return 1;
    }
    char json_data[BUFFER_SIZE];
    written = snprintf(json_data, sizeof(json_data),
        "{\"model\":\"%s\",\"messages\":["
        "{\"role\":\"system\",\"content\":\"the system coding rule is no contexts.\"},"
        "{\"role\":\"user\",\"name\":\"%s\",\"content\":\"%s\"}],"
        "\"temperature\":0.7,\"max_tokens\":4096}",
        api_model, api_user, escaped);
    if (written >= sizeof(json_data)) {
        fprintf(stderr, "JSON data too long\n");
        return 1;
    }
    char *args[] = {"curl", "-s", "--max-time", "3600", "https://api.x.ai/v1/chat/completions", "-H", "Content-Type: application/json", "-H", auth_header, "-d", json_data, NULL};
    // Create pipe and fork
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        return 1;
    }
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return 1;
    }
    FILE *fp = NULL;
    if (pid == 0) {
        // child
        (void)close(pipefd[0]);
        (void)dup2(pipefd[1], STDOUT_FILENO);
        (void)close(pipefd[1]);
        execvp("curl", args);
        perror("execvp failed");
        exit(EXIT_FAILURE);
    } else {
        // parent
        (void)close(pipefd[1]);
        fp = fdopen(pipefd[0], "r");
        if (!fp) {
            perror("fdopen failed");
            return 1;
        }
    }

    // Print the prompt
    (void)printf("\x1b[2J\x1b[H\x1b[34m");
    print_folded(argv[1], 72);
    (void)printf("\x1b[0m\n\n");

    // Read the Response
    // === Read entire JSON response into memory (most robust approach) ===
    char *response = malloc(BUFFER_SIZE);
    if (!response) {
        fprintf(stderr, "Memory allocation failed\n");
        if (fp) pclose(fp);
        return 1;
    }
    size_t len = fread(response, 1, BUFFER_SIZE - 1, fp);
    response[len] = '\0';
    (void)pclose(fp);
    int status;
    (void)waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "curl failed\n");
        free(response);
        return 1;
    }
    // === Find the assistant message content (context-aware) ===
    // First locate the message object to avoid any other "content" fields
    char *msg_start = strstr(response, "\"message\"");
    if (!msg_start) {
        msg_start = response;  // fallback
    }
    char *content_start = strstr(msg_start, "\"content\"");
    if (!content_start) {
        // Check for error response
        if (strstr(response, "\"error\"")) {
            printf("API returned an error.\n");
        } else {
            printf("No content in response.\n");
        }
        free(response);
        return 1;
    }
    // Find the colon after "content"
    char *colon = strchr(content_start + 9, ':');
    if (!colon) {
        printf("Malformed JSON.\n");
        free(response);
        return 1;
    }
    // Skip whitespace
    char *p = colon + 1;
    while (*p && isspace((unsigned char)*p)) ++p;
    // Handle null content (refusals, rate limits, etc.)
    if (strncmp(p, "null", 4) == 0) {
        printf("Response was empty (null content).\n");
        free(response);
        return 0;
    }
    // Must be a string
    if (*p != '"') {
        printf("Unexpected value type for content.\n");
        free(response);
        return 1;
    }
    ++p;  // skip opening quote
    // === Print with proper unescaping until closing quote ===
    char unescaped[BUFFER_SIZE];
    int idx = 0;
    const char *s = p;
    while (*s && *s != '"') {
        if (*s == '\\') {
            ++s;
            if (!*s) break;
            switch (*s) {
                case 'n': unescaped[idx++] = '\n'; break;
                case 'r': unescaped[idx++] = '\r'; break;
                case 't': unescaped[idx++] = '\t'; break;
                case 'b': unescaped[idx++] = '\b'; break;
                case 'f': unescaped[idx++] = '\f'; break;
                case '"': unescaped[idx++] = '"'; break;
                case '\\': unescaped[idx++] = '\\'; break;
                case '/': unescaped[idx++] = '/'; break;
                default: unescaped[idx++] = *s;
            }
            ++s;
        } else {
            unescaped[idx++] = *s;
            ++s;
        }
    }
    unescaped[idx] = '\0';
    print_folded(unescaped, 72);
    ++p;  // skip closing quote
    putchar('\n');
    free(response);
    return 0;
}
