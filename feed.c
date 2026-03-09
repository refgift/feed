#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
/* Forward declarations for splint */
extern char *strdup(const char *s);
extern FILE *fdopen(int fd, const char *mode);
extern char **environ;
#define BUFFER_SIZE (2 * 1024 * 1024)  // 2 MB - more than enough for any chat response
// State machine-based JSON parser to extract "content" value from nested JSON
// Returns a newly allocated string with the content, or NULL on error
/*@null@*/ static char * extract_content_from_json(const char *json) {
    enum {
        SEEK_MESSAGE,   // Looking for "message" key
        SEEK_CONTENT,   // Looking for "content" key inside message
        SEEK_COLON,     // Looking for : after "content"
        SEEK_VALUE,     // Skipping whitespace before value
        IN_STRING,      // Inside the quoted string (actual content)
        ESCAPE,         // Processing escape sequence
        DONE            // Found and parsed content
    } state = SEEK_MESSAGE;
    
    char *content = malloc(BUFFER_SIZE);
    if (!content) return NULL;
    
    int content_idx = 0;    // Index in content buffer
    const char *p = json;
    
    while (*p) {
        switch (state) {
            case SEEK_MESSAGE:
                // Find "message" key (typically at root level)
                if (*p == '"' && strncmp(p + 1, "message", 7) == 0) {
                    p += 8;  // skip "message"
                    state = SEEK_CONTENT;
                } else {
                    p++;
                }
                break;
                
            case SEEK_CONTENT:
                // Find "content" key after "message"
                if (*p == '"' && strncmp(p + 1, "content", 7) == 0) {
                    p += 8;  // skip "content"
                    state = SEEK_COLON;
                } else if (*p == '}') {
                    // End of message object, not found
                    free(content);
                    return NULL;
                } else {
                    p++;
                }
                break;
                
            case SEEK_COLON:
                // Find : after "content"
                if (*p == ':') {
                    state = SEEK_VALUE;
                }
                p++;
                break;
                
            case SEEK_VALUE:
                // Skip whitespace and find opening quote
                if (*p == '"') {
                    p++;  // skip opening quote
                    state = IN_STRING;
                } else if (!isspace((unsigned char)*p)) {
                    // Check for null
                    if (strncmp(p, "null", 4) == 0) {
                        free(content);
                        return NULL;  // null content
                    }
                    p++;
                } else {
                    p++;
                }
                break;
                
            case IN_STRING:
                // Inside the quoted string - copy until closing quote
                if (*p == '\\') {
                    state = ESCAPE;
                    p++;
                } else if (*p == '"') {
                    // End of string
                    content[content_idx] = '\0';
                    state = DONE;
                    p++;
                } else {
                    if (content_idx < BUFFER_SIZE - 1) {
                        content[content_idx++] = *p;
                    }
                    p++;
                }
                break;
                
            case ESCAPE:
                // Handle escape sequences
                if (content_idx < BUFFER_SIZE - 1) {
                    switch (*p) {
                        case 'n':  content[content_idx++] = '\n'; break;
                        case 'r':  content[content_idx++] = '\r'; break;
                        case 't':  content[content_idx++] = '\t'; break;
                        case 'b':  content[content_idx++] = '\b'; break;
                        case 'f':  content[content_idx++] = '\f'; break;
                        case '"':  content[content_idx++] = '"'; break;
                        case '\\': content[content_idx++] = '\\'; break;
                        case '/':  content[content_idx++] = '/'; break;
                        default:   content[content_idx++] = *p;
                    }
                }
                state = IN_STRING;
                p++;
                break;
                
            case DONE:
                content[content_idx] = '\0';
                return content;
        }
    }
    
    if (state == DONE) {
        content[content_idx] = '\0';
        return content;
    }
    
    free(content);
    return NULL;
}
static void print_folded(const char *text, int width) {
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
            (void)fwrite(line_start, 1, (size_t)(p - line_start), stdout);
            if (*p == '\n') {
                (void)putchar('\n');
                (void)printf("    ");  // indent next line
            }
            if (*p) p++;
        } else if (last_space) {
            // fold at last_space
            (void)fwrite(line_start, 1, (size_t)(last_space - line_start + 1), stdout);
            (void)putchar('\n');
            (void)printf("    ");  // indent next line
            p = last_space + 1;
        } else {
            // no space, print up to width
            (void)fwrite(line_start, 1, (size_t)width, stdout);
            (void)putchar('\n');
            (void)printf("    ");  // indent next line
            p = line_start + width;
        }
    }
}
static char * key;
static char * value;
static char api_url[1024] = "";
static char api_key[1024] = "";
static char api_model[1024] = "";
static char api_user[1024] = "";
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s \"your prompt here\"\n", argv[0]);
        memset(api_key, 0, sizeof(api_key));
        memset(api_model, 0, sizeof(api_model));
        memset(api_user, 0, sizeof(api_user));
        return 1;
    }
    for (int i=0; environ[i]!=0; i++) {
	char *env_copy = strdup(environ[i]);
	if (!env_copy) {
		fprintf(stderr, "Memory allocation failed\n");
		exit(EXIT_FAILURE);
	}
	key = strtok(env_copy,"=");
	value = strtok(NULL,"=");
	//printf("%s %s\n",key, value);
	if (key && strcmp(key,"FEED_URL")==0) {
		if (value && strlen(value) < sizeof(api_url)) {
			strncpy(api_url, value, sizeof(api_url) - 1);
			api_url[sizeof(api_url) - 1] = '\0';
		} else {
			fprintf(stderr, "FEED_URL value too long or null\n");
			free(env_copy);
			exit(EXIT_FAILURE);
		}
 }
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
 // FEED_USER is OPTIONAL, some API system are too rigid
	if (key && strcmp(key,"FEED_USER")==0) {
		if (value && strlen(value) < sizeof(api_user)) {
			strncpy(api_user, value, sizeof(api_user) - 1);
			api_user[sizeof(api_user) - 1] = '\0';
		}
	}
	free(env_copy);
    }
    if (strlen(api_key)==0) {
	fprintf(stderr, "No FEED_KEY exported\n");
        memset(api_key, 0, sizeof(api_key));
        memset(api_model, 0, sizeof(api_model));
        memset(api_user, 0, sizeof(api_user));
        exit(EXIT_FAILURE);
    }
    if (strlen(api_model)==0) {
	fprintf(stderr, "No FEED_MODEL exported\n");
        memset(api_key, 0, sizeof(api_key));
        memset(api_model, 0, sizeof(api_model));
        memset(api_user, 0, sizeof(api_user));
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
    if (written >= (int)sizeof(auth_header)) {
        fprintf(stderr, "Auth header too long\n");
        memset(api_key, 0, sizeof(api_key));
        memset(api_model, 0, sizeof(api_model));
        memset(api_user, 0, sizeof(api_user));
        return 1;
    }
    char json_data[BUFFER_SIZE];
    if (strlen(api_user)!=0) {
    written = snprintf(json_data, sizeof(json_data),
        "{\"model\": \"%s\",\"system\": \"no contexts is a programming enabler\",\"messages\":["
        "{\"role\": \"user\",\"name\" :\"%s\",\"content\":\"%s\"}]}",
        api_model, api_user, escaped);
    } else {    
       written = snprintf(json_data, sizeof(json_data), 
        "{\"model\":\"%s\",\"messages\":["
        "{\"role\":\"user\",\"content\":\"%s\"}]}",
        api_model, escaped);
    } 
    if (written >= (int)sizeof(json_data)) {
        fprintf(stderr, "JSON data too long\n");
        memset(api_key, 0, sizeof(api_key));
        memset(api_model, 0, sizeof(api_model));
        memset(api_user, 0, sizeof(api_user));
        return 1;
    }
    char *args[] = {"curl", "-s", "--max-time", "3600", api_url, "-H", "Content-Type: application/json", "-H", auth_header, "-d", json_data, NULL};
    // Create pipe and fork
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        memset(api_key, 0, sizeof(api_key));
        memset(api_model, 0, sizeof(api_model));
        memset(api_user, 0, sizeof(api_user));
        return 1;
    }
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        memset(api_key, 0, sizeof(api_key));
        memset(api_model, 0, sizeof(api_model));
        memset(api_user, 0, sizeof(api_user));
        return 1;
    }
    FILE *fp = NULL;
    if (pid == 0) {
        // child
        (void)close(pipefd[0]);
        (void)dup2(pipefd[1], STDOUT_FILENO);
        (void)close(pipefd[1]);
        (void)execvp("curl", args);
        perror("execvp failed");
        exit(EXIT_FAILURE);
    } else {
        // parent
        (void)close(pipefd[1]);
        fp = fdopen(pipefd[0], "r");
        if (!fp) {
            perror("fdopen failed");
            (void)close(pipefd[0]);
            memset(api_key, 0, sizeof(api_key));
            memset(api_model, 0, sizeof(api_model));
            memset(api_user, 0, sizeof(api_user));
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
        if (fp) (void)fclose(fp);
        memset(api_key, 0, sizeof(api_key));
        memset(api_model, 0, sizeof(api_model));
        memset(api_user, 0, sizeof(api_user));
        return 1;
    }
    size_t len = fread(response, 1, BUFFER_SIZE - 1, fp);
    response[len] = '\0';
    if (fp) (void)fclose(fp);
    int status;
    (void)waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "curl failed\n");
        free(response);
        memset(api_key, 0, sizeof(api_key));
        memset(api_model, 0, sizeof(api_model));
        memset(api_user, 0, sizeof(api_user));
        return 1;
    }
    // === Extract content using state machine parser ===
    char *content = extract_content_from_json(response);
    if (!content) {
        // Check for error response
        if (strstr(response, "\"error\"")) {
            printf("API returned an error: %s\n",response);
        } else {
            printf("No content in response.\n");
        }
        free(response);
        memset(api_key, 0, sizeof(api_key));
        memset(api_model, 0, sizeof(api_model));
        memset(api_user, 0, sizeof(api_user));
        return 1;
    }
    
    // Print the extracted content with proper formatting
    print_folded(content, 72);
    (void)putchar('\n');
    
    free(content);
    free(response);
    
    // Clear sensitive data from memory
    memset(api_key, 0, sizeof(api_key));
    memset(api_model, 0, sizeof(api_model));
    memset(api_user, 0, sizeof(api_user));
    
    return 0;
}