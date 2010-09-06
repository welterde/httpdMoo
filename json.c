/******************************************************************************
  Copyright 2010 Todd Sundsted. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY TODD SUNDSTED ``AS IS'' AND ANY EXPRESS OR
  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
  EVENT SHALL TODD SUNDSTED OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
  OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  The views and conclusions contained in the software and documentation are
  those of the authors and should not be interpreted as representing official
  policies, either expressed or implied, of Todd Sundsted.
 *****************************************************************************/

#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

#include <stdio.h>

#include "my-string.h"
#include "my-stdlib.h"

#include "functions.h"
#include "hash.h"
#include "json.h"
#include "list.h"
#include "numbers.h"
#include "storage.h"
#include "unparse.h"
#include "utils.h"

struct stack_item {
  struct stack_item *prev;
  Var v;
};

static void
push(struct stack_item **top, Var v) {
  struct stack_item *item = malloc(sizeof(struct stack_item));
  item->prev = *top;
  item->v = v;
  *top = item;
}

static Var
pop(struct stack_item **top) {
  struct stack_item *item = *top;
  *top = item->prev;
  Var v = item->v;
  free(item);
  return v;
}

#define PUSH(stack, v) push((struct stack_item **)stack, v)

#define POP(stack) pop((struct stack_item **)stack)

static int
handle_null(void * ctx)
{
  return 1;
}

static int
handle_boolean(void * ctx, int boolean)
{
  return 1;
}

static int
handle_integer(void * ctx, long integerVal)
{
  Var v;
  v.type = TYPE_INT;
  v.v.num = (int)integerVal;
  PUSH(ctx, v);
  return 1;
}

static int
handle_float(void * ctx, double doubleVal)
{
  Var v;
  v = new_float(doubleVal);
  PUSH(ctx, v);
  return 1;
}

static int
handle_string(void * ctx, const unsigned char * stringVal, unsigned int stringLen)
{
  Var v;
  char temp[stringLen + 1];
  strncpy(temp, stringVal, stringLen);
  temp[stringLen] = '\0';
  v.type = TYPE_STR;
  v.v.str = str_dup(temp);
  PUSH(ctx, v);
  return 1;
}

static int
handle_map_key(void * ctx, const unsigned char * stringVal, unsigned int stringLen)
{
  Var v;
  char temp[stringLen + 1];
  strncpy(temp, stringVal, stringLen);
  temp[stringLen] = '\0';
  v.type = TYPE_STR;
  v.v.str = str_dup(temp);
  PUSH(ctx, v);
  return 1;
}

static int
handle_start_map(void * ctx)
{
  Var k, v;
  k.type = -2;
  PUSH(ctx, k);
  v.type = -2;
  PUSH(ctx, v);
  return 1;
}

static int
handle_end_map(void * ctx)
{
  Var list = new_hash();
  Var k, v;
  for (v = POP(ctx), k = POP(ctx); (int)v.type > -1; v = POP(ctx), k = POP(ctx)) {
    hashinsert(list, k, v);
  }
  PUSH(ctx, list);
  return 1;
}

static int
handle_start_array(void * ctx)
{
  Var v;
  v.type = -1;
  PUSH(ctx, v);
  return 1;
}

static int
handle_end_array(void * ctx)
{
  Var list = new_list(0);
  Var v;
  for (v = POP(ctx); (int)v.type > -1; v = POP(ctx)) {
    list = listinsert(list, v, 1);
  }
  PUSH(ctx, list);
  return 1;
}

static yajl_callbacks callbacks = {
    NULL,
    NULL,
    handle_integer,
    handle_float,
    NULL,
    handle_string,
    handle_start_map,
    handle_map_key,
    handle_end_map,
    handle_start_array,
    handle_end_array
};

static void
generate(yajl_gen g, Var v);

static void
do_hash_hash(Var key, Var value, void *data, int32 first)
{
  yajl_gen g = *(yajl_gen *)data;
  generate(g, key);
  generate(g, value);
}

static void
generate(yajl_gen g, Var v)
{
  switch (v.type) {
    case TYPE_INT:
      yajl_gen_integer(g, v.v.num);
      break;
    case TYPE_FLOAT:
      yajl_gen_double(g, *v.v.fnum);
      break;
    case TYPE_STR:
      yajl_gen_string(g, v.v.str, strlen(v.v.str));
      break;
    case TYPE_ERR: {
      const char *tmp = error_name(v.v.err);
      yajl_gen_string(g, tmp, strlen(tmp));
      break;
    }
    case TYPE_HASH: {
      yajl_gen_map_open(g);
      hashforeach(v, do_hash_hash, &g);
      yajl_gen_map_close(g);
      break;
    }
    case TYPE_LIST: {
      int i;
      yajl_gen_array_open(g);
      for (i = 1; i <= v.v.list[0].v.num; i++)
	generate(g, v.v.list[i]);
      yajl_gen_array_close(g);
      break;
    }
  }
}

static package
bf_parse_json(Var arglist, Byte next, void *vdata, Objid progr)
{
  yajl_handle hand;
  yajl_parser_config cfg = { 1, 1 };
  yajl_status stat;
  struct stack_item stack;
  struct stack_item *top = &stack;
  stack.v.type = TYPE_INT;
  stack.v.v.num = -1;
  const char *str = arglist.v.list[1].v.str;
  size_t len = strlen(str);
  package pack;
  int done = 0;

  hand = yajl_alloc(&callbacks, &cfg, NULL, (void *)&top);

  while (!done) {
    if (len == 0)
      done = 1;

    if (done)
      stat = yajl_parse_complete(hand);
    else
      stat = yajl_parse(hand, str, len);

    len = 0;

    if (done) {
      if (stat != yajl_status_ok)
	pack = make_error_pack(E_INVARG);
      else
	pack = make_var_pack(top->v);
    }
  }

  yajl_free(hand);

  free_var(arglist);
  return pack;
}

static package
bf_generate_json(Var arglist, Byte next, void *vdata, Objid progr)
{
  yajl_gen g;
  yajl_gen_config cfg = { 0, "" };
  const unsigned char *buf;
  unsigned int len;
  Var json;
  package pack;

  g = yajl_gen_alloc(&cfg, NULL);

  generate(g, arglist.v.list[1]);

  yajl_gen_get_buf(g, &buf, &len);

  json.type = TYPE_STR;
  json.v.str = str_dup(buf);

  pack = make_var_pack(json);

  yajl_gen_clear(g);
  yajl_gen_free(g);

  free_var(arglist);
  return pack;
}

void
register_yajl(void)
{
  register_function("parse_json", 1, 1, bf_parse_json, TYPE_STR);
  register_function("generate_json", 1, 1, bf_generate_json, TYPE_ANY);
}