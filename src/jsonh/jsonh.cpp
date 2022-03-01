#include "blvckstd/jsonh.h"

ENUM_LUT_FULL_IMPL(jsonh_datatype, _EVALS_JSONH_DTYPE);
ENUM_LUT_FULL_IMPL(jsonh_literal, _EVALS_JSONH_LITERAL);
ENUM_LUT_FULL_IMPL(jsonh_opres, _EVALS_JSONH_OPRES);

/*
============================================================================
                                 Creation                                   
============================================================================
*/

htable_t *jsonh_make()
{
  scptr htable_t *res = htable_make(JSONH_ROOT_ITEM_CAP, mman_dealloc_nr);
  return (htable_t *) mman_ref(res);
}

/*
============================================================================
                                  Parsing                                   
============================================================================
*/

jsonh_cursor_t *jsonh_cursor_make(const char *text)
{
  scptr jsonh_cursor_t *cursor = (jsonh_cursor_t *) mman_alloc(sizeof(jsonh_cursor_t), 1, NULL);

  // Set the text and calculate it's length
  // This gets saved for efficiency reasons, as strlen is quite slow
  cursor->text = text;
  cursor->text_length = strlen(text);

  // Start out at the first character
  cursor->text_index = 0;

  // Start out at the first line's first char
  cursor->char_index = 0;
  cursor->line_index = 0;

  return (jsonh_cursor_t *) mman_ref(cursor);
}

jsonh_char_t jsonh_cursor_getc(jsonh_cursor_t *cursor)
{
  jsonh_char_t ret = jsonh_cursor_peekc(cursor);

  // Linebreak occurred, update index trackers
  if (ret.c == '\n' && !ret.is_esc)
  {
    cursor->char_index = 0;
    cursor->line_index++;
  }

  // Character index should not be leading, like text_index is
  else if (cursor->text_index != 0)
    cursor->char_index++;

  // Advance position in text
  cursor->text_index++;
  return ret;
}

jsonh_char_t jsonh_cursor_peekc(jsonh_cursor_t *cursor)
{
  // EOF
  if (cursor->text_index == cursor->text_length)
    return (jsonh_char_t) { 0, false };

  // Fetch the current character
  char ret = cursor->text[cursor->text_index];

  // Check if this character is escaped
  bool is_esc = (
    cursor->text_index > 0                              // Not the first char (as it's unescapable)
    && cursor->text[cursor->text_index - 1] == '\\'     // And it's preceded by a backslash
  );

  return (jsonh_char_t) { ret, is_esc };
}

void jsonh_cursor_ungetc(jsonh_cursor_t *cursor)
{
  // Cannot go back any further
  if (cursor->text_index == 0)
    return;

  // Rewind character index and also rewind the
  // line-index if it goes below zero
  if (
    cursor->char_index != 0
    && --(cursor->char_index) < 0
    && cursor->line_index != 0
  )
  {
    cursor->line_index--;
    cursor->char_index = 0;
  }

  // Rewind text index marker
  cursor->text_index--;
}

static void jsonh_parse_err(jsonh_cursor_t *cursor, char **err, const char *fmt, ...)
{
  // No error buffer provided
  if (!err) return;

  // Generate error message from provided format and varargs
  va_list ap;
  va_start(ap, fmt);
  scptr char *errmsg = vstrfmt_direct(fmt, ap);
  va_end(ap);

  // Append prefix and write into error buffer
  *err = strfmt_direct(
    "(%ld:%ld) -> %s",
    cursor->line_index, cursor->char_index,
    errmsg
  );
}

bool jsonh_parse_str(jsonh_cursor_t *cursor, char **err, char **out)
{
  jsonh_char_t curr;
  if ((curr = jsonh_cursor_getc(cursor)).c != '"')
  {
    jsonh_parse_err(cursor, err, "Expected >\"< but encountered >%c<", curr.c);
    return false;
  }

  // Save a copy of the string-starting cursor
  jsonh_cursor_t strstart_c = *cursor;

  // Allocate buffer
  scptr char *str = (char *) mman_alloc(sizeof(char), 128, NULL);
  size_t str_offs = 0;

  // Collect characters into a buffer
  while (true)
  {
    curr = jsonh_cursor_getc(cursor);

    // End of string reached
    if (curr.c == '"' && !curr.is_esc)
      break;

    // Non-printable characters need to be escaped inside of strings
    if (curr.c >= 1 && curr.c <= 31 && !curr.is_esc)
    {
      jsonh_parse_err(cursor, err, "Unescaped control sequence inside of string");
      return false;
    }

    // EOF before string has been closed
    if (!curr.c)
    {
      jsonh_parse_err(&strstart_c, err, "Unterminated string encountered");
      return false;
    }

    // Append to buffer
    strfmt(&str, &str_offs, "%c", curr.c);
  }

  *out = (char *) mman_ref(str);
  return true;
}

