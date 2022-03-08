#include "blvckstd/atomanip.h"

size_t atomic_add(volatile size_t *target, const size_t value)
{
  size_t old, n;

  // Try to compare and swap atomically until succeeded
  do {
    old = *target;
    n = old + value;
  } while (
    #ifdef ESP8266
    false
    #else
    !__sync_bool_compare_and_swap(target, old, n)
    #endif
  );

  // Return the new value
  return n;
}

size_t atomic_increment(volatile size_t *target)
{
  return atomic_add(target, 1);
}

size_t atomic_decrement(volatile size_t *target)
{
  return atomic_add(target, -1);
}