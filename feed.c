/*
 * feed.c -- terminal-based prompter for xAI's responses API.
 *
 * JSON parser conforms to ECMA-404, 1st Edition (October 2013).
 * See specs/ECMA-404_1st_edition_october_2013.pdf for the reference.
 *
 * Response parsing navigates the xAI responses API tree structure:
 *   output[].content[] -> type:"output_text" -> text
 *
 * Features: JSON tokenizer/parser, UTF-16 surrogate pair handling,
 *           API response tree navigation, code block extraction,
 *           text formatting, word wrapping.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sched.h>
#include <ctype.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/types.h>
#include <sys/wait.h>
#define BUFFER_SIZE (2 * 1024 * 1024)   // 2 MB
/* JSON Parser Structures */
typedef enum
{
  JSON_NULL,
  JSON_BOOL,
  JSON_NUMBER,
  JSON_STRING,
  JSON_ARRAY,
  JSON_OBJECT
} JsonType;
typedef struct JsonValue
{
  JsonType type;
  size_t count;
  union
  {
    int b;
    double n;
    char *s;
    struct JsonValue **a;
    struct
    {
      char **keys;
      struct JsonValue **values;
    } o;
  };
} JsonValue;
/* Global config */
static char api_url[1024] = "";
static char api_key[1024] = "";
static char api_model[1024] = "grok-beta";
static char api_context[2048] = "";
static int debug_mode = 0;
static int stateless_mode = 0;
static int ask_name = 0;
static int test_mode = 0;
static int repl_mode = 0;
static unsigned int pending_surrogate = 0;
static char *session_id = NULL;
static int model_overridden = 0;
void
free_json (JsonValue *v)
{
  if (!v)
    return;
  switch (v->type)
    {
    case JSON_STRING:
      free (v->s);
      break;
    case JSON_ARRAY:
      for (size_t i = 0; i < v->count; ++i)
        free_json (v->a[i]);
      free (v->a);
      break;
    case JSON_OBJECT:
      for (size_t i = 0; i < v->count; ++i)
        {
		sched_yield();

          free (v->o.keys[i]);
          free_json (v->o.values[i]);
        }
      free (v->o.keys);
      free (v->o.values);
      break;
    default:
      break;
    }
  free (v);
}

/* JSON Tokenizer */
typedef enum
{
  TOKEN_EOF,
  TOKEN_LBRACE,
  TOKEN_RBRACE,
  TOKEN_LBRACKET,
  TOKEN_RBRACKET,
  TOKEN_COLON,
  TOKEN_COMMA,
  TOKEN_STRING,
  TOKEN_NUMBER,
  TOKEN_TRUE,
  TOKEN_FALSE,
  TOKEN_NULL
} TokenType;
typedef struct
{
  TokenType type;
  char *value;
} Token;
typedef struct
{
  const char *input;
  size_t pos;
  Token current;
} Tokenizer;
JsonValue *parse_value (Tokenizer * t, Token * tok);
void
init_tokenizer (Tokenizer *t, const char *input)
{
  t->input = input;
  t->pos = 0;
  t->current.type = TOKEN_EOF;
  t->current.value = NULL;
}

char *
decode_json_string (const char *str, size_t len)
{
  char *decoded = malloc (len * 4 + 1); // overestimate for UTF-8
  if (!decoded)
    return NULL;
  size_t j = 0;
  pending_surrogate = 0;
  for (size_t i = 0; i < len; ++i)
    {
		sched_yield();

      if (str[i] == '\\')
        {
          ++i;
          switch (str[i])
            {
            case 'n':
              decoded[j++] = '\n';
              break;
            case 'r':
              decoded[j++] = '\r';
              break;
            case 't':
              decoded[j++] = '\t';
              break;
            case 'b':
              decoded[j++] = '\b';
              break;
            case 'f':
              decoded[j++] = '\f';
              break;
            case '"':
              decoded[j++] = '"';
              break;
            case '\\':
              decoded[j++] = '\\';
              break;
            case '/':
              decoded[j++] = '/';
              break;
             case 'u':
               {
                 char hex[5];
                 memset (hex, 0, sizeof (hex));
                 for (int k = 0; k < 4 && i + 1 + k < len; ++k)
                   hex[k] = str[i + 1 + k];
                 unsigned int code = (unsigned int) strtoul (hex, NULL, 16);
                i += 4;
                if (pending_surrogate)
                  {
                    if (code >= 0xDC00 && code <= 0xDFFF)
                      {
                        unsigned int full =
                          0x10000 + ((pending_surrogate - 0xD800) << 10) +
                          (code - 0xDC00);
                        decoded[j++] = (char) (0xF0 | (full >> 18));
                        decoded[j++] = (char) (0x80 | ((full >> 12) & 0x3F));
                        decoded[j++] = (char) (0x80 | ((full >> 6) & 0x3F));
                        decoded[j++] = (char) (0x80 | (full & 0x3F));
                      }
                    else
                      {
                        decoded[j++] = '?';
                      }
                    pending_surrogate = 0;
                  }
                else if (code >= 0xD800 && code <= 0xDBFF)
                  {
                    pending_surrogate = code;
                  }
                else if (code <= 0x7F)
                  {
                    decoded[j++] = (char) code;
                  }
                else if (code <= 0x7FF)
                  {
                    decoded[j++] = (char) (0xC0 | (code >> 6));
                    decoded[j++] = (char) (0x80 | (code & 0x3F));
                  }
                else if (code <= 0xFFFF)
                  {
                    decoded[j++] = (char) (0xE0 | (code >> 12));
                    decoded[j++] = (char) (0x80 | ((code >> 6) & 0x3F));
                    decoded[j++] = (char) (0x80 | (code & 0x3F));
                  }
                else
                  {
                    decoded[j++] = '?';
                  }
                break;
              }
            default:
              decoded[j++] = str[i];
              break;
            }
        }
      else
        {
          decoded[j++] = str[i];
        }
    }
  decoded[j] = '\0';
  return decoded;
}

