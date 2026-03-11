#include <stdio.h>
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
    
    #define BUFFER_SIZE (2 * 1024 * 1024)  // 2 MB buffer for responses
    
    // Apply uniform spacing: ensure two spaces after sentence-ending punctuation
    static char *format_text_spacing(const char *text) {
    if (!text) return NULL;
    size_t len = strlen(text);
    char *result = malloc(len * 2 + 1);  // Allocate extra space for potential  additions
    if (!result) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; ++i) {
    result[j++] = text[i];
    if ((text[i] == '.' || text[i] == '?' || text[i] == '!') &&
    i + 1 < len && text[i + 1] == ' ') {
    if (i + 2 >= len || text[i + 2] != ' ') {
    result[j++] = ' ';  // Add second space if only one exists
    }
    }
    }
    result[j] = '\0';
    return result;
    }
    
    // Extract and save code blocks from content based on prompt
    static void extract_and_save_code_blocks(const char *content, const char  *prompt) {
    if (!content) return;
    const char *ptr = content;
    int block_count = 0;
    while ((ptr = strstr(ptr, "```"))) {
    ptr += 3;  // Skip ```
    // Extract language hint
    const char *lang_start = ptr;
    while (*ptr && *ptr != '\n') ++ptr;
    if (*ptr == '\n') ++ptr;
    char lang[32] = {0};
    size_t lang_len = ptr - lang_start - 1;  // Exclude newline
    if (lang_len < sizeof(lang)) {
    memcpy(lang, lang_start, lang_len);
    }
    // Find closing ```
    const char *code_start = ptr;
    const char *code_end = strstr(ptr, "```");
    if (!code_end) break;
    size_t code_len = code_end - code_start;
    char *code = malloc(code_len + 1);
    if (!code) break;
    memcpy(code, code_start, code_len);
    code[code_len] = '\0';
    // Determine extension
    char ext[8] = ".txt";
    if (strcasecmp(lang, "c") == 0 || strstr(lang, "c")) strcpy(ext, ".c");
    else if (strcasecmp(lang, "python") == 0 || strstr(lang, "py")) 
    strcpy(ext, ".py");
    else if (strcasecmp(lang, "javascript") == 0 || strstr(lang, "js")) 
    strcpy(ext, ".js");
    // Parse filename from prompt
    char filename[256] = {0};
    const char *save_pos = strstr(prompt, "save as");
    if (save_pos) {
    save_pos += 8;  // Skip "save as "
    const char *name_end = strstr(save_pos, " ");
    if (!name_end) name_end = save_pos + strlen(save_pos);
    size_t name_len = name_end - save_pos;
    if (name_len < sizeof(filename) - 10) {
    memcpy(filename, save_pos, name_len);
    if (!strchr(filename, '.')) strncat(filename, ext, sizeof(filename) - 
    strlen(filename) - 1);
    } else {
    snprintf(filename, sizeof(filename), "code%s", ext);
    }
    } else {
    snprintf(filename, sizeof(filename), "code%s", ext);
    }
    // Ensure uniqueness
    char unique_name[256];
    strcpy(unique_name, filename);
    int counter = 1;
    while (access(unique_name, F_OK) == 0) {
    char base[256];
    strcpy(base, filename);
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    snprintf(unique_name, sizeof(unique_name), "%s_%d%s", base, counter++, 
    ext);
    }
    // Write to file
    FILE *file = fopen(unique_name, "w");
    if (file) {
    fputs(code, file);
    fclose(file);
    printf("Saved code to %s\n", unique_name);
    } else {
    fprintf(stderr, "Failed to save to %s\n", unique_name);
    }
    free(code);
    ptr = code_end + 3;
    ++block_count;
    }
    if (block_count > 0) {
    printf("Extracted and saved %d file(s).\n", block_count);
    }
    }
    
    // Simple JSON content extractor
    static char *extract_json_content(const char *json) {
    if (!json) return NULL;
    const char *ptr = strstr(json, "\"content\"");
    if (!ptr) return NULL;
    ptr = strstr(ptr, "\":");
    if (!ptr) return NULL;
    ptr += 2;
    while (*ptr && isspace((unsigned char)*ptr)) ++ptr;
    if (*ptr != '"') return NULL;
    ++ptr;  // Skip opening quote
    char *content = malloc(BUFFER_SIZE);
    if (!content) return NULL;
    size_t idx = 0;
    while (*ptr && *ptr != '"' && idx < BUFFER_SIZE - 1) {
    if (*ptr == '\\') {
    ++ptr;
    if (*ptr == 'n') content[idx++] = '\n';
    else if (*ptr == 'r') content[idx++] = '\r';
    else if (*ptr == 't') content[idx++] = '\t';
    else if (*ptr == '"') content[idx++] = '"';
    else if (*ptr == '\\') content[idx++] = '\\';
    else content[idx++] = *ptr;
    } else {
    content[idx++] = *ptr;
    }
    ++ptr;
    }
    content[idx] = '\0';
    return content;
    }
    
    // Print text with word wrapping
    static void print_wrapped(const char *text, int width) {
    if (!text) return;
    const char *ptr = text;
    while (*ptr) {
    printf("    ");
    // Skip leading whitespace except newlines
    while (*ptr && isspace((unsigned char)*ptr) && *ptr != '\n') ++ptr;
    const char *line_start = ptr;
    int col = 0;
    const char *last_space = NULL;
    while (*ptr && *ptr != '\n' && col < width) {
    if (isspace((unsigned char)*ptr)) last_space = ptr;
    ++col;
    ++ptr;
    }
    if (!*ptr || *ptr == '\n') {
    fwrite(line_start, 1, ptr - line_start, stdout);
    if (*ptr == '\n') putchar('\n');
    if (*ptr) ++ptr;
    } else if (last_space) {
    fwrite(line_start, 1, last_space - line_start + 1, stdout);
    putchar('\n');
    ptr = last_space + 1;
    } else {
    fwrite(line_start, 1, width, stdout);
    putchar('\n');
    ptr = line_start + width;
    }
    }
    }
    
    // Global config variables
    static char api_url[1024] = {0};
    static char api_key[1024] = {0};
    static char api_model[1024] = "grok-1";
    static char api_user[1024] = "Anonymous";
    static char api_context[1024] = "You are Grok, a helpful and maximally truthful AI built by xAI.";
    static bool debug_mode = false;
    
    // Securely wipe sensitive data
    static void clear_sensitive_data(void) {
    memset(api_key, 0, sizeof(api_key));
    memset(api_model, 0, sizeof(api_model));
    memset(api_user, 0, sizeof(api_user));
    memset(api_context, 0, sizeof(api_context));
    }
    
    // Load configuration from environment
    static bool load_config(void) {
    for (char **env = environ; *env; ++env) {
    char *env_copy = strdup(*env);
    if (!env_copy) return false;
    char *key = strtok(env_copy, "=");
    char *value = strtok(NULL, "");
    if (key && value) {
    if (strcmp(key, "FEED_URL") == 0 && strlen(value) < sizeof(api_url)) {
    strcpy(api_url, value);
    } else if (strcmp(key, "FEED_KEY") == 0 && strlen(value) < 
    sizeof(api_key)) {
    strcpy(api_key, value);
    } else if (strcmp(key, "FEED_MODEL") == 0 && strlen(value) < 
    sizeof(api_model)) {
    strcpy(api_model, value);
    } else if (strcmp(key, "FEED_USER") == 0 && strlen(value) < 
    sizeof(api_user)) {
    strcpy(api_user, value);
    } else if (strcmp(key, "FEED_CONTEXT") == 0 && strlen(value) < 
    sizeof(api_context)) {
    strcpy(api_context, value);
    }
    }
    free(env_copy);
    }
    return api_key[0] && api_model[0] && api_url[0];
    }
    
    // Escape prompt for JSON
    static char *escape_json_string(const char *str) {
    if (!str) return NULL;
    char *escaped = malloc(BUFFER_SIZE / 4);
    if (!escaped) return NULL;
    size_t j = 0;
    for (const char *p = str; *p && j < sizeof(escaped) - 10; ++p) {
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
    
    int main(int argc, char *argv[]) {
    char *prompt = NULL;
    for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
    debug_mode = true;
    } else if (!prompt) {
    prompt = argv[i];
    } else {
    fprintf(stderr, "Usage: %s [--debug|-d] \"prompt\"\n", argv[0]);
    return EXIT_FAILURE;
    }
    }
    if (!prompt) {
    fprintf(stderr, "Usage: %s [--debug|-d] \"prompt\"\n", argv[0]);
    return EXIT_FAILURE;
    }
    if (!load_config()) {
    fprintf(stderr, "Missing FEED_URL, FEED_KEY, or FEED_MODEL\n");
    clear_sensitive_data();
    return EXIT_FAILURE;
    }
    char *escaped_prompt = escape_json_string(prompt);
    if (!escaped_prompt) {
    fprintf(stderr, "Memory error\n");
    clear_sensitive_data();
    return EXIT_FAILURE;
    }
    char user_msg[BUFFER_SIZE / 4];
    if (snprintf(user_msg, sizeof(user_msg), "{\"role\":\"user\",\"name\":\"%s\",\"content\":\"%s\"}",
    api_user, escaped_prompt) >= (int)sizeof(user_msg)) {
    fprintf(stderr, "User message too long\n");
    free(escaped_prompt);
    clear_sensitive_data();
    return EXIT_FAILURE;
    }
    char auth_hdr[2048];
    if (snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s",  api_key) >= (int)sizeof(auth_hdr)) {
    fprintf(stderr, "Auth header too long\n");
    free(escaped_prompt);
    clear_sensitive_data();
    return EXIT_FAILURE;
    }
    char json_payload[BUFFER_SIZE];
    if (snprintf(json_payload, sizeof(json_payload),
    "{\"model\":\"%s\",\"system\":\"%s\",\"messages\":[%s]}", api_model, api_context, user_msg) >= (int)sizeof(json_payload)) {
    fprintf(stderr, "JSON payload too long\n");
    free(escaped_prompt);
    clear_sensitive_data();
    return EXIT_FAILURE;
    }
    if (debug_mode) {
    printf("Debug: URL: %s\n", api_url);
    printf("Debug: Payload: %s\n", json_payload);
    }
    char *curl_args[] = {"curl", "-s", "--max-time", "3600", api_url, "-H", "Content-Type: application/json", "-H", auth_hdr, "-d", json_payload, NULL};
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1) {
    perror("pipe");
    free(escaped_prompt);
    clear_sensitive_data();
    return EXIT_FAILURE;
    }
    pid_t child_pid = fork();
    if (child_pid == -1) {
    perror("fork");
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    free(escaped_prompt);
    clear_sensitive_data();
    return EXIT_FAILURE;
    }
    FILE *pipe_fp = NULL;
    if (child_pid == 0) {
    close(pipe_fds[0]);
    dup2(pipe_fds[1], STDOUT_FILENO);
    close(pipe_fds[1]);
    execvp("curl", curl_args);
    perror("execvp");
    _exit(EXIT_FAILURE);
    } else {
    close(pipe_fds[1]);
    pipe_fp = fdopen(pipe_fds[0], "r");
    if (!pipe_fp) {
    perror("fdopen");
    close(pipe_fds[0]);
    free(escaped_prompt);
    clear_sensitive_data();
    return EXIT_FAILURE;
    }
    }
    // Clear screen and print prompt in blue
    printf("\x1b[2J\x1b[H\x1b[34m");
    char *formatted_prompt = format_text_spacing(prompt);
    print_wrapped(formatted_prompt ?  formatted_prompt : prompt, 75);
    printf("\x1b[0m\n\n");
    free(formatted_prompt);
    char *response = malloc(BUFFER_SIZE);
    if (!response) {
    fprintf(stderr, "Memory error\n");
    fclose(pipe_fp);
    free(escaped_prompt);
    clear_sensitive_data();
    return EXIT_FAILURE;
    }
    size_t bytes_read = fread(response, 1, BUFFER_SIZE - 1, pipe_fp);
    response[bytes_read] = '\0';
    fclose(pipe_fp);
    if (debug_mode) {
    printf("Debug: Response: %s\n", response);
    }
    int status;
    waitpid(child_pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "curl failed\n");
    free(response);
    free(escaped_prompt);
    clear_sensitive_data();
    return EXIT_FAILURE;
    }
    char *content = extract_json_content(response);
    if (!content) {
    if (strstr(response, "\"error\"")) {
    printf("API error: %s\n", response);
    } else {
    printf("No content in response.\n");
    printf("Try using first name in FEED_USER or unset for anonymous.\n");
    }
    free(response);
    free(escaped_prompt);
    clear_sensitive_data();
    return EXIT_FAILURE;
    }
    extract_and_save_code_blocks(content, prompt);
    char *formatted_content = format_text_spacing(content);
    print_wrapped(formatted_content ?  formatted_content : content, 75);
    putchar('\n');
    free(formatted_content);
    free(content);
    free(response);
    free(escaped_prompt);
    clear_sensitive_data();
    return EXIT_SUCCESS;
    }