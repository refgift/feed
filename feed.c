#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define BUFFER_SIZE (2 * 1024 * 1024)   // 2 MB
#define F_OK 0
#define STDOUT_FILENO 1
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
static int debug_mode = 0;
static int stateless_mode = 0;
static unsigned int pending_surrogate = 0;
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
  static unsigned int pending_surrogate = 0;    // for surrogates
  for (size_t i = 0; i < len; ++i)
    {
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
                char hex[5] = { 0 };
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
      obj->o.keys = realloc (obj->o.keys, obj->count * sizeof (char *));
      obj->o.values =
        realloc (obj->o.values, obj->count * sizeof (JsonValue *));
      if (!obj->o.keys || !obj->o.values)
        {
          free_json (obj);
          free (key);
          free_json (val);
          return NULL;
        }
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
      JsonValue *val = parse_value (t, tok);
      if (!val)
        {
          free_json (arr);
          return NULL;
        }
      arr->count++;
      arr->a = realloc (arr->a, arr->count * sizeof (JsonValue *));
      if (!arr->a)
        {
          free_json (arr);
          free_json (val);
          return NULL;
        }
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
/* const char match[] = "\"object\":\"response\",\"output\":[{\"content\":[{\"type\":\"output_text\",\"text\"" */
  ;
  if (json == NULL)
    return NULL;
  // Handle plain text errors (non-JSON)
  if (strstr (json, "{") == NULL)
    {
      printf ("API plain error:\n%s\n", json);
      return NULL;
    }
  // Check for API error first
  const char *err_ptr = strstr (json, "\"error\"");
  if (err_ptr)
    {
      const char *val_ptr = strchr (err_ptr, ':');
      if (val_ptr)
        {
          ++val_ptr;
          while (*val_ptr && isspace ((unsigned char) *val_ptr))
            ++val_ptr;
          if (strncmp (val_ptr, "null", 4) != 0)
            {
              // Not null, so it's an error
              const char *msg_ptr = strstr (err_ptr, "\"message\"");
              if (msg_ptr)
                {
                  msg_ptr = strchr (msg_ptr, ':') + 1;
                  while (*msg_ptr && isspace ((unsigned char) *msg_ptr))
                    ++msg_ptr;
                  if (*msg_ptr == '"')
                    ++msg_ptr;
                  printf ("API Error: ");
                  size_t printed = 0;
                  while (*msg_ptr && printed < 400 && *msg_ptr != '"')
                    {
                      if (*msg_ptr == '\\')
                        {
                          ++msg_ptr;
                        }
                      putchar (*msg_ptr);
                      ++msg_ptr;
                      printed++;
                    }
                  printf ("\n");
                }
              else
                {
                  printf ("API error response (no message).\n");
                }
              return NULL;
            }
          // If "error" is null, continue to content extraction
        }
    }
  const char *ptr = strstr (json, "\"text\":\"");
  if (!ptr)
    {
      fprintf (stderr, "No \"text\" field found in API response.\\n");
      if (debug_mode)
        printf ("Response preview (first 900 chars):\\n%.900s\\n", json);
      return NULL;
    }
  ptr += 8;                     // Skip "\"text\":\"" to start of content
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
            case '/':
              content[idx++] = '/';
              break;
            case 'b':
              content[idx++] = '\b';
              break;
            case 'f':
              content[idx++] = '\f';
              break;
            case '"':
              content[idx++] = '"';
              break;
            case 'u':
              {
                char hexbuf[5];
                int i;
                for (i = 0; i < 4 && *ptr; ++i, ++ptr)
                  {
                    if (!isxdigit ((unsigned char) *ptr))
                      break;
                    hexbuf[i] = *ptr;
                  }
                hexbuf[i] = '\0';
                if (i == 4)
                  {
                    unsigned int code =
                      (unsigned int) strtoul (hexbuf, NULL, 16);
                    if (code <= 0x7F)
                      {
                        content[idx++] = (char) code;
                      }
                    else if (code <= 0x7FF)
                      {
                        content[idx++] = (char) (0xC0 | (code >> 6));
                        content[idx++] = (char) (0x80 | (code & 0x3F));
                      }
                    else if (code <= 0xFFFF)
                      {
                        if (code >= 0xD800 && code <= 0xDBFF)
                          {
                            // high surrogate
                            pending_surrogate = code;
                          }
                        else if (code >= 0xDC00 && code <= 0xDFFF)
                          {
                            // low surrogate
                            if (pending_surrogate)
                              {
                                // combine
                                unsigned int full =
                                  0x10000 +
                                  ((pending_surrogate - 0xD800) << 10) +
                                  (code - 0xDC00);
                                content[idx++] = (char) (0xF0 | (full >> 18));
                                content[idx++] =
                                  (char) (0x80 | ((full >> 12) & 0x3F));
                                content[idx++] =
                                  (char) (0x80 | ((full >> 6) & 0x3F));
                                content[idx++] =
                                  (char) (0x80 | (full & 0x3F));
                                pending_surrogate = 0;
                              }
                            else
                              {
                                content[idx++] = '?';   // lone low surrogate
                              }
                          }
                        else
                          {
                            // BMP non-surrogate
                            content[idx++] = (char) (0xE0 | (code >> 12));
                            content[idx++] =
                              (char) (0x80 | ((code >> 6) & 0x3F));
                            content[idx++] = (char) (0x80 | (code & 0x3F));
                          }
                      }
                    else if (code <= 0x10FFFF)
                      {
                        // > U+FFFF, 4 bytes
                        content[idx++] = (char) (0xF0 | (code >> 18));
                        content[idx++] =
                          (char) (0x80 | ((code >> 12) & 0x3F));
                        content[idx++] = (char) (0x80 | ((code >> 6) & 0x3F));
                        content[idx++] = (char) (0x80 | (code & 0x3F));
                      }
                    else
                      {
                        content[idx++] = '?';   // invalid
                      }
                  }
                else
                  {
                    // invalid hex, copy as is
                    content[idx++] = 'u';
                    for (int j = 0; j < i; ++j)
                      content[idx++] = hexbuf[j];
                  }
                --ptr;          // adjust for outer ++ptr
              }
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
static int
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
  int stateless_set = 0;
  for (int i = 1; i < argc; ++i)
    {
      if (strcmp (argv[i], "--debug") == 0 || strcmp (argv[i], "-d") == 0)
        {
          debug_mode = 1;
        }
      else if (strcmp (argv[i], "--stateless") == 0)
        {
          if (stateless_set)
            {
              fprintf (stderr, "Error: --stateless and --stateful are mutually exclusive\n");
              return EXIT_FAILURE;
            }
          stateless_mode = 1;
          stateless_set = 1;
        }
      else if (strcmp (argv[i], "--stateful") == 0)
        {
          if (stateless_set)
            {
              fprintf (stderr, "Error: --stateless and --stateful are mutually exclusive\n");
              return EXIT_FAILURE;
            }
          stateless_mode = 0;
          stateless_set = 1;
        }
      else if (!prompt)
        {
          prompt = argv[i];
        }
      else
        {
          fprintf (stderr, "Usage: %s [--debug|-d] [--stateless|--stateful] \"prompt\"\n", argv[0]);
          return EXIT_FAILURE;
        }
    }
  if (!prompt)
    {
      fprintf (stderr, "Usage: %s [--debug|-d] [--stateless|--stateful] \"prompt\"\n", argv[0]);
      return EXIT_FAILURE;
    }
  if (!load_config ())
    {
      fprintf (stderr,
               "Error: Missing FEED_URL, FEED_KEY, or FEED_MODEL environment variables.\n");
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
  snprintf (json_payload, BUFFER_SIZE, "{\"model\":\"%s\",\"input\":\"%s\",\"store\":%s}",
            api_model, escaped_prompt, stateless_mode ? "false" : "true");
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
      clear_sensitive_data ();
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
      clear_sensitive_data ();
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
              clear_sensitive_data ();
              return EXIT_FAILURE;
            }
        }
    }
  response[total_read] = '\0';
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
  if (content == NULL)
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
