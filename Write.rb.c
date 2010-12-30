
#include <assert.h>
#include <math.h>
#include "Write.h"
#include "Stata.h"

int populate_fields_from_ruby_index = 0;
VALUE populate_fields_from_ruby(VALUE field, struct stata_file * f)
{
  VALUE v;
  
  v = rb_hash_aref(field, rb_str_new2("type"));
  assert(TYPE(v) == T_FIXNUM);
  f->typlist[populate_fields_from_ruby_index] = NUM2INT(v);
  
  v = rb_hash_aref(field, rb_str_new2("name"));
  assert(TYPE(v) == T_STRING);
  f->varlist[populate_fields_from_ruby_index] = (char*)malloc(33);
  strncpy(f->varlist[populate_fields_from_ruby_index], rb_string_value_cstr(&v), 33);
  
  v = rb_hash_aref(field, rb_str_new2("sort"));
  assert(TYPE(v) == T_FIXNUM);
  f->srtlist[populate_fields_from_ruby_index] = NUM2INT(v);
  
  v = rb_hash_aref(field, rb_str_new2("format"));
  assert(TYPE(v) == T_STRING);
  f->fmtlist[populate_fields_from_ruby_index] = (char*)malloc(49);
  strncpy(f->fmtlist[populate_fields_from_ruby_index], rb_string_value_cstr(&v), 49);
  
  v = rb_hash_aref(field, rb_str_new2("value_label"));
  assert(TYPE(v) == T_STRING);
  f->lbllist[populate_fields_from_ruby_index] = (char*)malloc(33);
  strncpy(f->lbllist[populate_fields_from_ruby_index], rb_string_value_cstr(&v), 33);
  
  v = rb_hash_aref(field, rb_str_new2("variable_label"));
  assert(TYPE(v) == T_STRING);
  f->variable_labels[populate_fields_from_ruby_index] = (char*)malloc(81);
  strncpy(f->variable_labels[populate_fields_from_ruby_index], rb_string_value_cstr(&v), 81);
  
  populate_fields_from_ruby_index++;
}

int populate_data_from_ruby_index = 0;
VALUE populate_data_from_ruby(VALUE row, struct stata_file * f)
{
  VALUE v;
  int j = 0;//, i = populate_data_from_ruby_index;
  for (j = 0 ; j < f->nvar ; j++)
  {
    v = rb_ary_entry(row, j);
    struct stata_var * var = &f->obs[populate_data_from_ruby_index].var[j];
    
    if (f->typlist[j] == 255) var->v_type = V_DOUBLE;
    else if (f->typlist[j] == 254) var->v_type = V_FLOAT;
    else if (f->typlist[j] == 253) var->v_type = V_LONG;
    else if (f->typlist[j] == 252) var->v_type = V_INT;
    else if (f->typlist[j] == 251) var->v_type = V_BYTE;
    else if (f->typlist[j] <= 244) var->v_type = f->typlist[j];
    else printf("ERROR, type is %d\n", f->typlist[j]);
    
    switch (TYPE(v)) {
      case T_SYMBOL:
        v = rb_str_new2(rb_id2name(SYM2ID(v)));
        const char * symbol_name = STR2CSTR(v);
        
        int dot = 0;
        if (strlen(symbol_name) == 5) dot = symbol_name[4] - 96;
        if (dot < 0 || dot > 26) { printf("ERROR, INVALID SYMBOL '%s'\n", symbol_name); continue; }
        
        if (f->typlist[j] == 255) var->v_double = pow(2, 1023) + dot*pow(2, 1011);
        else if (f->typlist[j] == 254) var->v_float = pow(2, 127) + dot*pow(2, 115);
        else if (f->typlist[j] == 253) var->v_long = 2147483621 + dot;
        else if (f->typlist[j] == 252) var->v_int = 32741 + dot;
        else if (f->typlist[j] == 251) var->v_byte = 101 + dot;
        else {
          printf("ERROR: invalid typlist '%d' %d\n", f->typlist[j], TYPE(v));
          exit(1);
        }
      break;
      case T_BIGNUM:
      case T_FIXNUM:
      case T_FLOAT:
        if (f->typlist[j] == 255) var->v_double = rb_num2dbl(v);
        else if (f->typlist[j] == 254) var->v_float = rb_num2dbl(v);
        else if (f->typlist[j] == 253) var->v_long = FIX2LONG(v);
        else if (f->typlist[j] == 252) var->v_int = FIX2LONG(v);
        else if (f->typlist[j] == 251) var->v_byte = FIX2LONG(v);
        else {
          printf("ERROR: invalid typlist '%d' %d %f\n", f->typlist[j], TYPE(v), RFLOAT(v)->value);
          exit(1);
        }
      break;
      case T_STRING:
        var->v_type = f->typlist[j];
        var->v_str = (char*)malloc(f->typlist[j]+1);
        strncpy(var->v_str, STR2CSTR(v), f->typlist[j]+1);
      break;
      case T_NIL:
        printf("ERROR: nil value submitted - this is invalid.\n");
      break;
      default:
        printf("ERROR: unknown ruby type: %d\n", TYPE(v));
      break;
    }
  }
  populate_data_from_ruby_index++;
}

