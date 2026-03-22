#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#define BUFFER_SIZE (2 * 1024 * 1024)	// 2 MB
    /* Global config */
static char api_url[1024] = "";
static char api_key[1024] = "";
static char api_model[1024] = "grok-beta";
static bool debug_mode = false;
    /* Forward declarations */
extern char **environ;
    /* =================================================================== */
    /* Format text with two spaces after sentence-ending punctuation      */
    /* =================================================================== */
static char *
format_text_spacing (const char *text)
{
  if (!text || strlen (text) == 0)
    return strdup (text ? text : "");
  size_t len = strlen (text);
  char *result = malloc (len * 2 + 1);
  if (!result)
    return NULL;
  size_t j = 0;
  for (size_t i = 0; i < len; ++i)
    {
      result[j++] = text[i];
      if ((text[i] == '.' || text[i] == '?' || text[i] == '!') &&
	  i + 1 < len && text[i + 1] == ' ')
	{
	  if (i + 2 >= len || text[i + 2] != ' ')
	    {
	      result[j++] = ' ';	/* Add second space */
	    }
	}
    }
  result[j] = '\0';
  return result;
}
    /* =================================================================== */
    /* Extract and save code blocks from ``` markers                      */
    /* =================================================================== */
static void
extract_and_save_code_blocks (const char *content, const char *prompt)
{
  if (!content)
    return;
  const char *ptr = content;
  int block_count = 0;
  while ((ptr = strstr (ptr, "```")) != NULL)
    {
      ptr += 3;
      /* Get language hint */
      const char *lang_start = ptr;
      while (*ptr && *ptr != '\n')
	++ptr;
      size_t lang_len = (size_t) (ptr - lang_start);
      if (*ptr == '\n')
	++ptr;
      char lang[32] = "";
      if (lang_len > 0 && lang_len < sizeof (lang))
	{
	  memcpy (lang, lang_start, lang_len);
	  lang[lang_len] = '\0';
	}
      /* Find closing ``` */
      const char *code_start = ptr;
      const char *code_end = strstr (ptr, "```");
      if (!code_end)
	break;
      size_t code_len = (size_t) (code_end - code_start);
      char *code = malloc (code_len + 1);
      if (!code)
	break;
      memcpy (code, code_start, code_len);
      code[code_len] = '\0';
      /* Determine file extension */
      char ext[8] = ".txt";
      if (strcasestr (lang, "c") || strcasestr (lang, "cpp"))
	strcpy (ext, ".c");
      else if (strcasestr (lang, "python") || strcasestr (lang, "py"))
	strcpy (ext, ".py");
      else if (strcasestr (lang, "javascript") || strcasestr (lang, "js"))
	strcpy (ext, ".js");
      else if (strcasestr (lang, "rust"))
	strcpy (ext, ".rs");
      /* Try to extract filename from prompt ("save as ...") */
      char filename[256] = "";
      const char *save_pos = strcasestr (prompt, "save as");
      if (save_pos)
	{
	  save_pos += 7;	/* "save as" */
	  while (isspace ((unsigned char) *save_pos))
	    ++save_pos;
	  const char *name_end = strchr (save_pos, ' ');
	  if (!name_end)
	    name_end = save_pos + strlen (save_pos);
	  size_t name_len = (size_t) (name_end - save_pos);
	  if (name_len > 0 && name_len < sizeof (filename) - 10)
	    {
	      memcpy (filename, save_pos, name_len);
	      filename[name_len] = '\0';
	    }
	}
      if (strlen (filename) == 0)
	{
	  snprintf (filename, sizeof (filename), "code%s", ext);
	}
      else if (!strchr (filename, '.'))
	{
	  strncat (filename, ext, sizeof (filename) - strlen (filename) - 1);
	}
      /* Ensure unique filename */
      char unique_name[512];
      int counter = 1;
      strcpy (unique_name, filename);
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
      FILE *file = fopen (unique_name, "w");
      if (file)
	{
	  fputs (code, file);
	  fclose (file);
	  printf ("Saved code to %s\n", unique_name);
	}
      else
	{
	  fprintf (stderr, "Failed to save %s\n", unique_name);
	}
      free (code);
      ptr = code_end + 3;
      ++block_count;
    }
  if (block_count > 0)
    printf ("Extracted and saved %d code block(s).\n", block_count);
}
    /* =================================================================== */
    /* Very simple JSON content extractor                                  */
    /* =================================================================== */