/*
  TODO: Not yet conform, missing the following:
  * e-notation (5e10, 5e-10)
*/
bool jsonh_parse_num(jsonh_cursor_t *cursor, char **err, double *out, bool *had_dot)
{
  bool has_dot = false, is_first = true;
  jsonh_char_t curr;

  // Collect digits and a possible dot
  scptr char *buf = (char *) mman_alloc(sizeof(char), 128, NULL);
  size_t buf_offs = 0;
  while ((curr = jsonh_cursor_getc(cursor)).c)
  {
    // Append to buffer
    strfmt(&buf, &buf_offs, "%c", curr.c);

    // Not a digit
    if (!(curr.c >= '0' && curr.c <= '9'))
    {
      // Numbers always start with digits or the optional negative sign
      if (is_first)
      {
        // Negative sign encountered, just add it to the buffer
        if (curr.c == '-')
        {
          is_first = false;
          continue;
        }

        jsonh_parse_err(cursor, err, "Expected a digit or a negative sign, but found >%c<", curr.c);
        return false;
      }

      // Found a dot which indicates floating point numbers
      if (curr.c == '.')
      {
        // Already has a dot
        if (has_dot)
        {
          jsonh_parse_err(cursor, err, "Numbers can only have one comma");
          return false;
        }

        // Now has a dot
        has_dot = true;
        is_first = false;
        continue;
      }

      // Done reading the number, unget the last char to be picked up by the next routine
      jsonh_cursor_ungetc(cursor);
      break;
    }

    is_first = false;
  }

  // Routine started out at EOF
  if (is_first && curr.c == 0)
  {
    jsonh_parse_err(cursor, err, "Expected a digit, but found EOF");
    return false;
  }

  // Write parsed number to output
  *out = atof(buf);
  if (had_dot) *had_dot = has_dot;
  return true;
}

bool jsonh_parse_literal(jsonh_cursor_t *cursor, char **err, jsonh_literal_t *out)
{
  jsonh_char_t curr;
  bool is_first = true;

  // Collect literal into buffer
  scptr char *buf = (char *) mman_alloc(sizeof(char), 128, NULL);
  size_t buf_offs = 0;
  while ((curr = jsonh_cursor_getc(cursor)).c)
  {
    // Is a literal delimiter
    if (
      curr.c == ' '
      || curr.c == '}'
      || curr.c == ']'
      || curr.c == ','
      || (curr.c < ' ' || curr.c == 127) // Unprintables
    )
    {
      // Put back this delimiter to be picked up by the next routine
      jsonh_cursor_ungetc(cursor);
      break;
    }

    // Append to buffer
    strfmt(&buf, &buf_offs, "%c", curr.c);
    is_first = false;
  }

  // Routine started out at EOF
  if (is_first && curr.c == 0)
  {
    jsonh_parse_err(cursor, err, "Expected a literal, but found EOF");
    return false;
  }

  if (strcmp("true", buf) == 0)
  {
    *out = JLIT_TRUE;
    return true;
  }

  if (strcmp("false", buf) == 0)
  {
    *out = JLIT_FALSE;
    return true;
  }

  if (strcmp("null", buf) == 0)
  {
    *out = JLIT_NULL;
    return true;
  }

  jsonh_parse_err(cursor, err, "Unknown literal " QUOTSTR, buf);
  return false;
}

