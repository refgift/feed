#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

// Improved JSON parser using cJSON-like approach, but kept simple
// Returns a newly allocated string with the content, or NULL on error

static char *apply_uniform_spacing(const char *text) {
    if (!text) return NULL;
    size_t len = strlen(text);
    char *result = malloc(len * 2 + 1); // worst case
    if (!result) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        result[j++] = text[i];
        if ((text[i] == '.' || text[i] == '?' || text[i] == '!') && i + 1 < len && text[i + 1] == ' ') {
            // Already has space, check if second space
            if (i + 2 < len && text[i + 2] == ' ') {
                // Already two spaces
            } else {
                result[j++] = ' '; // Add second space
            }
        }
    }
    result[j] = '\0';
    return result;
}

static void save_code_blocks(const char *content, const char *prompt) {
    if (!content) return;
    const char *p = content;
    int block_count = 0;
    while ((p = strstr(p, "```"))) {
        p += 3; // skip ```
        // Extract language
        const char *lang_start = p;
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++; // skip newline
        char lang[32] = "";
        size_t lang_len = (size_t)(p - lang_start - 1); // -1 for newline
        if (lang_len < sizeof(lang)) {
            memcpy(lang, lang_start, lang_len);
            lang[lang_len] = '\0';
        }
        // Find end ```
        const char *code_start = p;
        const char *end = strstr(p, "```");
        if (!end) break; // malformed
        int code_len = (int)(end - code_start);
        char *code = malloc(code_len + 1);
        if (!code) break;
        memcpy(code, code_start, code_len);
        code[code_len] = '\0';
        // Infer filename
        char filename[256];
        char ext[8] = ".txt";
        if (strcasecmp(lang, "c") == 0 || strstr(lang, "c") != NULL) strcpy(ext, ".c");
        else if (strcasecmp(lang, "python") == 0 || strstr(lang, "py") != NULL) strcpy(ext, ".py");
        else if (strcasecmp(lang, "javascript") == 0 || strstr(lang, "js") != NULL) strcpy(ext, ".js");
        // Check prompt for "save as"
        const char *save_as = strstr(prompt, "save as");
        if (save_as) {
            save_as += 8; // skip "save as "
            const char *end_name = strstr(save_as, " ");
            if (!end_name) end_name = save_as + strlen(save_as);
            int name_len = (int)(end_name - save_as);
            if (name_len < sizeof(filename) - 10) {
            memcpy(filename, save_as, (size_t)name_len);
            filename[name_len] = '\0';
            // Ensure extension
            if (!strstr(filename, ".")) strncat(filename, ext, sizeof(filename) - strlen(filename) - 1);
            } else {
                sprintf(filename, "food%s", ext);
            }
        } else {
            snprintf(filename, sizeof(filename), "food%s", ext);
        }
        // Uniqueness
        char final_name[256];
        strcpy(final_name, filename);
        int counter = 1;
        while (access(final_name, F_OK) == 0) {
            snprintf(final_name, sizeof(final_name), "%s_%d%s", filename, counter++, ext);
        }
        // Save
        FILE *f = fopen(final_name, "w");
        if (f) {
            (void)fputs(code, f);
            (void)fclose(f);
            printf("Saved code to %s\n", final_name);
        } else {
            fprintf(stderr, "Failed to save to %s\n", final_name);
        }
        free(code);
        p = end + 3; // skip ```
        block_count++;
    }
    if (block_count > 0) {
        printf("Extracted and saved %d file(s).\n", block_count);
    }
}

/*@null@*/ static char * extract_content_from_json(const char *json) {
    if (!json) return NULL;
    const char *p = strstr(json, "\"choices\"");
    if (!p) return NULL;
    p = strstr(p, "\"message\"");
    if (!p) return NULL;
    p = strstr(p, "\"content\"");
    if (!p) return NULL;
    p = strstr(p, "\":");
    if (!p) return NULL;
    p += 2; // skip ":
    while (*p != '\0' && isspace((unsigned char)*p)) p++;
    if (*p != '"') return NULL;
    p++; // skip opening "
    char *content = malloc(BUFFER_SIZE);
    if (!content) return NULL;
    size_t idx = 0;
    while (*p != '\0' && *p != '"' && idx < BUFFER_SIZE - 1) {
        if (*p == '\\') {
            p++;
            if (*p == 'n') content[idx++] = '\n';
            else if (*p == 'r') content[idx++] = '\r';
            else if (*p == 't') content[idx++] = '\t';
            else if (*p == '"') content[idx++] = '"';
            else if (*p == '\\') content[idx++] = '\\';
            else content[idx++] = *p;
        } else {
            content[idx++] = *p;
        }
        p++;
    }
    content[idx] = '\0';
    return content;
}