int populate_value_labels_from_ruby_index = 0;
VALUE populate_value_labels_from_ruby(VALUE r_vlt, struct stata_file * f)
{
  VALUE v;
  
  f->num_vlt++;
  f->vlt = (struct stata_vlt *)realloc(f->vlt, sizeof(struct stata_vlt)*f->num_vlt);
  struct stata_vlt * vlt = &f->vlt[f->num_vlt-1];
  
  assert(TYPE(r_vlt) == T_HASH);
  
  v = rb_hash_aref(r_vlt, rb_str_new2("name"));
  assert(TYPE(v) == T_STRING);
  strncpy(vlt->name, STR2CSTR(v), 33);
  
  v = rb_hash_aref(r_vlt, rb_str_new2("table"));
  assert(TYPE(v) == T_ARRAY);
  
  vlt->n = RARRAY(v)->len;
  vlt->txtlen = 0;
  vlt->off = (uint32_t*)malloc(sizeof(uint32_t)*vlt->n);
  vlt->val = (uint32_t*)malloc(sizeof(uint32_t)*vlt->n);
  vlt->txtbuf = NULL;
  
  int i;
  for (i = 0 ; i < RARRAY(v)->len ; i++)
  {
    VALUE r = rb_ary_entry(v, i);
    assert(TYPE(r) == T_ARRAY);
    assert(TYPE(rb_ary_entry(r, 0)) == T_FIXNUM);
    assert(TYPE(rb_ary_entry(r, 1)) == T_STRING);
    char * txt = STR2CSTR(rb_ary_entry(r, 1));
    vlt->txtlen += strlen(txt)+1;
  }
  vlt->txtbuf = (char*)malloc(vlt->txtlen);
  
  vlt->txtlen = 0;
  for (i = 0 ; i < RARRAY(v)->len ; i++)
  {
    VALUE r = rb_ary_entry(v, i);
    vlt->val[i] = NUM2INT(rb_ary_entry(r, 0));
    char * txt = STR2CSTR(rb_ary_entry(r, 1));
    vlt->txtlen += strlen(txt)+1;
    
    vlt->off[i] = vlt->txtlen-(strlen(txt)+1);
    memcpy(vlt->txtbuf+vlt->off[i], txt, strlen(txt)+1);
  }
  
  populate_value_labels_from_ruby_index++;
}

VALUE method_write(VALUE self, VALUE filename, VALUE data)
{
  VALUE v;
  assert(TYPE(data) == T_HASH);
  assert(TYPE(filename) == T_STRING);
  
  if (rb_hash_aref(data, rb_str_new2("nvar")) == Qnil) return rb_str_new2("nvar is required\n");
  if (rb_hash_aref(data, rb_str_new2("nobs")) == Qnil) return rb_str_new2("nobs is required\n");
  if (rb_hash_aref(data, rb_str_new2("fields")) == Qnil) return rb_str_new2("no fields provided\n");
  if (rb_hash_aref(data, rb_str_new2("data")) == Qnil) return rb_str_new2("no data provided\n");
  
  struct stata_file * f = (struct stata_file *)malloc(sizeof(struct stata_file));
  memset(f, 0, sizeof(struct stata_file));
  
  
  /* 5.1 Headers */
  v = rb_hash_aref(data, rb_str_new2("nvar"));
  assert(TYPE(v) == T_FIXNUM);
  f->nvar = NUM2UINT(v);
  
  v = rb_hash_aref(data, rb_str_new2("nobs"));
  assert(TYPE(v) == T_FIXNUM);
  f->nobs = NUM2UINT(v);
  
  v = rb_hash_aref(data, rb_str_new2("data_label"));
  assert(TYPE(v) == T_STRING);
  strncpy(f->data_label, rb_string_value_cstr(&v), sizeof(f->data_label));
  
  v = rb_hash_aref(data, rb_str_new2("time_stamp"));
  assert(TYPE(v) == T_STRING);
  strncpy(f->time_stamp, rb_string_value_cstr(&v), sizeof(f->time_stamp));
  
  
  /* 5.2 and 5.3, Descriptors and Variable Labels */
  f->typlist = (uint8_t *)malloc(f->nvar);
  f->varlist = (char **)malloc(sizeof(char *)*f->nvar);
  f->srtlist = (uint16_t *)malloc(sizeof(uint16_t)*(f->nvar+1));
  f->fmtlist = (char **)malloc(sizeof(char *)*f->nvar);
  f->lbllist = (char **)malloc(sizeof(char *)*f->nvar);
  f->variable_labels = (char **)malloc(sizeof(char *)*f->nvar);
  
  v = rb_hash_aref(data, rb_str_new2("fields"));
  assert(TYPE(v) == T_ARRAY);
  
  populate_fields_from_ruby_index = 0;
  rb_iterate(rb_each, v, populate_fields_from_ruby, (VALUE)f);
  
  
  /* 5.3 Expansion Fields - nothing comes from ruby */
  
  
  /* 5.4 Data */
  int i, j;
  f->obs = (struct stata_obs *)malloc(sizeof(struct stata_obs)*f->nobs);
  for (j = 0 ; j < f->nobs ; j++)
  {
    f->obs[j].var = (struct stata_var *)malloc(sizeof(struct stata_var)*f->nvar);
    for (i = 0 ; i < f->nvar ; i++)
    {
      struct stata_var * var = &f->obs[j].var[i];
      memset(var, 0, sizeof(struct stata_var));
    }
  }
  v = rb_hash_aref(data, rb_str_new2("data"));
  assert(TYPE(v) == T_ARRAY);
  
  populate_data_from_ruby_index = 0;
  rb_iterate(rb_each, v, populate_data_from_ruby, (VALUE)f);
  
  
  /* 5.5 Value Label Tables */
  v = rb_hash_aref(data, rb_str_new2("value_labels"));
  assert(TYPE(v) == T_ARRAY);
  
  populate_value_labels_from_ruby_index = 0;
  rb_iterate(rb_each, v, populate_value_labels_from_ruby, (VALUE)f);
  
  write_stata_file(STR2CSTR(filename), f);
  
  free_stata(f);
  return INT2NUM(1);
}