bool jsonh_parse_value(jsonh_cursor_t *cursor, char **err, jsonh_value_t *out)
{
  // Peek at the first non-whitespace char
  jsonh_parse_eat_whitespace(cursor);
  jsonh_char_t fc = jsonh_cursor_peekc(cursor);

  // Number
  if ((fc.c >= '0' && fc.c <= '9') || fc.c == '-')
  {
    double num;
    bool had_dot;
    if (!jsonh_parse_num(cursor, err, &num, &had_dot))
      return false;

    // Had a dot and thus is a float
    if (had_dot)
    {
      out->type = JDTYPE_FLOAT;
      out->value = mman_alloc(sizeof(float), 1, NULL);
      *((float *) out->value) = num;
      return true;
    }

    // No dot, just a regular integer
    out->type = JDTYPE_INT;
    out->value = mman_alloc(sizeof(int), 1, NULL);
    *((int *) out->value) = num;
    return true;
  }

  // String
  if (fc.c == '"')
  {
    scptr char *str = NULL;
    if (!jsonh_parse_str(cursor, err, &str))
      return false;

    out->value = mman_ref(str);
    out->type = JDTYPE_STR;
    return true;
  }

  // Object
  if (fc.c == '{')
  {
    scptr htable_t *obj = NULL;
    if (!jsonh_parse_obj(cursor, err, &obj))
      return false;

    out->type = JDTYPE_OBJ;
    out->value = mman_ref(obj);
    return true;
  }

  // Array
  if (fc.c == '[')
  {
    scptr dynarr_t *arr = NULL;
    if (!jsonh_parse_arr(cursor, err, &arr))
      return false;

    out->type = JDTYPE_ARR;
    out->value = mman_ref(arr);
    return true;
  }

  // Only thing remaining: Literal
  jsonh_literal_t literal;
  if (!jsonh_parse_literal(cursor, err, &literal))
    return false;

  switch(literal)
  {
    // Boolean value
    case JLIT_FALSE:
    case JLIT_TRUE:
    out->type = JDTYPE_BOOL;
    out->value = mman_alloc(sizeof(bool), 1, NULL);
    *((bool *) out->value) = literal == JLIT_TRUE;
    return true;

    // Null value
    case JLIT_NULL:
    out->type = JDTYPE_NULL;
    out->value = NULL;
    return true;

    default:
    jsonh_parse_err(cursor, err, "Literal %s not yet implemented", jsonh_literal_name(literal));
    return false;
  }
}

bool jsonh_parse_arr(jsonh_cursor_t *cursor, char **err, dynarr_t **out)
{
  // Array start char
  jsonh_char_t curr;
  if ((curr = jsonh_cursor_getc(cursor)).c != '[')
  {
    jsonh_parse_err(cursor, err, "Expected array-begin but found >%c<", curr.c);
    return false;
  }

  jsonh_parse_eat_whitespace(cursor);

  // Parse values until the end of array is reached
  scptr dynarr_t *arr = dynarr_make(16, 1024, mman_dealloc_nr);
  while ((curr = jsonh_cursor_peekc(cursor)).c != ']')
  {
    jsonh_parse_eat_whitespace(cursor);

    // Parse a JSON value
    scptr jsonh_value_t *value = jsonh_value_make(NULL, JDTYPE_NULL);
    if (!jsonh_parse_value(cursor, err, value))
      return false;
    
    dynarr_result_t res;
    if ((res = dynarr_push(arr, mman_ref(value), NULL)) != DYNARR_SUCCESS)
    {
      jsonh_parse_err(cursor, err, "Could not push array value internally (%s)", dynarr_result_name(res));
      mman_dealloc(value);
      return false;
    }

    // Get the next non-whitespace char and check if it's a value separator
    // If not, stop reading
    jsonh_parse_eat_whitespace(cursor);
    if ((curr = jsonh_cursor_getc(cursor)).c != ',')
    {
      // Put back the non-separator for the next step to pick up
      jsonh_cursor_ungetc(cursor);
      break;
    }
    jsonh_parse_eat_whitespace(cursor);
  }

  jsonh_parse_eat_whitespace(cursor);

  // Array end char
  if ((curr = jsonh_cursor_getc(cursor)).c != ']')
  {
    jsonh_parse_err(cursor, err, "Expected array-end but found >%c<", curr.c);
    return false;
  }

  *out = (dynarr_t *) mman_ref(arr);
  return true;
}

