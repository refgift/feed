#include <stdio.h>
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdbool.h>
#include <wait.h>
#include <ctype.h>
#include <unistd.h>
/* Forward declarations for splint */
extern char *strdup (const char *s);
extern FILE *fdopen (int fd, const char *mode);
extern char **environ;
#define BUFFER_SIZE (2 * 1024 * 1024)   // 2 MB buffer for responses
    // Apply uniform spacing: ensure two spaces after sentence-ending punctuation
static char *
format_text_spacing (const char *text)
{
  if (strlen(text)==0) return (char *) text;
  int len = (int) strlen (text);
  char *result = malloc ((size_t) (len * 2 + 1));  // Allocate extra space for potential  additions
  if (!result)
    return (char *) NULL;
  int j = 0;
  for (int i = 0; i < len; ++i)
    {
      result[j++] = text[i];
      if ((text[i] == '.' || text[i] == '?' || text[i] == '!') &&
          i + 1 < len && text[i + 1] == ' ')
        {
          if (i + 2 >= len || text[i + 2] != ' ')
            {
              result[j++] = ' ';        // Add second space if only one exists
            }
        }
    }
  result[j] = '\0';
  return result;
}
    // Extract and save code blocks from content based on prompt
static void
extract_and_save_code_blocks (const char *content, const char *prompt)
{
  if (!content)
    return;
  const char *ptr = content;
  int block_count = 0;
  while ((ptr = strstr (ptr, "```")))
    {
      ptr += 3;                 // Skip ```
      // Extract language hint
      const char *lang_start = ptr;
      while (*ptr && *ptr != '\n')
        ++ptr;
      if (*ptr == '\n')
        ++ptr;
      char lang[32]="";
      size_t lang_len = (size_t) (ptr - lang_start - 1);   // Exclude newline
      if (lang_len < strlen (lang))
        {
          memcpy (lang, lang_start, lang_len);
        }
      // Find closing ```
      const char *code_start = ptr;
      const char *code_end = strstr (ptr, "```");
      if (!code_end)
        break;
      size_t code_len = (size_t) (code_end - code_start);
      char *code = malloc ((size_t) (code_len + 1));
      if (!code)
        break;
      memcpy (code, code_start, code_len);
      code[code_len] = '\0';
      // Determine extension
      char ext[8] = ".txt";
      if (strcasecmp (lang, "c") == 0 || strstr (lang, "c")==0)
        strcpy (ext, ".c");
      else if (strcasecmp (lang, "python") == 0 || strstr (lang, "py")==0)
        strcpy (ext, ".py");
      else if (strcasecmp (lang, "javascript") == 0 || strstr (lang, "js")==0)
        strcpy (ext, ".js");
      // Parse filename from prompt
      char filename[256] = "";
      const char *save_pos = strstr (prompt, "save as");
      if (save_pos!=NULL)
        {
          save_pos += 8;        // Skip "save as "
          const char *name_end = strcasestr (save_pos, " ");
          if (name_end!=NULL)
            name_end = save_pos + strlen (save_pos);
          size_t name_len = (size_t) (name_end - save_pos);
          if (name_len < sizeof (filename) - 10)
            {
              memcpy (filename, save_pos, name_len);
              if (!strchr (filename, '.'))
                strncat (filename, ext, sizeof (filename) -
                         strlen (filename) - 1);
            }
          else
            {
              snprintf (filename, sizeof (filename), "code%s", ext);
            }
        }
      else
        {
          snprintf (filename, sizeof (filename), "code%s", ext);
        }
      // Ensure uniqueness
      char unique_name[512];
      strcpy (unique_name, filename);
      int counter = 1;
      while (access (unique_name, F_OK) == 0)
        {
          char base[256];
          strcpy (base, filename);
          char *dot = strrchr (base, '.');
          if (dot)
            *dot = '\0';
          snprintf (unique_name, sizeof (unique_name), "%s_%d%s", base,
                    counter++, ext);
        }
      // Write to file
      FILE *file = fopen (unique_name, "w");
      if (file)
        {
          fputs (code, file);
          fclose (file);
          printf ("Saved code to %s\n", unique_name);
        }
      else
        {
          fprintf (stderr, "Failed to save to %s\n", unique_name);
        }
      free (code);
      ptr = code_end + 3;
      ++block_count;
    }
  if (block_count > 0)
    {
      printf ("Extracted and saved %d file(s).\n", block_count);
    }
}
    // Simple JSON content extractor
