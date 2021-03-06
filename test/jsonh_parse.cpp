#include <stdio.h>
#include <blvckstd/jsonh.h>

#define EXIT_TEST_FAILURE(varname, retv)                              \
  {                                                                   \
    const char *rets = jsonh_opres_name(retv);                        \
    printf(varname " didn't match the expected value! (%s)\n", rets); \
    return 1;                                                         \
  }

int proc()
{
  const char *jsn = \
  "{"
  "  \"hello\": \"world\","
  " \"my-arr\": ["
  "     5,4,3"
  "     ,"
  "     2,"
  "         "
  "         2,"
  "     44.3,"
  "     null,"
  "     true,"
  "     \"yes\","
  "     false\n"
  " ]"
  "}";

  scptr char *err = NULL;
  scptr htable_t *res = jsonh_parse(jsn, &err);

  if (!res)
  {
    printf("Could not parse JSON: %s\n", err);
    return 1;
  }

  scptr char *stringified = jsonh_stringify(res, 2);
  printf("%s", stringified);

  char *hello = NULL;
  if (
    jsonh_get_str(res, "hello", &hello) != JOPRES_SUCCESS
    || strcmp(hello, "world") != 0
  )
  {
    printf("Missing or mismatching value: hello=%s\n", hello);
    return 1;
  }

  return 0;
}

int main()
{
  int ret = proc();

  if (ret == 0)
    printf("Tests passed!\n");
  else
    printf("Test(s) failed!\n");

  mman_print_info();
  return ret;
}