bool jsonh_parse_obj(jsonh_cursor_t *cursor, char **err, htable_t **out)
{
  // Object start char
  jsonh_char_t curr;
  if ((curr = jsonh_cursor_getc(cursor)).c != '{')
  {
    jsonh_parse_err(cursor, err, "Expected object-begin but found >%c<", curr.c);
    return false;
  }

  jsonh_parse_eat_whitespace(cursor);

  // Parse values until the end of array is reached
  scptr htable_t *obj = htable_make(1024, mman_dealloc_nr);
  while ((curr = jsonh_cursor_peekc(cursor)).c != '}')
  {
    jsonh_parse_eat_whitespace(cursor);

    // Parse string key
    scptr char *key = NULL;
    if (!jsonh_parse_str(cursor, err, &key))
      return false;

    jsonh_parse_eat_whitespace(cursor);

    // Read k-v separator
    if ((curr = jsonh_cursor_getc(cursor)).c != ':')
    {
      jsonh_parse_err(cursor, err, "Expected >:< but found >%c<", curr.c);
      return false;
    }

    jsonh_parse_eat_whitespace(cursor);

    // Parse a JSON value
    scptr jsonh_value_t *value = jsonh_value_make(NULL, JDTYPE_NULL);
    if (!jsonh_parse_value(cursor, err, value))
      return false;

    jsonh_parse_eat_whitespace(cursor);

    htable_result_t res;
    if ((res = htable_insert(obj, key, mman_ref(value))) != HTABLE_SUCCESS)
    {
      jsonh_parse_err(cursor, err, "Could not push hashtable value internally (%s)", htable_result_name(res));
      mman_dealloc(value);
      return false;
    }

    // Get the next non-whitespace char and check if it's a value separator
    // If not, stop reading
    jsonh_parse_eat_whitespace(cursor);
    if ((curr = jsonh_cursor_getc(cursor)).c != ',')
    {
      // Put back the non-separator for the next step to pick up
      jsonh_cursor_ungetc(cursor);
      break;
    }
    jsonh_parse_eat_whitespace(cursor);
  }

  jsonh_parse_eat_whitespace(cursor);

  // Object end char
  if ((curr = jsonh_cursor_getc(cursor)).c != '}')
  {
    jsonh_parse_err(cursor, err, "Expected object-end but found >%c<", curr.c);
    return false;
  }

  *out = (htable_t *) mman_ref(obj);
  return true;
}

void jsonh_parse_eat_whitespace(jsonh_cursor_t *cursor)
{
  // Get chars until EOF or printable has been reached
  jsonh_char_t curr;
  while ((curr = jsonh_cursor_getc(cursor)).c > 0 && (curr.c <= 32 || curr.c == 127));

  // Unget a printable to be picked up by the next routine
  if (curr.c != 0)
    jsonh_cursor_ungetc(cursor);
}

htable_t *jsonh_parse(const char *json, char **err)
{
  scptr jsonh_cursor_t *cursor = jsonh_cursor_make(json);

  // JSON needs to have an object at root level
  scptr htable_t *res = NULL;
  if (!jsonh_parse_obj(cursor, err, &res))
    return NULL;

  return (htable_t *) mman_ref(res);
}

/*
============================================================================
                                 Stringify                                  
============================================================================
*/

// Hoisted declarations
static void jsonh_stringify_obj(htable_t *obj, int indent, int indent_level, char **buf, size_t *buf_offs);
static void jsonh_stringify_arr(dynarr_t *arr, int indent, int indent_level, char **buf, size_t *buf_offs);

static char *jsonh_gen_indent(int indent)
{
  scptr char *buf = (char *) mman_alloc(sizeof(char), indent + 1, NULL);
  for (int i = 0; i < indent; i++)
    buf[i] = ' ';
  buf[indent] = 0;
  return (char *) mman_ref(buf);
}

static char *jsonh_escape_string(char *str)
{
  scptr char *res = (char *) mman_alloc(sizeof(char), 128, NULL);
  size_t res_ind = 0;

  for (char *c = str; *c; c++)
  {
    // Double buffer size when the end is reached
    mman_meta_t *res_meta = mman_fetch_meta(res);
    if (res_meta->num_blocks <= res_ind)
      mman_realloc((void **) &res, res_meta->block_size, res_meta->num_blocks * 2);

    // Escape characters by a leading backslash
    if (*c == '"' || *c == '\\')
      res[res_ind++] = '\\';

    res[res_ind++] = *c;
  }

  res[res_ind] = 0;
  return (char *) mman_ref(res);
}