static char *
extract_json_content (const char *json)
{
  if (!json)
    return NULL;
  // Find the content in the assistant message
  const char *ptr = strstr (json, "\"content\"");
  if (!ptr)
    return NULL;
  ptr = strstr (ptr, "\"text\"");
  if (!ptr)
    return NULL;
  ptr = strstr (ptr, "\":");
  if (!ptr)
    return NULL;
  ptr += 2;
  while (*ptr && isspace ((unsigned char) *ptr))
    ++ptr;
  if (*ptr != '"')
    return NULL;
  ++ptr;                        // Skip opening quote
  char *content = malloc ((size_t) BUFFER_SIZE);
  if (!content)
    return NULL;
  int idx = 0;
  while (*ptr && *ptr != '"' && idx < BUFFER_SIZE - 1)
    {
      if (*ptr == '\\')
        {
          ++ptr;
          if (*ptr == 'n')
            content[idx++] = '\n';
          else if (*ptr == 'r')
            content[idx++] = '\r';
          else if (*ptr == 't')
            content[idx++] = '\t';
          else if (*ptr == '"')
            content[idx++] = '"';
          else if (*ptr == '\\')
            content[idx++] = '\\';
          else
            content[idx++] = *ptr;
        }
      else
        {
          content[idx++] = *ptr;
        }
      ++ptr;
    }
  content[idx] = '\0';
  return content;
}
    // Print text with word wrapping
static void
print_wrapped (const char *text, int width)
{
  if (!text)
    return;
  const char *ptr = text;
  while (*ptr)
    {
      printf ("    ");
      // Skip leading whitespace except newlines
      while (*ptr && isspace ((unsigned char) *ptr) && *ptr != '\n')
        ++ptr;
      const char *line_start = ptr;
      int col = 0;
      const char *last_space = NULL;
      while (*ptr && *ptr != '\n' && col < width)
        {
          if (isspace ((unsigned char) *ptr))
            last_space = ptr;
          ++col;
          ++ptr;
        }
      if (!*ptr || *ptr == '\n')
        {
          (void) fwrite (line_start, 1, (size_t)(ptr - line_start), stdout);
          if (*ptr == '\n')
            (void) putchar ('\n');
          if (*ptr)
            ++ptr;
        }
      else if (last_space)
        {
          (void) fwrite (line_start, 1, (size_t) (last_space - line_start + 1), stdout);
          (void) putchar ('\n');
          ptr = last_space + 1;
        }
      else
        {
          (void) fwrite (line_start, 1, (size_t) width, stdout);
          (void) putchar ('\n');
          ptr = line_start + width;
        }
    }
}
    // Global config variables
static char api_url[1024]="";
static char api_key[1024]="";
static char api_model[1024] = "grok-1";
static bool debug_mode = false;
    // Securely wipe sensitive data
static void
clear_sensitive_data (void)
{
  memset (api_key, 0, sizeof (api_key));
  memset (api_model, 0, sizeof (api_model));
}
    // Load configuration from environment
static bool
load_config (void)
{
  for (char **env = environ; *env; ++env)
    {
      char *env_copy = strdup (*env);
      if (!env_copy)
        return false;
      char *key = strtok (env_copy, "=");
      char *value = strtok (NULL, "");
      if (key!=NULL&&strlen(key)>0 && value!=NULL&&strlen(value)>0)
        {
          if (strcmp (key, "FEED_URL") == 0
              && strlen (value) < sizeof (api_url))
            {
              strcpy (api_url, value);
            }
          else if (strcmp (key, "FEED_KEY") == 0 && strlen (value) <
                   sizeof (api_key))
            {
              strcpy (api_key, value);
            }
          else if (strcmp (key, "FEED_MODEL") == 0 && strlen (value) <
                   sizeof (api_model))
            {
              strcpy (api_model, value);
            }
       }
      free (env_copy);
    }
  return api_key[0] && api_model[0] && api_url[0];
}
    // Escape prompt for JSON