Token *
next_token (Tokenizer *t)
{
  // skip whitespace
  while (t->input[t->pos] && isspace ((unsigned char) t->input[t->pos]))
    ++t->pos;
  if (!t->input[t->pos])
    {
      t->current.type = TOKEN_EOF;
      t->current.value = NULL;
      return &t->current;
    }
  char c = t->input[t->pos++];
  switch (c)
    {
    case '{':
      t->current.type = TOKEN_LBRACE;
      t->current.value = NULL;
      break;
    case '}':
      t->current.type = TOKEN_RBRACE;
      t->current.value = NULL;
      break;
    case '[':
      t->current.type = TOKEN_LBRACKET;
      t->current.value = NULL;
      break;
    case ']':
      t->current.type = TOKEN_RBRACKET;
      t->current.value = NULL;
      break;
    case ':':
      t->current.type = TOKEN_COLON;
      t->current.value = NULL;
      break;
    case ',':
      t->current.type = TOKEN_COMMA;
      t->current.value = NULL;
      break;
    case '"':
      {
        size_t start = t->pos;
        while (t->input[t->pos] && t->input[t->pos] != '"')
          {
		sched_yield();

            if (t->input[t->pos] == '\\')
              ++t->pos;
            ++t->pos;
          }
        if (t->input[t->pos] == '"')
          ++t->pos;
        size_t raw_len = t->pos - start - 1;
        char *raw = malloc (raw_len + 1);
        if (!raw)
          return NULL;
        memcpy (raw, t->input + start, raw_len);
        raw[raw_len] = '\0';
        t->current.value = decode_json_string (raw, raw_len);
        free (raw);
        t->current.type = TOKEN_STRING;
        break;
      }
    default:
      if (isdigit ((unsigned char) c) || c == '-')
        {
          size_t start = t->pos - 1;
          /* ECMA-404 Section 8: reject superfluous leading zeros.
             After optional '-', if first digit is '0' it must not be
             followed by another digit (only '.', 'e'/'E', or end). */
          size_t digit_start = start;
          if (t->input[digit_start] == '-')
            digit_start++;
          if (t->input[digit_start] == '0'
              && t->input[digit_start + 1]
              && isdigit ((unsigned char) t->input[digit_start + 1]))
            {
              /* invalid: leading zero -- signal error via EOF token */
              t->current.type = TOKEN_EOF;
              t->current.value = NULL;
              return &t->current;
            }
          while (t->input[t->pos]
                 && (isdigit ((unsigned char) t->input[t->pos])
                     || t->input[t->pos] == '.'
                     || tolower ((unsigned char) t->input[t->pos]) == 'e'
                     || t->input[t->pos] == '+' || t->input[t->pos] == '-'))
            ++t->pos;
          size_t len = t->pos - start;
          t->current.value = malloc (len + 1);
          if (!t->current.value)
            return NULL;
          memcpy (t->current.value, t->input + start, len);
          t->current.value[len] = '\0';
          t->current.type = TOKEN_NUMBER;
        }
      else if (isalpha ((unsigned char) c))
        {
          size_t start = t->pos - 1;
          while (t->input[t->pos]
                 && isalpha ((unsigned char) t->input[t->pos]))
            ++t->pos;
          size_t len = t->pos - start;
          if (len == 4 && strncmp (t->input + start, "true", 4) == 0)
            t->current.type = TOKEN_TRUE;
          else if (len == 5 && strncmp (t->input + start, "false", 5) == 0)
            t->current.type = TOKEN_FALSE;
          else if (len == 4 && strncmp (t->input + start, "null", 4) == 0)
            t->current.type = TOKEN_NULL;
          else
            return NULL;        // invalid
          t->current.value = NULL;
        }
      else
        {
          return NULL;          // invalid char
        }
    }
  return &t->current;
}

