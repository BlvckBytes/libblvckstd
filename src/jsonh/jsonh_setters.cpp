#include "blvckstd/jsonh.h"

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

INLINED static jsonh_opres_t jsonh_set_value(htable_t *jsonh, const char *key, void *val, jsonh_datatype_t val_type)
{
  scptr jsonh_value_t* value = jsonh_value_make(val, val_type);
  if (htable_insert(jsonh, key, mman_ref(value)) == HTABLE_SUCCESS)
    return JOPRES_SUCCESS;

  mman_dealloc(value);
  return JOPRES_SIZELIM_EXCEED;
}

jsonh_opres_t jsonh_set_obj(htable_t *jsonh, const char *key, void *obj)
{
  return jsonh_set_value(jsonh, key, obj, JDTYPE_OBJ);
}

jsonh_opres_t jsonh_set_obj_ref(htable_t *jsonh, const char *key, void *obj)
{
  jsonh_opres_t res = jsonh_set_value(jsonh, key, mman_ref(obj), JDTYPE_OBJ);
  if (res != JOPRES_SUCCESS)
    mman_dealloc(obj);
  return res;
}

jsonh_opres_t jsonh_set_str(htable_t *jsonh, const char *key, void *str)
{
  return jsonh_set_value(jsonh, key, str, JDTYPE_STR);
}

jsonh_opres_t jsonh_set_str_ref(htable_t *jsonh, const char *key, void *str)
{
  jsonh_opres_t res = jsonh_set_value(jsonh, key, mman_ref(str), JDTYPE_STR);
  if (res != JOPRES_SUCCESS)
    mman_dealloc(str);
  return res;
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

jsonh_opres_t jsonh_set_arr(htable_t *jsonh, const char *key, void *arr)
{
  return jsonh_set_value(jsonh, key, arr, JDTYPE_ARR);
}

jsonh_opres_t jsonh_set_arr_ref(htable_t *jsonh, const char *key, void *arr)
{
  jsonh_opres_t res = jsonh_set_value(jsonh, key, mman_ref(arr), JDTYPE_ARR);
  if (res != JOPRES_SUCCESS)
    mman_dealloc(arr);
  return res;
}