static char *
extract_json_content (const char *json)
{
/* const char match[] = "\"object\":\"response\",\"output\":[{\"content\":[{\"type\":\"output_text\",\"text\"" */;
  if (json==NULL)
    return NULL;

  // Handle plain text errors (non-JSON)
  if (strstr(json, "{") == NULL) {
    printf("API plain error:\n%s\n", json);
    return NULL;
  }

  // Check for API error first
  const char *err_ptr = strstr(json, "\"error\"");
  if (err_ptr) {
    const char *msg_ptr = strstr(err_ptr, "\"message\"");
    if (msg_ptr) {
      msg_ptr = strchr(msg_ptr, ':') + 1;
      while (*msg_ptr && isspace((unsigned char)*msg_ptr)) ++msg_ptr;
      if (*msg_ptr == '"') ++msg_ptr;
      printf("API Error: ");
      size_t printed = 0;
      while (*msg_ptr && printed < 400 && *msg_ptr != '"') {
        if (*msg_ptr == '\\') {
          ++msg_ptr;
        }
        putchar(*msg_ptr);
        ++msg_ptr;
        printed++;
      }
      printf("\n");
    } else {
      printf("API error response (no message).\n");
    }
    return NULL;
  }

  const char *ptr = strstr (json, "\"content\"");
  if (!ptr)
    ptr = strstr (json, "\"text\"");
  if (!ptr) {
    fprintf(stderr, "No \"content\" or \"text\" field found in API response.\\n");
    if (debug_mode)
      printf("Response preview (first 900 chars):\\n%.900s\\n", json);
    return NULL;
  }
  ptr = strchr(ptr, ':');
  if (!ptr) return NULL;
  ++ptr;
  while (*ptr && isspace((unsigned char)*ptr)) ++ptr;
  if (*ptr != '"') return NULL;
  ++ptr;
  if (*ptr != '"')
    return NULL;
  ++ptr;
  char *content = malloc (BUFFER_SIZE);
  if (!content)
    return NULL;
  size_t idx = 0;
  while (*ptr && *ptr != '"' && idx < BUFFER_SIZE - 1)
    {
      if (*ptr == '\\')
	{
	  ++ptr;
	  switch (*ptr)
	    {
	    case 'n':
	      content[idx++] = '\n';
	      break;
	    case 'r':
	      content[idx++] = '\r';
	      break;
	    case 't':
	      content[idx++] = '\t';
	      break;
	    case '"':
	      content[idx++] = '"';
	      break;
	    case '\\':
	      content[idx++] = '\\';
	      break;
	    default:
	      content[idx++] = *ptr;
	    }
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
    /* =================================================================== */
    /* Print with word wrapping                                            */
    /* =================================================================== */
static void
print_wrapped (const char *text, int width)
{
  if (!text)
    return;
  const char *ptr = text;
  while (*ptr)
    {
      printf ("    ");
      while (*ptr && isspace ((unsigned char) *ptr) && *ptr != '\n')
	++ptr;
      const char *line_start = ptr;
      const char *last_space = NULL;
      int col = 0;
      while (*ptr && *ptr != '\n' && col < width)
	{
	  if (isspace ((unsigned char) *ptr))
	    last_space = ptr;
	  ++col;
	  ++ptr;
	}
      if (!*ptr || *ptr == '\n')
	{
	  fwrite (line_start, 1, (size_t) (ptr - line_start), stdout);
	  if (*ptr == '\n')
	    putchar ('\n');
	  if (*ptr)
	    ++ptr;
	}
      else if (last_space)
	{
	  fwrite (line_start, 1, (size_t) (last_space - line_start + 1),
		  stdout);
	  putchar ('\n');
	  ptr = last_space + 1;
	}
      else
	{
	  fwrite (line_start, 1, (size_t) width, stdout);
	  putchar ('\n');
	  ptr = line_start + width;
	}
    }
}
    /* =================================================================== */
    /* Load configuration from environment variables                       */
    /* =================================================================== */
static bool
load_config (void)
{
  for (char **env = environ; *env; ++env)
    {
      char *env_copy = strdup (*env);
      if (!env_copy)
	continue;
      char *key = strtok (env_copy, "=");
      char *value = strtok (NULL, "");
      if (key && value)
	{
	  if (strcmp (key, "FEED_URL") == 0)
	    strncpy (api_url, value, sizeof (api_url) - 1);
	  else if (strcmp (key, "FEED_KEY") == 0)
	    strncpy (api_key, value, sizeof (api_key) - 1);
	  else if (strcmp (key, "FEED_MODEL") == 0)
	    strncpy (api_model, value, sizeof (api_model) - 1);
	}
      free (env_copy);
    }
  return (api_url[0] && api_key[0] && api_model[0]);
}
static void
clear_sensitive_data (void)
{
  memset (api_key, 0, sizeof (api_key));
}
    /* =================================================================== */
    /* JSON string escaping                                                */
    /* =================================================================== */
static char *
escape_json_string (const char *str)
{
  if (!str)
    return NULL;
  size_t len = strlen (str);
  char *escaped = malloc (len * 2 + 1);
  if (!escaped)
    return NULL;
  size_t j = 0;
  for (size_t i = 0; i < len; ++i)
    {
      char c = str[i];
      switch (c)
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
	  escaped[j++] = c;
	}
    }
  escaped[j] = '\0';
  return escaped;
}
    /* =================================================================== */
    /* Main                                                            */
    /* =================================================================== */
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
      fprintf (stderr, "Error: Missing FEED_URL, FEED_KEY, or FEED_MODEL environment variables.\n");
      return EXIT_FAILURE;
    }
  char *escaped_prompt = escape_json_string (prompt);
  if (!escaped_prompt)
    {
      fprintf (stderr, "Memory allocation error\n");
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  char json_payload[BUFFER_SIZE];
  snprintf (json_payload, sizeof (json_payload), "{\"model\":\"%s\",\"input\":\"%s\"}", api_model, escaped_prompt);
// Payload length check removed (large buffer)
  if (strlen(json_payload) >= sizeof(json_payload) - 1) {
    fprintf(stderr, "Prompt too long\\n");
    free(escaped_prompt);
    clear_sensitive_data();
    return EXIT_FAILURE;
  }
    {
      fprintf (stderr, "Prompt too long\n");
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  char auth_hdr[2048];
  snprintf (auth_hdr, sizeof (auth_hdr), "Authorization: Bearer %s", api_key);
  if (debug_mode)
    {
      printf ("Debug: URL: %s\n", api_url);
      printf ("Debug: Payload: %s\n", json_payload);
    }
  /* Fork + exec curl */
  int pipe_fds[2];
  if (pipe (pipe_fds) == -1)
    {
      perror ("pipe");
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  pid_t child_pid = fork ();
  if (child_pid == -1)
    {
      perror ("fork");
      close (pipe_fds[0]);
      close (pipe_fds[1]);
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  if (child_pid == 0)
    {				/* Child */
      close (pipe_fds[0]);
      dup2 (pipe_fds[1], STDOUT_FILENO);
      close (pipe_fds[1]);
      char *curl_args[] = {
	"curl", "-s", "--max-time", "3600", api_url,
	"-H", "Content-Type: application/json",
	"-H", auth_hdr,
	"-d", json_payload, NULL
      };
      execvp ("curl", curl_args);
      perror ("execvp curl");
      _exit (EXIT_FAILURE);
    }
  /* Parent */
  close (pipe_fds[1]);
  FILE *pipe_fp = fdopen (pipe_fds[0], "r");
  if (!pipe_fp)
    {
      perror ("fdopen");
      close (pipe_fds[0]);
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  /* Print prompt in blue */
  printf ("\x1b[2J\x1b[H\x1b[34m");
  char *formatted_prompt = format_text_spacing (prompt);
  print_wrapped (formatted_prompt ? formatted_prompt : prompt, 75);
  printf ("\x1b[0m\n\n");
  free (formatted_prompt);
  char *response = malloc (BUFFER_SIZE);
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
    printf ("Debug: Response: %s\n", response);
  int status;
  waitpid (child_pid, &status, 0);
  if (!WIFEXITED (status) || WEXITSTATUS (status) != 0)
    {
      fprintf (stderr, "curl command failed\n");
      free (response);
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  char *content = extract_json_content (response);
  free (response);
  if (content==NULL)
    {
      printf ("No content in response or API error occurred.\n");
      free (escaped_prompt);
      clear_sensitive_data ();
      return EXIT_FAILURE;
    }
  extract_and_save_code_blocks (content, prompt);
  char *formatted_content = format_text_spacing (content);
  print_wrapped (formatted_content ? formatted_content : content, 75);
  putchar ('\n');
  free (formatted_content);
  free (content);
  free (escaped_prompt);
  clear_sensitive_data ();
  return EXIT_SUCCESS;
}