static void print_folded(const char *text, int width) {
    if (!text) return;
    const char *p = text;
    while (*p != '\0') {
        printf("    ");
        while (*p != '\0' && isspace((unsigned char)*p) && *p != '\n') p++;
        const char *line_start = p;
        int col = 0;
        const char *last_space = NULL;
        while (*p != '\0' && *p != '\n' && col < width) {
            if (isspace((unsigned char)*p)) last_space = p;
            col++;
            p++;
        }
        if (*p == '\0' || *p == '\n') {
            (void)fwrite(line_start, 1, (size_t)(p - line_start), stdout);
            if (*p == '\n') (void)putchar('\n');
            if (*p != '\0') p++;
        } else if (last_space) {
            (void)fwrite(line_start, 1, (size_t)(last_space - line_start + 1), stdout);
            (void)putchar('\n');
            p = last_space + 1;
        } else {
            (void)fwrite(line_start, 1, (size_t)width, stdout);
            (void)putchar('\n');
            p = line_start + width;
        }
    }
}

static char api_url[1024] = "";
static char api_key[1024] = "";
static char api_model[1024] = "grok-1";
static char api_user[1024] = "Anonymous";
static char api_context[1024] = "You are Grok, a helpful and maximally truthful AI built by xAI.";
static bool debug = false;


static void wipe_sensitive(void) {
    memset(api_key, 0, sizeof(api_key));
    memset(api_model, 0, sizeof(api_model));
    memset(api_user, 0, sizeof(api_user));
    memset(api_context, 0, sizeof(api_context));
}

static bool load_env_vars(void) {
    for (int i = 0; environ[i]; i++) {
        char *env_copy = strdup(environ[i]);
        if (!env_copy) return false;
        char *key = strtok(env_copy, "=");
        char *value = strtok(NULL, "");
        if (key != NULL && value != NULL) {
            if (strcmp(key, "FEED_URL") == 0 && strlen(value) < sizeof(api_url)) {
                strcpy(api_url, value);
            } else if (strcmp(key, "FEED_KEY") == 0 && strlen(value) < sizeof(api_key)) {
                strcpy(api_key, value);
            } else if (strcmp(key, "FEED_MODEL") == 0 && strlen(value) > 0 && strlen(value) < sizeof(api_model)) {
                strcpy(api_model, value);
            } else if (strcmp(key, "FEED_USER") == 0 && strlen(value) < sizeof(api_user)) {
                strcpy(api_user, value);
            } else if (strcmp(key, "FEED_CONTEXT") == 0 && strlen(value) < sizeof(api_context)) {
                strcpy(api_context, value);
            }
        }
        free(env_copy);
    }
    return strlen(api_key) > 0 && strlen(api_model) > 0 && strlen(api_url) > 0;
}

/*@null@*/ static char *escape_prompt(const char *prompt) {
    if (!prompt) return NULL;
    char *escaped = malloc(BUFFER_SIZE / 4);
    if (!escaped) return NULL;
    size_t j = 0;
    for (const char *p = prompt; *p != '\0' && j < sizeof(escaped) - 10; p++) {
        switch (*p) {
            case '"': escaped[j++] = '\\'; escaped[j++] = '"'; break;
            case '\\': escaped[j++] = '\\'; escaped[j++] = '\\'; break;
            case '\n': escaped[j++] = '\\'; escaped[j++] = 'n'; break;
            case '\r': escaped[j++] = '\\'; escaped[j++] = 'r'; break;
            case '\t': escaped[j++] = '\\'; escaped[j++] = 't'; break;
            default: escaped[j++] = *p;
        }
    }
    escaped[j] = '\0';
    return escaped;
}