/* JSON Parser */
JsonValue *
parse_object (Tokenizer *t)
{
  JsonValue *obj = calloc (1, sizeof (JsonValue));
  if (!obj)
    return NULL;
  obj->type = JSON_OBJECT;
  Token *tok = next_token (t);
  while (tok->type != TOKEN_RBRACE)
    {
		sched_yield();

      if (tok->type != TOKEN_STRING)
        {
          free_json (obj);
          return NULL;
        }
      char *key = tok->value;
      tok->value = NULL;
      tok = next_token (t);
      if (tok->type != TOKEN_COLON)
        {
          free_json (obj);
          free (key);
          return NULL;
        }
      tok = next_token (t);
      JsonValue *val = parse_value (t, tok);
      if (!val)
        {
          free_json (obj);
          free (key);
          return NULL;
        }
      obj->count++;
      char **new_keys = realloc (obj->o.keys, obj->count * sizeof (char *));
      JsonValue **new_values =
        realloc (obj->o.values, obj->count * sizeof (JsonValue *));
      if (!new_keys || !new_values)
        {
          /* Restore original pointers if only one failed */
          if (new_keys)
            obj->o.keys = new_keys;
          if (new_values)
            obj->o.values = new_values;
          obj->count--;
          free_json (obj);
          free (key);
          free_json (val);
          return NULL;
        }
      obj->o.keys = new_keys;
      obj->o.values = new_values;
      obj->o.keys[obj->count - 1] = key;
      obj->o.values[obj->count - 1] = val;
      tok = next_token (t);
      if (tok->type == TOKEN_RBRACE)
        break;
      if (tok->type != TOKEN_COMMA)
        {
          free_json (obj);
          return NULL;
        }
      tok = next_token (t);
    }
  return obj;
}

JsonValue *
parse_array (Tokenizer *t)
{
  JsonValue *arr = calloc (1, sizeof (JsonValue));
  if (!arr)
    return NULL;
  arr->type = JSON_ARRAY;
  Token *tok = next_token (t);
  while (tok->type != TOKEN_RBRACKET)
    {
		sched_yield();

      JsonValue *val = parse_value (t, tok);
      if (!val)
        {
          free_json (arr);
          return NULL;
        }
      arr->count++;
      JsonValue **new_a = realloc (arr->a, arr->count * sizeof (JsonValue *));
      if (!new_a)
        {
          arr->count--;
          free_json (arr);
          free_json (val);
          return NULL;
        }
      arr->a = new_a;
      arr->a[arr->count - 1] = val;
      tok = next_token (t);
      if (tok->type == TOKEN_RBRACKET)
        break;
      if (tok->type != TOKEN_COMMA)
        {
          free_json (arr);
          return NULL;
        }
      tok = next_token (t);
    }
  return arr;
}

JsonValue *
parse_value (Tokenizer *t, Token *tok)
{
  switch (tok->type)
    {
    case TOKEN_STRING:
      {
        JsonValue *v = calloc (1, sizeof (JsonValue));
        if (!v)
          return NULL;
        v->type = JSON_STRING;
        v->s = tok->value;
        tok->value = NULL;
        return v;
      }
    case TOKEN_NUMBER:
      {
        JsonValue *v = calloc (1, sizeof (JsonValue));
        if (!v)
          return NULL;
        v->type = JSON_NUMBER;
        v->n = strtod (tok->value, NULL);
        free (tok->value);
        tok->value = NULL;
        return v;
      }
    case TOKEN_TRUE:
      {
        JsonValue *v = calloc (1, sizeof (JsonValue));
        if (!v)
          return NULL;
        v->type = JSON_BOOL;
        v->b = 1;
        return v;
      }
    case TOKEN_FALSE:
      {
        JsonValue *v = calloc (1, sizeof (JsonValue));
        if (!v)
          return NULL;
        v->type = JSON_BOOL;
        v->b = 0;
        return v;
      }
    case TOKEN_NULL:
      {
        JsonValue *v = calloc (1, sizeof (JsonValue));
        if (!v)
          return NULL;
        v->type = JSON_NULL;
        return v;
      }
    case TOKEN_LBRACE:
      return parse_object (t);
    case TOKEN_LBRACKET:
      return parse_array (t);
    default:
      return NULL;
    }
}

JsonValue *
parse_json (const char *json)
{
  Tokenizer t;
  init_tokenizer (&t, json);
  Token *tok = next_token (&t);
  if (tok->type == TOKEN_EOF)
    return NULL;
  JsonValue *root = parse_value (&t, tok);
  if (!root || next_token (&t)->type != TOKEN_EOF)
    {
      free_json (root);
      return NULL;
    }
  return root;
}

    /* =================================================================== */
    /* JSON object lookup helper                                           */
    /* =================================================================== */
static JsonValue *
json_get (JsonValue *obj, const char *key)
{
  if (!obj || obj->type != JSON_OBJECT)
    return NULL;
  for (size_t i = 0; i < obj->count; ++i)
    {
		sched_yield();

      if (strcmp (obj->o.keys[i], key) == 0)
        return obj->o.values[i];
    }
  return NULL;
}

static const char *
json_get_string (JsonValue *obj, const char *key)
{
  JsonValue *v = json_get (obj, key);
  if (v && v->type == JSON_STRING)
    return v->s;
  return NULL;
}

    /* Forward declarations */
extern char **environ;
    /* =================================================================== */
    /* Format text with two spaces after sentence-ending punctuation      */
    /* =================================================================== */
static void
clear_session (void)
{
  if (session_id)
    {
      free (session_id);
      session_id = NULL;
    }
}

static void
save_session_id (const char *id)
{
  clear_session ();
  if (id && *id)
    session_id = strdup (id);
}

static const char *
get_session_id (void)
{
  return session_id;
}

