#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deps/str-replace/str-replace.h"
#include "parse_extras.h"
#include "string_extras.h"

void envdata_copy(void *_dst, const void *_src)
{
  envdata_t *dst = (envdata_t*)_dst, *src = (envdata_t*)_src;
  dst->rowspacing = src->rowspacing ? strdup(src->rowspacing) : NULL;
  dst->rowlines = src->rowlines ? strdup(src->rowlines) : NULL;
}

void envdata_dtor(void *_elt)
{
  envdata_t *elt = (envdata_t*)_elt;
  if (elt->rowspacing) { free(elt->rowspacing); }
  if (elt->rowlines) { free(elt->rowlines); }
}

UT_icd envdata_icd = {sizeof(envdata_t), NULL, envdata_copy, envdata_dtor};

void env_replacements(UT_array **environment_data_stack, const char *environment)
{
  UT_array *array_stack;
  UT_array *row_spacing_stack;
  UT_array *rowlines_stack;

  char *tok = NULL, *at_top = NULL;

  char *dupe_str = dupe_string(environment);
  char *line = strtok(dupe_str, "\n");
  char *attr_rowspacing = "", *temp = "", **last_stack_item;
  char *a;
  UT_string *em_str;
  const char *from = "\\begin", *until = "\\end", *hline = "\\hline", *hdashline = "\\hdashline",
              *line_separator = "\\\\",
               *em_pattern_begin = "\\[", *em_pattern_end = "]",
                *is_smallmatrix = NULL, *is_gathered = NULL;

  int rowlines_stack_len = 0, em_offset = 0;

  // if not an environment, don't bother going on
  if ( ((strstr(environment, from) == NULL && strstr(environment, until) == NULL)) || strstr(environment, "begin{svg}")) {
    free(dupe_str);
    // XXX: Should there be a return here?
  }

  // set up the array stack
  utarray_new(array_stack, &ut_str_icd);
  utarray_new(*environment_data_stack, &envdata_icd);
  utarray_new(row_spacing_stack, &ut_str_icd);
  utarray_new(rowlines_stack, &ut_str_icd);

  while (line != NULL) {
    utarray_push_back(array_stack, &line);

    if (strstr(line, until) != NULL) {
      while (utarray_len(array_stack) > 0) {
        last_stack_item = (char **)utarray_back(array_stack);

        rowlines_stack_len = utarray_len(rowlines_stack);
        at_top = strstr(*last_stack_item, from);

        // we've reached the top, but there looks like there might be some data
        if (at_top != NULL && strstr(*last_stack_item, line_separator) == NULL) {
          break;
        }

        // looking for a line match
        if (strstr(*last_stack_item, hline) != NULL) {
          if (rowlines_stack_len > 0) {
            utarray_pop_back(rowlines_stack);
          }
          a = "solid";
          utarray_push_back(rowlines_stack, &a);
        } else if (strstr(*last_stack_item, hdashline) != NULL) {
          if (rowlines_stack_len > 0) {
            utarray_pop_back(rowlines_stack);
          }
          a = "dashed";
          utarray_push_back(rowlines_stack, &a);
        } else {
          a = "none";
          utarray_push_back(rowlines_stack, &a);
        }

        if (strstr(*last_stack_item, line_separator) != NULL) {
          if ( (tok = strstr(*last_stack_item, em_pattern_begin)) != NULL) {
            temp = tok + 2;
            if ( (tok = strstr(temp, em_pattern_end)) != NULL) {
              em_offset = (int)(tok - temp);
              char *s = dupe_string_n(temp, em_offset);
              utstring_new(em_str);
              utstring_printf(em_str, "%s ", s);
              utarray_push_back(row_spacing_stack, &utstring_body(em_str));
              utstring_free(em_str);
              free(s);
            }
          } else {
            if (strstr(*last_stack_item, "\\begin{smallmatrix}") != NULL) {
              em_str = "0.2em ";
            } else if (strstr(*last_stack_item, "\\begin{gathered}") != NULL) {
              em_str = "1.0ex ";
            } else {
              em_str = "0.5ex ";
            }
            utarray_push_back(row_spacing_stack, &em_str);
          }
        }

        // make sure to pop at the end here; it messes with some references in Travis/Ubuntu for some reason
        utarray_pop_back(array_stack);
        // we've reached the top, so stop.
        if (at_top != NULL) {
          break;
        }
      }
      if (at_top != NULL) {
        is_smallmatrix = strstr(at_top, "\\begin{smallmatrix}");
        is_gathered = strstr(at_top, "\\begin{gathered}");
      }

      // TODO: we are skipping equation environments
      if ((rowlines_stack_len != 0 || utarray_len(row_spacing_stack)) && strstr(*last_stack_item, "\\begin{equation}") == NULL) {
      	// XXX: I would make this block into a function.
        perform_replacement(environment_data_stack, rowlines_stack, attr_rowspacing, is_smallmatrix, is_gathered, row_spacing_stack);
      }

      utarray_clear(row_spacing_stack);
      utarray_clear(rowlines_stack);
      attr_rowspacing = "";
      rowlines_stack_len = 0;
    }

    line = strtok(NULL, "\n");
  }

  utarray_free(row_spacing_stack);
  utarray_free(array_stack);
  utarray_free(rowlines_stack);
}