int main(int argc, char **argv) {
    char *prompt = NULL;
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            debug = true;
        } else if (!prompt) {
            prompt = argv[i];
        } else {
            fprintf(stderr, "Usage: %s [--debug|-d] \"your prompt here\"\n", argv[0]);
            return 1;
        }
        i++;
    }

    if (!prompt) {
        fprintf(stderr, "Usage: %s [--debug|-d] \"your prompt here\"\n", argv[0]);
        return 1;
    }

    if (!load_env_vars()) {
        fprintf(stderr, "Missing or invalid FEED_URL, FEED_KEY, or FEED_MODEL\n");
        wipe_sensitive();
        return 1;
    }

    char *escaped = escape_prompt(prompt);
    if (!escaped) {
        fprintf(stderr, "Memory allocation failed\n");
        wipe_sensitive();
        return 1;
    }

    char user_message[BUFFER_SIZE / 4];
    (void)snprintf(user_message, sizeof(user_message), "{\"role\":\"user\",\"name\":\"%s\",\"content\":\"%s\"}", api_user, escaped);

    char auth_header[2048];
    int written = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    if (written >= (int)sizeof(auth_header)) {
        fprintf(stderr, "Auth header too long\n");
        free(escaped);
        wipe_sensitive();
        return 1;
    }

    char json_data[BUFFER_SIZE];
    written = snprintf(json_data, sizeof(json_data),
        "{\"model\": \"%s\",\"system\": \"%s\",\"messages\":[%s]}",
        api_model, api_context, user_message);
    if (written >= (int)sizeof(json_data)) {
        fprintf(stderr, "JSON data too long\n");
        free(escaped);
        wipe_sensitive();
        return 1;
    }

    if (debug) {
        printf("Debug: api_url: '%s'\n", api_url);
        printf("Debug: JSON data: %s\n", json_data);
    }

    char *args[] = {"curl", "-s", "--max-time", "3600", api_url, "-H", "Content-Type: application/json", "-H", auth_header, "-d", json_data, NULL};

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        free(escaped);
        wipe_sensitive();
        return 1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        (void)close(pipefd[0]);
        (void)close(pipefd[1]);
        free(escaped);
        wipe_sensitive();
        return 1;
    }

    FILE *fp = NULL;
    if (pid == 0) {
        (void)close(pipefd[0]);
        (void)dup2(pipefd[1], STDOUT_FILENO);
        (void)close(pipefd[1]);
        (void)execvp("curl", args);
        perror("execvp failed");
        exit(EXIT_FAILURE);
    } else {
        (void)close(pipefd[1]);
        fp = fdopen(pipefd[0], "r");
        if (!fp) {
            perror("fdopen failed");
            (void)close(pipefd[0]);
            free(escaped);
            wipe_sensitive();
            return 1;
        }
    }

    printf("\x1b[2J\x1b[H\x1b[34m");
    char *fmt_prompt = apply_uniform_spacing(prompt);
    if (fmt_prompt) {
        print_folded(fmt_prompt, 75); // Use 75 like fmt
        free(fmt_prompt);
    } else {
        print_folded(prompt, 75);
    }
    printf("\x1b[0m\n\n");

    char *response = malloc(BUFFER_SIZE);
    if (!response) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(fp);
        free(escaped);
        wipe_sensitive();
        return 1;
    }

    size_t len = fread(response, 1, BUFFER_SIZE - 1, fp);
    response[len] = '\0';
    (void)fclose(fp);

    if (debug) {
        printf("Debug: API response: %s\n", response);
    }

    int status;
    (void)waitpid(pid, &status, 0);
    if (WIFEXITED(status) == 0 || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "curl failed\n");
        free(response);
        free(escaped);
        wipe_sensitive();
        return 1;
    }

    char *content = extract_content_from_json(response);
    if (!content) {
        if (strstr(response, "\"error\"")) {
            printf("API returned an error: %s\n", response);
        } else {
            printf("No content in response.\n");
            printf("If this persists, try using just your first name in FEED_USER or unset it for anonymous mode.\n");
        }
        free(response);
        free(escaped);
        wipe_sensitive();
        return 1;
    }

    save_code_blocks(content, prompt);

    char *fmt_content = apply_uniform_spacing(content);
    if (fmt_content) {
        print_folded(fmt_content, 75);
        free(fmt_content);
    } else {
        print_folded(content, 75);
    }
    (void)putchar('\n');

    free(content);
    free(response);
    free(escaped);
    wipe_sensitive();
    return 0;
}