/**
 * @brief Stringify a json-value based on it's type marker field
 * 
 * @param jv Jason value
 * @param indent Size of an indentation
 * @param indent_level Current level of indentation
 * @param buf String buffer to write into
 * @param buf_offs Current buffer offset, will be altered
 */
static void jsonh_stringify_value(jsonh_value_t *jv, int indent, int indent_level, char **buf, size_t *buf_offs)
{
  // Decide on type and call matching stfingifier
  switch (jv->type)
  {
    case JDTYPE_BOOL:
    strfmt(buf, buf_offs, "%s", *((bool *) jv->value) ? "true" : "false");
    return;

    case JDTYPE_INT:
    strfmt(buf, buf_offs, "%d", *((int *) jv->value));
    return;

    case JDTYPE_FLOAT:
    strfmt(buf, buf_offs, "%.7f", *((float *) jv->value));
    return;

    case JDTYPE_ARR:
    jsonh_stringify_arr((dynarr_t *) jv->value, indent, indent_level + 1, buf, buf_offs);
    return;

    case JDTYPE_OBJ:
    jsonh_stringify_obj((htable_t *) jv->value, indent, indent_level + 1, buf, buf_offs);
    return;

    case JDTYPE_NULL:
    strfmt(buf, buf_offs, "null");
    return;

    case JDTYPE_STR:
    {
      char *value = (char *) jv->value;
      scptr char *value_escaped = jsonh_escape_string(value);
      strfmt(buf, buf_offs, QUOTSTR, value_escaped);
      return;
    }
  }
}

/**
 * @brief Stringify an array to it's corresponding JSON value
 * 
 * Example value:
 * {
 *   "a": "b",
 *   "c": "d",
 *   "e": 55
 * }
 * 
 * @param arr Array to stringify
 * @param indent Size of an indentation
 * @param indent_level Current level of indentation
 * @param buf String buffer to write into
 * @param buf_offs Current buffer offset, will be altered
 */
static void jsonh_stringify_obj(htable_t *obj, int indent, int indent_level, char **buf, size_t *buf_offs)
{
  strfmt(buf, buf_offs, "{\n");
  scptr char *indent_str = jsonh_gen_indent(indent * indent_level);
  scptr char *indent_str_outer = jsonh_gen_indent(indent * u64_max(0, (uint64_t) indent_level - 1U));

  // Get all object keys
  scptr char **keys = NULL;
  htable_list_keys(obj, &keys);

  // Iterate object keys
  for (char **key = keys; *key; key++)
  {
    jsonh_value_t *jv = NULL;
    if (htable_fetch(obj, *key, (void **) &jv) != HTABLE_SUCCESS)
      continue;

    const char *sep = *(key + 1) == NULL ? "" : ",";
    strfmt(buf, buf_offs, "%s" QUOTSTR ": ", indent_str, *key);
    jsonh_stringify_value(jv, indent, indent_level, buf, buf_offs);
    strfmt(buf, buf_offs, "%s\n", sep);
  }

  strfmt(buf, buf_offs, "%s}", indent_str_outer);
}

/**
 * @brief Stringify an array to it's corresponding JSON value
 * 
 * Example value:
 * [
 *   1,
 *   2,
 *   3
 * ]
 * 
 * @param arr Array to stringify
 * @param indent Size of an indentation
 * @param indent_level Current level of indentation
 * @param buf String buffer to write into
 * @param buf_offs Current buffer offset, will be altered
 */