void perform_replacement(UT_array **environment_data_stack, UT_array *rowlines_stack, char *attr_rowspacing, char *is_smallmatrix, char *is_gathered, UT_array *row_spacing_stack) {
  int i = 1, rowlines_stack_size = utarray_len(rowlines_stack);
  char *a, *attr_rowlines;
  envdata_t row_data;

  // we cut the last char because we can always skip the first row
  utarray_pop_back(rowlines_stack);

  // empty rowlines should be reset
  if (utarray_len(rowlines_stack) == 0) {
    a = "none";
    utarray_push_back(rowlines_stack, &a);
  }

  // given the row_attribute values, construct an attribute list (separated by spaces)
  UT_string *l;
  utstring_new(l);
  char **o=NULL;
  a = "rowlines=\"";
  utstring_printf(l, "%s", a);
  while ( (o=(char**)utarray_prev(rowlines_stack,o))) {
    if (i >= rowlines_stack_size - 1) {
      utstring_printf(l, "%s", *o);
    }
    else {
      utstring_printf(l, "%s ", *o);
      i++;
    }
  }

  // a = "\"";
  // utstring_printf(l, "%s", a);
  attr_rowlines = utstring_body(l);

  // given the row_spacing values, construct an attribute list (separated by spaces)
  UT_string *s;
  utstring_new(s);
  char **p=NULL;
  while ( (p=(char**)utarray_prev(row_spacing_stack,p))) {
    if (is_smallmatrix && strcmp(*p, "0.5ex ") == 0) {
      utstring_printf(s, "%s", "0.2em ");
    } else if (is_gathered && strcmp(*p, "0.5ex ") == 0) {
      utstring_printf(s, "%s", "1.0ex ");
    } else {
      utstring_printf(s, "%s", *p);
    }
  }

  attr_rowspacing = utstring_body(s);
  if (strlen(attr_rowspacing) > 0) {
    remove_last_char(attr_rowspacing); // remove the final space
  } else {
    if (is_smallmatrix != NULL) {
      attr_rowspacing = "0.2em";
    } else if (is_gathered != NULL) {
      attr_rowspacing = "1.0ex";
    } else {
      attr_rowspacing = "0.5ex";
    }
  }

  row_data.rowspacing = attr_rowspacing;
  row_data.rowlines = attr_rowlines;

  utarray_push_back(*environment_data_stack, &row_data);
  utstring_free(s);
}

const char *vertical_pipe_extract(const char *string)
{
  char *orig = dupe_string(string);
  char *columnlines = malloc(30), *previous_column = "";
  int i = 0;

  if (strncmp(orig, "s", 1) == 0) {
  	// XXX: Did you mean to copy these?
    columnlines = "frame=\"solid\" columnlines=\"";
    remove_first_char(orig);
  } else if (strncmp(orig, "d", 1) == 0) {
    columnlines = "frame=\"dashed\" columnlines=\"";
    remove_first_char(orig);
  } else {
    columnlines = "columnlines=\"";
  }

  char *token = strtok(orig, " ");

  while (token != NULL) {
    if (strncmp(token, "s", 1) == 0) {
      previous_column = "s";
      // XXX: Same memory issue with using join here
      columnlines = join(columnlines, "solid ");
    } else if (strncmp(token, "d", 1) == 0) {
      previous_column = "d";
      columnlines = join(columnlines, "dashed ");
    } else {
      if (i >= 1) { // we must skip the first blank col
        // only if there is no previous border should a border be considered, eg. "cc", not "c|c"
        if (strncmp(previous_column, "s", 1) != 0 && strncmp(previous_column, "d", 1) != 0) {
          columnlines = join(columnlines, "none ");
        }
        previous_column = "0";
      }
    }

    i++;
    token = strtok(NULL, " ");
  }

  // an empty string here angers Lasem
  if (strncmp(columnlines, "columnlines=\"\0", 14) == 0) {
    columnlines = join("columnlines=\"none", "");
  }
  // an empty space at the end also angers Lasem
  else {
    remove_last_char(columnlines);
  }

  free(orig);

  return columnlines;
}

const char *remove_excess_pipe_chars(const char *string)
{
  char *dup = str_replace(string, "s", "");
  // XXX: dup is leaked here
  dup = str_replace(dup, "d", "");

  return dup;
}

const char *combine_row_data(UT_array **environment_data_stack)
{
  // if no information was provided, give a standard sizing
  if (utarray_len(*environment_data_stack) == 0) {
    const char* s = "rowspacing=\"0.5ex\" rowlines=\"none\"";
    char* c = (char*)malloc(strlen(s) + 1);
    strcpy(c, s);
    return c;
  }
  envdata_t *row_data_elem = (envdata_t*) utarray_front(*environment_data_stack);

  char *row_spacing_data = row_data_elem->rowspacing,
        *row_lines_data = row_data_elem->rowlines,
        *row_attr;

  UT_string *row_attr_data;
  utstring_new(row_attr_data);

  // combine the row spacing and row lines data
  utstring_printf(row_attr_data, "%s%s\" %s\"", "rowspacing=\"", row_spacing_data, row_lines_data);

  row_attr = utstring_body(row_attr_data);
  utarray_erase(*environment_data_stack, 0, 1);

  return row_attr;
}
