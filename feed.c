#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define BUFFER_SIZE (2 * 1024 * 1024)  // 2 MB - more than enough for any chat response

void print_unescaped(const char *s) {
    while (*s) {
        if (*s == '\\') {
            ++s;
            if (!*s) break;
            switch (*s) {
                case 'n':  putchar('\n'); break;
                case 'r':  putchar('\r'); break;
                case 't':  putchar('\t'); break;
                case 'b':  putchar('\b'); break;
                case 'f':  putchar('\f'); break;
                case '"':  putchar('"');  break;
                case '\\': putchar('\\'); break;
                case '/':  putchar('/');  break;
                // \uXXXX is ignored for simplicity (most text is ASCII/UTF-8)
                default:   putchar(*s);
            }
            ++s;
        } else {
            putchar(*s);
            ++s;
        }
    }
}

char * key;
char * value;
char api_key[1024];
char api_model[1024];
char * token;
int main(int argc, char **argv, char**expv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s \"your prompt here\"\n", argv[0]);
        return 1;
    }

    for (int i=0; expv[i]!=0; i++) {
	key = strtok(expv[i],"=");
	value = strtok(NULL,"=");
	//printf("%s %s\n",key, value);
	if (strcmp(key,"FEED_KEY")==0) {
		strcpy(api_key,value);
	}
	if (strcmp(key,"FEED_MODEL")==0) {
		strcpy(api_model,value);
	}
    }

    if (strlen(api_key)==0) {
	fprintf(stderr, "No FEED_KEY exported\n");
        exit(-1);
    }
    if (strlen(api_model)==0) {
	fprintf(stderr, "No FEED_MODEL exported\n");
        exit(-1);
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

    // === Build curl command (standard xAI endpoint) ===
    char cmd[BUFFER_SIZE / 2];
    snprintf(cmd, sizeof(cmd),
        "curl -s --max-time 3600 "
        "https://api.x.ai/v1/chat/completions "
        "-H 'Content-Type: application/json' "
        "-H 'Authorization: Bearer %s' "
        "-d '{\"model\":\"%s\",\"messages\":["
        "{\"role\":\"system\",\"content\":\"the system coding rule is no contexts.\"},"
        "{\"role\":\"user\",\"content\":\"%s\"}],"
        "\"temperature\":0.7,\"max_tokens\":4096}'",
        api_key, api_model, escaped);

  

    printf("\x1b[2J\x1b[H\x1b[34m%s\x1b[0m\n\n",argv[1]);
		

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        perror("popen failed");
        return 1;
    }

    // === Read entire JSON response into memory (most robust approach) ===
    char *response = malloc(BUFFER_SIZE);
    if (!response) {
        fprintf(stderr, "Memory allocation failed\n");
        pclose(pipe);
        return 1;
    }
    size_t len = fread(response, 1, BUFFER_SIZE - 1, pipe);
    response[len] = '\0';
    pclose(pipe);

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

     int cols=0;

    // === Print with proper unescaping until closing quote ===
    while (*p && *p != '"') {
        if (*p == '\\') {
            ++p;
            if (!*p) break;
            switch (*p) {
                case 'n':  putchar('\n'); break;
                case 'r':  putchar('\r'); break;
                case 't':  putchar('\t'); break;
                case 'b':  putchar('\b'); break;
                case 'f':  putchar('\f'); break;
                case '"':  putchar('"');  break;
                case '\\': putchar('\\'); break;
                case '/':  putchar('/');  break;
                default:   putchar(*p);
            }
        } else {
            putchar(*p);
	    if (cols++ > 72 && isblank(*p)) {
		putchar(10);
		cols=0;
	     }
        }
        ++p;
    }

    putchar('\n');
    free(response);
    return 0;
}