static void jsonh_stringify_arr(dynarr_t *arr, int indent, int indent_level, char **buf, size_t *buf_offs)
{
  strfmt(buf, buf_offs, "[\n");
  scptr char *indent_str = jsonh_gen_indent(indent * indent_level);
  scptr char *indent_str_outer = jsonh_gen_indent(indent * u64_max(0, (uint64_t) indent_level - 1U));

  // Get all array values
  scptr void **arr_vals = NULL;
  dynarr_as_array(arr, &arr_vals);
  
  // Iterate array values
  for (void **arr_v = arr_vals; *arr_v; arr_v++)
  {
    jsonh_value_t *jv = (jsonh_value_t *) *arr_v;
    const char *sep = *(arr_v + 1) == NULL ? "" : ",";
    strfmt(buf, buf_offs, "%s", indent_str);
    jsonh_stringify_value(jv, indent, indent_level, buf, buf_offs);
    strfmt(buf, buf_offs, "%s\n", sep);
  }

  strfmt(buf, buf_offs, "%s]", indent_str_outer);
}

char *jsonh_stringify(htable_t *jsonh, int indent)
{
  scptr char *buf = (char *) mman_alloc(sizeof(char), 512, NULL);
  size_t buf_offs = 0;

  jsonh_stringify_obj(jsonh, indent, 1, &buf, &buf_offs);
  strfmt(&buf, &buf_offs, "\n");

  return (char *) mman_ref(buf);
}

/*
============================================================================
                                 Setters                                    
============================================================================
*/

static void jsonh_value_cleanup(mman_meta_t *meta)
{
  jsonh_value_t *value = (jsonh_value_t *) meta->ptr;
  mman_dealloc(value->value);
}

jsonh_value_t *jsonh_value_make(void *val, jsonh_datatype_t val_type)
{
  scptr jsonh_value_t *value = (jsonh_value_t *) mman_alloc(sizeof(jsonh_value_t), 1, jsonh_value_cleanup);
  value->type = val_type;
  value->value = val;
  return (jsonh_value_t *) mman_ref(value);
}

static jsonh_opres_t jsonh_set_value(htable_t *jsonh, const char *key, void *val, jsonh_datatype_t val_type)
{
  scptr jsonh_value_t* value = jsonh_value_make(val, val_type);
  if (htable_insert(jsonh, key, mman_ref(value)) == HTABLE_SUCCESS)
    return JOPRES_SUCCESS;

  mman_dealloc(value);
  return JOPRES_SIZELIM_EXCEED;
}

static jsonh_opres_t jsonh_insert_value(dynarr_t *array, void *val, jsonh_datatype_t val_type)
{
  scptr jsonh_value_t* value = jsonh_value_make(val, val_type);
  if (dynarr_push(array, mman_ref(value), NULL) == DYNARR_SUCCESS)
    return JOPRES_SUCCESS;

  mman_dealloc(value);
  return JOPRES_SIZELIM_EXCEED;
}

jsonh_opres_t jsonh_set_obj(htable_t *jsonh, const char *key, htable_t *obj)
{
  return jsonh_set_value(jsonh, key, (void *) obj, JDTYPE_OBJ);
}

jsonh_opres_t jsonh_set_str(htable_t *jsonh, const char *key, char *str)
{
  return jsonh_set_value(jsonh, key, (void *) str, JDTYPE_STR);
}

jsonh_opres_t jsonh_set_int(htable_t *jsonh, const char *key, int num)
{
  scptr int *numv = (int *) mman_alloc(sizeof(int), 1, NULL);
  *numv = num;

  jsonh_opres_t ret = jsonh_set_value(jsonh, key, mman_ref(numv), JDTYPE_INT);
  if (ret != JOPRES_SUCCESS)
    mman_dealloc(numv);

  return ret;
}

jsonh_opres_t jsonh_set_float(htable_t *jsonh, const char *key, float num)
{
  scptr float *numv = (float *) mman_alloc(sizeof(float), 1, NULL);
  *numv = num;

  jsonh_opres_t ret = jsonh_set_value(jsonh, key, mman_ref(numv), JDTYPE_FLOAT);
  if (ret != JOPRES_SUCCESS)
    mman_dealloc(numv);

  return ret;
}

jsonh_opres_t jsonh_set_bool(htable_t *jsonh, const char *key, bool b)
{
  scptr bool *boolv = (bool *) mman_alloc(sizeof(bool), 1, NULL);
  *boolv = b;

  jsonh_opres_t ret = jsonh_set_value(jsonh, key, mman_ref(boolv), JDTYPE_BOOL);
  if (ret != JOPRES_SUCCESS)
    mman_dealloc(boolv);

  return ret;
}