static char *
escape_json_string (const char *str)
{
  if (!str) return NULL;
  char *escaped = calloc ((size_t) (BUFFER_SIZE / 4), strlen(str));
  if (!escaped) return NULL;
  size_t j = 0;
  for (const char *p = str; *p && j <BUFFER_SIZE/4 ; ++p)
    {
      switch (*p)
        {
        case '"':
          escaped[j++] = '\\';
          escaped[j++] = '"';
          break;
        case '\\':
          escaped[j++] = '\\';
          escaped[j++] = '\\';
          break;
        case '\n':
          escaped[j++] = '\\';
          escaped[j++] = 'n';
          break;
        case '\r':
          escaped[j++] = '\\';
          escaped[j++] = 'r';
          break;
        case '\t':
          escaped[j++] = '\\';
          escaped[j++] = 't';
          break;
        default:
          escaped[j++] = *p;
        }
    }
  escaped[j] = '\0';
  return escaped;
}
int
main (int argc, char *argv[])
{
  char *prompt = NULL;
  for (int i = 1; i < argc; ++i)
    {
      if (strcmp (argv[i], "--debug") == 0 || strcmp (argv[i], "-d") == 0)
        {
          debug_mode = true;
        }
      else if (!prompt)
        {
          prompt = argv[i];
        }
      else
        {
          fprintf (stderr, "Usage: %s [--debug|-d] \"prompt\"\n", argv[0]);
          return EXIT_FAILURE;
        }
    }
  if (!prompt)
    {
      fprintf (stderr, "Usage: %s [--debug|-d] \"prompt\"\n", argv[0]);
      return EXIT_FAILURE;
    }
  if (!load_config ())
    {
      fprintf (stderr, "Missing FEED_URL, FEED_KEY, or FEED_MODEL\n");
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  char *escaped_prompt = escape_json_string (prompt);
  if (!escaped_prompt)
    {
      fprintf (stderr, "Memory error\n");
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  char user_msg[BUFFER_SIZE / 4];
  if (snprintf
      (user_msg, BUFFER_SIZE/4 ,"%s",escaped_prompt) >= BUFFER_SIZE/4)
    {
      fprintf (stderr, "User message too long\n");
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  char auth_hdr[2048];
  if (snprintf
      (auth_hdr, 2048, "Authorization: Bearer %s",api_key) >= 2048)
    {
      fprintf (stderr, "Auth header too long\n");
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
   char json_payload[BUFFER_SIZE];
    if (snprintf (json_payload, BUFFER_SIZE,
                "{\"model\": \"%s\",\"input\": \"%s\"}",
                api_model,user_msg)
                >= BUFFER_SIZE)
    {
      fprintf (stderr, "JSON payload too long\n");
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  char *curl_args[] = { "curl", "-s", "--max-time", "3600", api_url, "-H",
    "Content-Type: application/json", "-H", auth_hdr, "-d", json_payload, NULL
  };
  if (debug_mode)
    {
      printf ("Debug: URL: %s\n", api_url);
      printf ("Debug: Payload: %s\n", json_payload);
    }
  int pipe_fds[2];
  if (pipe (pipe_fds) == -1)
    {
      perror ("pipe");
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  __pid_t child_pid = fork ();
  if (child_pid == -1)
    {
      perror ("fork");
      close (pipe_fds[0]);
      close (pipe_fds[1]);
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  FILE *pipe_fp = NULL;
  if (child_pid == 0)
    {
      close (pipe_fds[0]);
      dup2 (pipe_fds[1], STDOUT_FILENO);
      close (pipe_fds[1]);
      execvp ("curl", curl_args);
      perror ("execvp");
      _exit (EXIT_FAILURE);
    }
  else
    {
      close (pipe_fds[1]);
      pipe_fp = fdopen (pipe_fds[0], "r");
      if (!pipe_fp)
        {
          perror ("fdopen");
          close (pipe_fds[0]);
          free (escaped_prompt);
          clear_sensitive_data ();
          return EXIT_FAILURE;
        }
    }
  // Clear screen and print prompt in blue
  printf ("\x1b[2J\x1b[H\x1b[34m");
  char *formatted_prompt = format_text_spacing (prompt);
  print_wrapped (formatted_prompt ? formatted_prompt : prompt, 75);
  printf ("\x1b[0m\n\n");
  free (formatted_prompt);
  char *response = malloc ((size_t) BUFFER_SIZE);
  if (!response)
    {
      fprintf (stderr, "Memory error\n");
      fclose (pipe_fp);
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  size_t bytes_read = fread (response, 1, BUFFER_SIZE - 1, pipe_fp);
  response[bytes_read] = '\0';
  fclose (pipe_fp);
  if (debug_mode)
    {
      printf ("Debug: Response: %s\n", response);
    }
  int status;
  __pid_t w = waitpid ((__pid_t)child_pid, &status, 0);
  if (w==-1) perror("child process failed");
  if (!WIFEXITED (status) || WEXITSTATUS (status) != 0)
    {
      fprintf (stderr, "curl failed\n");
      free (response);
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  char *content = extract_json_content (response);
  if (!content)
    {
      if (strstr (response, "\"error\""))
        {
          printf ("API error: %s\n", response);
        }
      else
        {
          printf ("No content in response.\n");
          printf
            ("Try using first name in FEED_USER or unset for anonymous.\n");
        }
      free (response);
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  extract_and_save_code_blocks (content, prompt);
  char *formatted_content = format_text_spacing (content);
  print_wrapped (formatted_content ? formatted_content : content, 75);
  int e  = putchar ('\n');
  if (e==EOF) perror("EOF unexpected");
  free (formatted_content);
  free (content);
  free (response);
  free (escaped_prompt);
  clear_sensitive_data ();
  return (int) EXIT_SUCCESS;
}