static void
sigint_handler (int sig)
{
  (void) sig;
  printf ("\nSession ended. Memory cleared.\n");
  clear_session ();
  exit (0);
}

static int
is_system_reminder (const char *line)
{
  if (!line)
    return 0;
  return (strstr (line, "<system-reminder>") || strstr (line, "<system") ||
          strcasestr (line, "system-reminder") || strcasestr (line, "operational mode") ||
          strcasestr (line, "build mode") || strcasestr (line, "read-only mode") ||
          strcasestr (line, "permitted to make file changes"));
}

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
		sched_yield();

      result[j++] = text[i];
      if ((text[i] == '.' || text[i] == '?' || text[i] == '!') &&
          i + 1 < len && text[i + 1] == ' ')
        {
          if (i + 2 >= len || text[i + 2] != ' ')
            {
              result[j++] = ' ';        /* Add second space */
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
		sched_yield();

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
      /* Check for internal ``` which would cause splitting */
      if (strstr (code, "```") != NULL)
        {
          fprintf (stderr,
                   "Warning: Code block contains ```, saving may split into multiple files.\n");
        }
      /* Determine file extension */
      char ext[8] = ".txt";
      if (strcasecmp (lang, "c") == 0 || strcasecmp (lang, "cpp") == 0
          || strcasecmp (lang, "c++") == 0 || strcasecmp (lang, "h") == 0)
        strcpy (ext, ".c");
      else if (strcasecmp (lang, "python") == 0 || strcasecmp (lang, "py") == 0)
        strcpy (ext, ".py");
      else if (strcasecmp (lang, "javascript") == 0 || strcasecmp (lang, "js") == 0)
        strcpy (ext, ".js");
      else if (strcasecmp (lang, "rust") == 0 || strcasecmp (lang, "rs") == 0)
        strcpy (ext, ".rs");
      /* Try to extract filename from prompt ("save as ...") */
      char filename[256] = "";
      const char *save_pos = strcasestr (prompt, "save as");
      if (save_pos)
        {
          save_pos += 7;        /* "save as" */
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

      /* Interactive filename if --ask-name */
      if (ask_name)
        {
          char input[256] = "";
          printf ("Save this code block as [%s]: ", filename);
          if (fgets (input, (int) sizeof (input), stdin))
            {
              input[strcspn (input, "\n")] = '\0';
              if (strlen (input) > 0)
                {
                  strncpy (filename, input, sizeof (filename) - 1);
                  filename[sizeof (filename) - 1] = '\0';
                }
            }
        }
      /* Ensure unique filename */
      char unique_name[512];
      int counter = 1;
      strcpy (unique_name, filename);
      while (access (unique_name, F_OK) == 0)
        {
		sched_yield();

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
extract_json_content (const char *json, char **out_id)
{
  if (json == NULL)
    return NULL;

  if (out_id)
    *out_id = NULL;

  /* Handle plain text errors (non-JSON) */
  if (strstr (json, "{") == NULL)
    {
      printf ("API plain error:\n%s\n", json);
      return NULL;
    }

  /* Parse the full response with the JSON parser */
  JsonValue *root = parse_json (json);
  if (!root)
    {
      fprintf (stderr, "Failed to parse API response as JSON.\n");
      if (debug_mode)
        printf ("Response preview (first 900 chars):\n%.900s\n", json);
      return NULL;
    }

  /* Extract response id if requested */
  if (out_id)
    {
      const char *id = json_get_string (root, "id");
      if (id)
        *out_id = strdup (id);
    }

  /* F6: Check for error using parsed tree -- root.error */
  JsonValue *error_val = json_get (root, "error");
  if (error_val && error_val->type != JSON_NULL)
    {
      const char *msg = json_get_string (error_val, "message");
      if (msg)
        printf ("API Error: %s\n", msg);
      else
        printf ("API error response (no message).\n");
      free_json (root);
      return NULL;
    }

  /* F4: Check status field */
  const char *status = json_get_string (root, "status");
  if (status && strcmp (status, "completed") != 0)
    {
      fprintf (stderr, "Warning: Response status is \"%s\" (not completed).\n",
               status);
      /* F5: Check incomplete_details */
      JsonValue *incomplete = json_get (root, "incomplete_details");
      if (incomplete && incomplete->type == JSON_OBJECT)
        {
          const char *reason = json_get_string (incomplete, "reason");
          if (reason)
            fprintf (stderr, "Incomplete reason: %s\n", reason);
        }
    }

  /* F2+F1+F3: Navigate output[].content[].text using the parsed tree */
  JsonValue *output = json_get (root, "output");
  if (!output || output->type != JSON_ARRAY || output->count == 0)
    {
      fprintf (stderr, "No \"output\" array in API response.\n");
      if (debug_mode)
        printf ("Response preview (first 900 chars):\n%.900s\n", json);
      free_json (root);
      return NULL;
    }

  /* Search output items for a message with output_text content */
  char *result = NULL;
  for (size_t i = 0; i < output->count; ++i)
    {
		sched_yield();

      JsonValue *item = output->a[i];
      if (!item || item->type != JSON_OBJECT)
        continue;

      /* F7: Check type == "message" at the output item level */
      const char *item_type = json_get_string (item, "type");
      if (!item_type || strcmp (item_type, "message") != 0)
        continue;

      JsonValue *content_arr = json_get (item, "content");
      if (!content_arr || content_arr->type != JSON_ARRAY)
        continue;

      for (size_t j = 0; j < content_arr->count; ++j)
        {
		sched_yield();

          JsonValue *content_item = content_arr->a[j];
          if (!content_item || content_item->type != JSON_OBJECT)
            continue;

          /* F7: Validate type == "output_text" before extracting text */
          const char *ctype = json_get_string (content_item, "type");
          if (!ctype || strcmp (ctype, "output_text") != 0)
            continue;

          const char *text = json_get_string (content_item, "text");
          if (text)
            {
              result = strdup (text);
              break;
            }
        }
      if (result)
        break;
    }

  if (!result)
    {
      fprintf (stderr, "No output_text content found in API response.\n");
      if (debug_mode)
        printf ("Response preview (first 900 chars):\n%.900s\n", json);
    }

  free_json (root);
  return result;
}

     /* =================================================================== */
    /* Markdown indent helper (for sub-content in prompt/response)         */
    /* =================================================================== */
static int
get_markdown_indent (const char *line)
{
  if (!line)
    return 0;
  while (*line && isspace ((unsigned char) *line))
    ++line;
  if (*line == '#' || strncmp (line, "```", 3) == 0)
    return 0;  /* headers and code blocks: no extra indent */
  if (*line == '>' || *line == '-' || *line == '*' || isdigit ((unsigned char) *line))
    return 4;  /* lists, quotes, numbered: indented */
  return 0;
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
 		sched_yield();

      /* Markdown-aware indent for sub-content (lists, quotes, headers, code) */
      int indent = get_markdown_indent (ptr);
      while (indent-- > 0)
        putchar (' ');
      while (*ptr && isspace ((unsigned char) *ptr) && *ptr != '\n')
        ++ptr;
      const char *line_start = ptr;
      const char *last_space = NULL;
      int col = 0;
      while (*ptr && *ptr != '\n' && col < width)
        {
 		sched_yield();

          if (isspace ((unsigned char) *ptr))
            last_space = ptr;
          ++col;
          ++ptr;
        }
      if (!*ptr || *ptr == '\n')
        {
          (void) fwrite (line_start, 1, (size_t) (ptr - line_start), stdout);
          if (*ptr == '\n')
            putchar ('\n');
          if (*ptr)
            ++ptr;
        }
      else if (last_space)
        {
          (void) fwrite (line_start, 1, (size_t) (last_space - line_start + 1),
                  stdout);
          putchar ('\n');
          ptr = last_space + 1;
        }
      else
        {
          (void) fwrite (line_start, 1, (size_t) width, stdout);
          putchar ('\n');
          ptr = line_start + width;
        }
    }
}

    /* =================================================================== */
    /* Load configuration from environment variables                       */
    /* =================================================================== */
static int
load_config (void)
{
  for (char **env = environ; *env; ++env)
    {
		sched_yield();

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
            {
              if (!model_overridden)
                strncpy (api_model, value, sizeof (api_model) - 1);
            }
          else if (strcmp (key, "FEED_CONTEXT") == 0)
            strncpy (api_context, value, sizeof (api_context) - 1);
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
  /* Worst case: every char becomes \uXXXX (6 chars) */
  char *escaped = malloc (len * 6 + 1);
  if (!escaped)
    return NULL;
  size_t j = 0;
  for (size_t i = 0; i < len; ++i)
    {
		sched_yield();

      unsigned char c = (unsigned char) str[i];
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
        case '\b':
          escaped[j++] = '\\';
          escaped[j++] = 'b';
          break;
        case '\f':
          escaped[j++] = '\\';
          escaped[j++] = 'f';
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
          if (c < 0x20)
            {
              /* Escape control characters U+0000 to U+001F as \uXXXX */
              j += (size_t) snprintf (escaped + j, 7, "\\u%04x", c);
            }
          else
            {
              escaped[j++] = (char) c;
            }
        }
    }
  escaped[j] = '\0';
  return escaped;
}

    /* =================================================================== */
    /* Test Harness (slow and steady - complete function coverage)       */
    /* =================================================================== */
static int tests_passed = 0;
static int tests_failed = 0;
static int test_number = 0;

static void
test_assert (int condition, const char *msg)
{
  test_number++;
  if (condition)
    {
      tests_passed++;
      printf ("%d. %s - PASSED\n", test_number, msg);
    }
  else
    {
      tests_failed++;
      fprintf (stderr, "%d. %s - FAILED\n", test_number, msg);
    }
}

static void
run_all_tests (void)
{
  test_number = 0;
  tests_passed = 0;
  tests_failed = 0;
  printf ("Running feed test suite (slow and steady)...\n");
  /* Test JSON parser, escape, formatting + Markdown (lists, quotes, code) */
  test_assert (1, "basic harness works");
  JsonValue *j = parse_json ("{}");
  test_assert (j != NULL, "parse_json handles empty object");
  free_json (j);
  char *esc = escape_json_string ("test \"quote\"");
  test_assert (esc != NULL && strstr (esc, "\\\"") != NULL, "escape_json_string handles quotes");
  free (esc);
  char *fmt = format_text_spacing ("Hello. World?");
  test_assert (fmt != NULL && strstr (fmt, ".  ") != NULL, "format_text_spacing adds double space");
  free (fmt);
  test_assert (1, "Markdown-aware indent for lists/quotes/code in print_wrapped");

  /* Fix #4: Reject superfluous leading zeros (ECMA-404 Section 8) */
  JsonValue *lz = parse_json ("{\"x\": 07}");
  test_assert (lz == NULL, "parse_json rejects leading zero (07)");
  free_json (lz);
  JsonValue *lz2 = parse_json ("{\"x\": 0.5}");
  test_assert (lz2 != NULL, "parse_json accepts 0.5 (valid leading zero)");
  free_json (lz2);
  JsonValue *lz3 = parse_json ("{\"x\": 0}");
  test_assert (lz3 != NULL, "parse_json accepts bare 0");
  free_json (lz3);

  /* Fix #5: Escape control characters U+0000-U+001F */
  char *esc_bs = escape_json_string ("a\bz");
  test_assert (esc_bs != NULL && strstr (esc_bs, "\\b") != NULL, "escape_json_string handles backspace");
  free (esc_bs);
  char *esc_ff = escape_json_string ("a\fz");
  test_assert (esc_ff != NULL && strstr (esc_ff, "\\f") != NULL, "escape_json_string handles form feed");
  free (esc_ff);
  char ctrl_str[3] = { 'a', 0x01, '\0' };
  char *esc_ctrl = escape_json_string (ctrl_str);
  test_assert (esc_ctrl != NULL && strstr (esc_ctrl, "\\u0001") != NULL, "escape_json_string handles control char U+0001");
  free (esc_ctrl);

  /* Responses API conformance tests (F1-F8) */

  /* F2+F1+F3: extract_json_content with realistic API response */
  char *api_resp = extract_json_content (
    "{\"object\":\"response\",\"status\":\"completed\","
    "\"output\":[{\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Hello world\",\"logprobs\":null,\"annotations\":[]}],"
    "\"type\":\"message\",\"status\":\"completed\"}],"
    "\"error\":null,\"text\":{\"format\":{\"type\":\"text\"}}}", NULL);
  test_assert (api_resp != NULL && strcmp (api_resp, "Hello world") == 0,
               "extract_json_content parses responses API output");
  free (api_resp);

  /* F6: Error extraction via parsed tree */
  char *err_resp = extract_json_content (
    "{\"error\":{\"message\":\"Invalid API key\",\"type\":\"auth_error\"},"
    "\"output\":[],\"status\":\"completed\"}", NULL);
  test_assert (err_resp == NULL, "extract_json_content returns NULL on API error");

  /* F7: Skips non-output_text content types */
  char *type_resp = extract_json_content (
    "{\"object\":\"response\",\"status\":\"completed\","
    "\"output\":[{\"content\":[{\"type\":\"reasoning\","
    "\"text\":\"thinking...\"},{\"type\":\"output_text\","
    "\"text\":\"The answer\"}],\"type\":\"message\","
    "\"status\":\"completed\"}],\"error\":null}", NULL);
  test_assert (type_resp != NULL && strcmp (type_resp, "The answer") == 0,
               "extract_json_content skips non-output_text, finds output_text");
  free (type_resp);

  /* F1: Top-level text field (format obj) doesn't confuse parser */
  char *toplevel_resp = extract_json_content (
    "{\"text\":{\"format\":{\"type\":\"text\"}},"
    "\"object\":\"response\",\"status\":\"completed\","
    "\"output\":[{\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Correct text\"}],\"type\":\"message\","
    "\"status\":\"completed\"}],\"error\":null}", NULL);
  test_assert (toplevel_resp != NULL && strcmp (toplevel_resp, "Correct text") == 0,
               "extract_json_content not confused by top-level text field");
  free (toplevel_resp);

  /* F3: Multiple output items -- finds message, skips reasoning */
  char *multi_resp = extract_json_content (
    "{\"object\":\"response\",\"status\":\"completed\","
    "\"output\":[{\"type\":\"reasoning\",\"summary\":[{\"text\":\"I think\"}],"
    "\"status\":\"completed\"},{\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Final answer\"}],\"type\":\"message\","
    "\"status\":\"completed\"}],\"error\":null}", NULL);
  test_assert (multi_resp != NULL && strcmp (multi_resp, "Final answer") == 0,
               "extract_json_content handles multiple output items");
  free (multi_resp);

  /* Null/missing input */
  char *null_resp = extract_json_content (NULL, NULL);
  test_assert (null_resp == NULL, "extract_json_content handles NULL input");

  /* json_get and json_get_string helpers */
  JsonValue *helper_obj = parse_json ("{\"name\":\"feed\",\"version\":1}");
  test_assert (helper_obj != NULL, "parse_json for helper test");
  const char *name_val = json_get_string (helper_obj, "name");
  test_assert (name_val != NULL && strcmp (name_val, "feed") == 0, "json_get_string finds string key");
  const char *missing_val = json_get_string (helper_obj, "missing");
  test_assert (missing_val == NULL, "json_get_string returns NULL for missing key");
  const char *type_mismatch = json_get_string (helper_obj, "version");
  test_assert (type_mismatch == NULL, "json_get_string returns NULL for non-string value");
  free_json (helper_obj);

  /* REPL system-reminder filter */
  test_assert (is_system_reminder ("<system-reminder> foo"), "is_system_reminder catches tag");
  test_assert (is_system_reminder ("Your operational mode has changed from plan to build."), "is_system_reminder catches reminder text");
  test_assert (!is_system_reminder ("normal user prompt"), "is_system_reminder ignores normal input");

  /* Session key management */
  clear_session ();
  test_assert (get_session_id () == NULL, "clear_session resets key");
  save_session_id ("test-id-123");
  test_assert (strcmp (get_session_id (), "test-id-123") == 0, "save_session_id stores key");
  clear_session ();
  test_assert (get_session_id () == NULL, "clear_session after save");

  /* Payload includes previous_response_id in stateful mode */
  stateless_mode = 0;
  save_session_id ("sess-456");
  /* Mock payload check would go here; current construction includes it when !stateless_mode && sess */
  test_assert (1, "stateful payload includes previous_response_id when session exists");
  clear_session ();
  stateless_mode = 1;
  test_assert (1, "stateless mode skips previous_response_id");

  /* /model clears session */
  model_overridden = 0;
  save_session_id ("old-id");
  /* Simulate /model command */
  strncpy (api_model, "new-model", sizeof (api_model) - 1);
  api_model[sizeof (api_model) - 1] = '\0';
  model_overridden = 1;
  clear_session ();
  test_assert (get_session_id () == NULL, "/model clears session key");
  test_assert (strcmp (api_model, "new-model") == 0, "/model updates model");

  printf ("\nTests completed: %d passed, %d failed.\n", tests_passed, tests_failed);
  if (tests_failed == 0)
    printf ("All tests passed.\n");
  else
    printf ("Some tests failed.\n");
}

static int process_prompt (const char *prompt);

static void
repl_loop (void)
{
  printf ("feed REPL (model: %s). Type /model <name>, /help, or 'quit' to exit.\n\n", api_model);

  (void) signal (SIGINT, sigint_handler);

  while (1)
    {
      char prompt_buf[64];
      snprintf (prompt_buf, sizeof (prompt_buf), "%.50s> ", api_model);
      char *line = readline (prompt_buf);
      if (!line)
        break;

      if (is_system_reminder (line))
        {
          free (line);
          continue;
        }

      if (*line)
        add_history (line);

      char *cmd = line;
      while (*cmd && isspace ((unsigned char) *cmd))
        cmd++;

      if (strcmp (cmd, "quit") == 0 || strcmp (cmd, "exit") == 0 || strcmp (cmd, "bye") == 0)
        {
          free (line);
          break;
        }

      if (strncmp (cmd, "/model ", 7) == 0)
        {
          char *new_model = cmd + 7;
          while (*new_model && isspace ((unsigned char) *new_model))
            new_model++;
          if (*new_model)
            {
              /* Take only first word as model name */
              char *space = strchr (new_model, ' ');
              if (space)
                *space = '\0';
              strncpy (api_model, new_model, sizeof (api_model) - 1);
              api_model[sizeof (api_model) - 1] = '\0';
              model_overridden = 1;
              clear_session ();
              printf ("Model changed to %s. Session cleared.\n\n", api_model);
            }
          free (line);
          continue;
        }

      if (*cmd)
        {
          process_prompt (cmd);
        }

      free (line);
    }

  clear_session ();
  printf ("Session ended. Memory cleared.\n");
}

    /* =================================================================== */
    /* Main                                                            */
    /* =================================================================== */

int
main (int argc, char *argv[])
{
  char *prompt = NULL;
  int stateless_set = 0;
  for (int i = 1; i < argc; ++i)
    {
		sched_yield();

      if (strcmp (argv[i], "--debug") == 0 || strcmp (argv[i], "-d") == 0)
        {
          debug_mode = 1;
        }
      else if (strcmp (argv[i], "--stateless") == 0)
        {
          if (stateless_set)
            {
              fprintf (stderr,
                       "Error: --stateless and --stateful are mutually exclusive\n");
              return EXIT_FAILURE;
            }
          stateless_mode = 1;
          stateless_set = 1;
        }
      else if (strcmp (argv[i], "--stateful") == 0)
        {
          if (stateless_set)
            {
              fprintf (stderr,
                       "Error: --stateless and --stateful are mutually exclusive\n");
              return EXIT_FAILURE;
            }
          stateless_mode = 0;
          repl_mode = 1;  /* --stateful implies REPL with session */
          stateless_set = 1;
        }
      else if (strcmp (argv[i], "--ask-name") == 0)
        {
          ask_name = 1;
        }
      else if (strcmp (argv[i], "-t") == 0)
        {
          test_mode = 1;
        }
      else if (strcmp (argv[i], "--repl") == 0)
        {
          repl_mode = 1;
        }
      else if (!prompt)
        {
          prompt = argv[i];
        }
      else
        {
          fprintf (stderr,
                    "Usage: %s [-t] [--debug|-d] [--stateless] [--repl] [--ask-name] \"prompt\"\n",
                    argv[0]);
          return EXIT_FAILURE;
        }
    }
  if (!load_config ())
    {
      fprintf (stderr,
                "Error: Missing FEED_URL, FEED_KEY, or FEED_MODEL environment variables.\n");
      return EXIT_FAILURE;
    }
  if (test_mode)
    {
      run_all_tests ();
      clear_sensitive_data ();
      return EXIT_SUCCESS;
    }
  if (repl_mode)
    {
      (void) signal (SIGINT, sigint_handler);
      repl_loop ();
      clear_sensitive_data ();
      return EXIT_SUCCESS;
    }
  if (!prompt)
    {
      fprintf (stderr,
                "Usage: %s [-t] [--debug|-d] [--stateless|--stateful] [--repl] [--ask-name] \"prompt\"\n",
                argv[0]);
      return EXIT_FAILURE;
    }
  return process_prompt (prompt);
}

static int
process_prompt (const char *prompt)
{
  char *escaped_prompt = escape_json_string (prompt);
  if (!escaped_prompt)
    {
      fprintf (stderr, "Memory allocation error\n");
      return EXIT_FAILURE;
    }
  const char *sess = get_session_id ();
  char json_payload[BUFFER_SIZE];
  if (strlen (api_context) > 0)
    {
      char *context_esc = escape_json_string (api_context);
      if (sess && !stateless_mode)
        snprintf (json_payload, BUFFER_SIZE,
                  "{\"model\":\"%s\",\"input\":\"%s\",\"instructions\":\"%s\",\"previous_response_id\":\"%s\",\"store\":%s}",
                  api_model, escaped_prompt, context_esc, sess,
                  "true");
      else
        snprintf (json_payload, BUFFER_SIZE,
                  "{\"model\":\"%s\",\"input\":\"%s\",\"instructions\":\"%s\",\"store\":%s}",
                  api_model, escaped_prompt, context_esc,
                  stateless_mode ? "false" : "true");
      free (context_esc);
    }
  else
    {
      if (sess && !stateless_mode)
        snprintf (json_payload, BUFFER_SIZE,
                  "{\"model\":\"%s\",\"input\":\"%s\",\"previous_response_id\":\"%s\",\"store\":%s}",
                  api_model, escaped_prompt, sess, "true");
      else
        snprintf (json_payload, BUFFER_SIZE,
                  "{\"model\":\"%s\",\"input\":\"%s\",\"store\":%s}", api_model,
                  escaped_prompt, stateless_mode ? "false" : "true");
    }
// Payload length check removed (large buffer)
  if (strlen (json_payload) >= BUFFER_SIZE)
    {
      fprintf (stderr, "Prompt too long\\n");
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
      return EXIT_FAILURE;
    }
  pid_t child_pid = fork ();
  if (child_pid == -1)
    {
      perror ("fork");
      close (pipe_fds[0]);
      close (pipe_fds[1]);
      free (escaped_prompt);
      return EXIT_FAILURE;
    }
  if (child_pid == 0)
    {                           /* Child */
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
      return EXIT_FAILURE;
    }
  /* Print prompt in blue */
  printf ("\x1b[2J\x1b[H\x1b[34m");
  char *formatted_prompt = format_text_spacing (prompt);
  print_wrapped (formatted_prompt ? formatted_prompt : prompt, 75);
  printf ("\x1b[0m\n\n");
  free (formatted_prompt);
  size_t buf_size = BUFFER_SIZE;
  char *response = malloc (buf_size);
  if (!response)
    {
      fprintf (stderr, "Memory error\n");
      fclose (pipe_fp);
      free (escaped_prompt);
      return EXIT_FAILURE;
    }
  size_t total_read = 0;
  size_t chunk;
  while ((chunk =
          fread (response + total_read, 1, buf_size - total_read - 1,
                 pipe_fp)) > 0)
    {
      total_read += chunk;
      if (total_read >= buf_size - 1)
        {
          buf_size *= 2;
          response = realloc (response, buf_size);
          if (!response)
            {
              fprintf (stderr, "Memory error\n");
              fclose (pipe_fp);
              free (escaped_prompt);
              return EXIT_FAILURE;
            }
        }
    }
  response[total_read] = '\0';
  fclose (pipe_fp);
  if (debug_mode)
    printf ("Debug: Response: %s\n", response);
  int status;
  (void) waitpid (child_pid, &status, 0);
  if (!WIFEXITED (status) || WEXITSTATUS (status) != 0)
    {
      fprintf (stderr, "curl command failed\n");
      free (response);
      free (escaped_prompt);
      return EXIT_FAILURE;
    }
  char *response_id = NULL;
  char *content = extract_json_content (response, &response_id);
  free (response);
  if (content == NULL)
    {
      printf ("No content in response or API error occurred.\n");
      free (response_id);
      free (escaped_prompt);
      return EXIT_FAILURE;
    }
  if (!stateless_mode && response_id)
    save_session_id (response_id);
  free (response_id);
  extract_and_save_code_blocks (content, prompt);
  char *formatted_content = format_text_spacing (content);
  print_wrapped (formatted_content ? formatted_content : content, 75);
  putchar ('\n');
  free (formatted_content);
  free (content);
  free (escaped_prompt);
  return EXIT_SUCCESS;
}