jsonh_opres_t jsonh_set_null(htable_t *jsonh, const char *key)
{
  return jsonh_set_value(jsonh, key, NULL, JDTYPE_NULL);
}

jsonh_opres_t jsonh_set_arr(htable_t *jsonh, const char *key, dynarr_t *arr)
{
  return jsonh_set_value(jsonh, key, (void *) arr, JDTYPE_ARR);
}

/*
============================================================================
                             Array Insertion                                
============================================================================
*/

jsonh_opres_t jsonh_insert_arr_obj(dynarr_t *array, htable_t *obj)
{
  return jsonh_insert_value(array, (void *) obj, JDTYPE_OBJ);
}

jsonh_opres_t jsonh_insert_arr_arr(dynarr_t *array, dynarr_t *arr)
{
  return jsonh_insert_value(array, (void *) arr, JDTYPE_ARR);
}

jsonh_opres_t jsonh_insert_arr_str(dynarr_t *array, char *str)
{
  return jsonh_insert_value(array, (void *) str, JDTYPE_STR);
}

jsonh_opres_t jsonh_insert_arr_int(dynarr_t *array, int num)
{
  scptr int *numv = (int *) mman_alloc(sizeof(int), 1, NULL);
  *numv = num;

  jsonh_opres_t ret = jsonh_insert_value(array, mman_ref(numv), JDTYPE_INT);
  if (ret != JOPRES_SUCCESS)
    mman_dealloc(numv);

  return ret;
}

jsonh_opres_t jsonh_insert_arr_float(dynarr_t *array, float num)
{
  scptr float *numv = (float *) mman_alloc(sizeof(float), 1, NULL);
  *numv = num;

  jsonh_opres_t ret = jsonh_insert_value(array, mman_ref(numv), JDTYPE_FLOAT);
  if (ret != JOPRES_SUCCESS)
    mman_dealloc(numv);

  return ret;
}

jsonh_opres_t jsonh_insert_arr_bool(dynarr_t *array, bool b)
{
  scptr bool *boolv = (bool *) mman_alloc(sizeof(bool), 1, NULL);
  *boolv = b;

  jsonh_opres_t ret = jsonh_insert_value(array, mman_ref(boolv), JDTYPE_BOOL);
  if (ret != JOPRES_SUCCESS)
    mman_dealloc(boolv);

  return ret;
}

jsonh_opres_t jsonh_insert_arr_null(dynarr_t *array)
{
  return jsonh_insert_value(array, NULL, JDTYPE_NULL);
}

/*
============================================================================
                                 Getters                                    
============================================================================
*/

char *jsonh_getter_errstr(const char *used_key, jsonh_opres_t opres)
{
  switch(opres)
  {
    case JOPRES_INVALID_KEY:
    return strfmt_direct("Missing the property " QUOTSTR, used_key);

    case JOPRES_DTYPE_MISMATCH:
    return strfmt_direct("The datatype of the property " QUOTSTR " mismatches", used_key);

    default:
    return strfmt_direct("Unknown error");
  }
}

/**
 * @brief Get a value from a json object by type, checks that the key
 * exists and the type matches
 * 
 * @param jsonh Json object to fetch from
 * @param key Key of the target value
 * @param dt Expected datatype
 * @param output Output buffer
 * 
 * @return jsonh_opres_t Operation result
 */
static jsonh_opres_t jsonh_get_value(htable_t *jsonh, const char *key, jsonh_datatype_t dt, void **output)
{
  jsonh_value_t *value = NULL;
  if (htable_fetch(jsonh, key, (void **) &value) != HTABLE_SUCCESS)
    return JOPRES_INVALID_KEY;

  if (value->type != dt)
    return JOPRES_DTYPE_MISMATCH;

  if (output)
    *output = value->value;

  return JOPRES_SUCCESS;
}

jsonh_opres_t jsonh_get_obj(htable_t *jsonh, const char *key, htable_t **obj)
{
  return jsonh_get_value(jsonh, key, JDTYPE_OBJ, (void **) obj);
}

jsonh_opres_t jsonh_get_arr(htable_t *jsonh, const char *key, dynarr_t **arr)
{
  return jsonh_get_value(jsonh, key, JDTYPE_ARR, (void **) arr);
}

jsonh_opres_t jsonh_get_str(htable_t *jsonh, const char *key, char **str)
{
  return jsonh_get_value(jsonh, key, JDTYPE_STR, (void **) str);
}

jsonh_opres_t jsonh_get_int(htable_t *jsonh, const char *key, int *num)
{
  int *val = NULL;
  jsonh_opres_t ret = jsonh_get_value(jsonh, key, JDTYPE_INT, (void **) &val);

  if (ret == JOPRES_SUCCESS)
    *num = *val;

  return ret;
}

jsonh_opres_t jsonh_get_float(htable_t *jsonh, const char *key, float *num)
{
  float *val = NULL;
  jsonh_opres_t ret = jsonh_get_value(jsonh, key, JDTYPE_FLOAT, (void **) &val);

  if (ret == JOPRES_SUCCESS)
    *num = *val;

  return ret;
}

jsonh_opres_t jsonh_get_bool(htable_t *jsonh, const char *key, bool *b)
{
  bool *val = NULL;
  jsonh_opres_t ret = jsonh_get_value(jsonh, key, JDTYPE_BOOL, (void **) &val);

  if (ret == JOPRES_SUCCESS)
    *b = *val;

  return ret;
}

jsonh_opres_t jsonh_get_is_null(htable_t *jsonh, const char *key, bool *is_null)
{
  jsonh_opres_t ret = jsonh_get_value(jsonh, key, JDTYPE_NULL, NULL);
  if (ret == JOPRES_INVALID_KEY)
    return ret;

  *is_null = ret != JOPRES_DTYPE_MISMATCH;
  return JOPRES_SUCCESS;
}

/*
============================================================================
                              Array Getters                                 
============================================================================
*/

static jsonh_opres_t jsonh_get_arr_value(dynarr_t *array, int index, jsonh_datatype_t dt, void **output)
{
  if (index < 0 || index >= array->_array_size)
    return JOPRES_INVALID_INDEX;

  jsonh_value_t *value = (jsonh_value_t *) array->items[index];
  if (value->type != dt)
    return JOPRES_DTYPE_MISMATCH;

  if (output)
    *output = value->value;

  return JOPRES_SUCCESS;
}

jsonh_opres_t jsonh_get_arr_obj(dynarr_t *array, int index, htable_t *obj)
{
  return jsonh_get_arr_value(array, index, JDTYPE_OBJ, (void **) &obj);
}

jsonh_opres_t jsonh_get_arr_arr(dynarr_t *array, int index, dynarr_t *arr)
{
  return jsonh_get_arr_value(array, index, JDTYPE_ARR, (void **) &arr);
}

jsonh_opres_t jsonh_get_arr_str(dynarr_t *array, int index, char *str)
{
  return jsonh_get_arr_value(array, index, JDTYPE_STR, (void **) &str);
}

jsonh_opres_t jsonh_get_arr_int(dynarr_t *array, int index, int *num)
{
  int *val = NULL;
  jsonh_opres_t ret = jsonh_get_arr_value(array, index, JDTYPE_INT, (void **) &val);

  if (ret == JOPRES_SUCCESS)
    *num = *val;

  return ret;
}

jsonh_opres_t jsonh_get_arr_float(dynarr_t *array, int index, float *num)
{
  float *val = NULL;
  jsonh_opres_t ret = jsonh_get_arr_value(array, index, JDTYPE_FLOAT, (void **) &val);

  if (ret == JOPRES_SUCCESS)
    *num = *val;

  return ret;
}

jsonh_opres_t jsonh_get_arr_bool(dynarr_t *array, int index, bool *b)
{
  bool *val = NULL;
  jsonh_opres_t ret = jsonh_get_arr_value(array, index, JDTYPE_BOOL, (void **) &val);

  if (ret == JOPRES_SUCCESS)
    *b = *val;

  return ret;
}

jsonh_opres_t jsonh_get_arr_is_null(dynarr_t *array, int index, bool *is_null)
{
  jsonh_opres_t ret = jsonh_get_arr_value(array, index, JDTYPE_NULL, NULL);
  if (ret == JOPRES_INVALID_KEY)
    return ret;

  *is_null = ret != JOPRES_DTYPE_MISMATCH;
  return JOPRES_SUCCESS;
}
