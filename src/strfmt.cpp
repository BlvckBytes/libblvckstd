#include "blvckstd/strfmt.h"

bool vstrfmt(char **buf, size_t *offs, const char *fmt, va_list ap)
{
  va_list ap2;
  va_copy(ap2, ap);

  // Calculate available remaining buffer size
  mman_meta_t *buf_meta = mman_fetch_meta(*buf);
  size_t offs_v = offs ? *offs : 0;
  size_t buf_len = buf_meta->num_blocks;
  size_t buf_avail = buf_len - (offs_v + 1);

  // Calculate how many chars are needed
  size_t needed = vsnprintf(NULL, 0, fmt, ap) + 1;

  // Buffer too small
  if (buf_avail < needed)
  {
    size_t diff = needed - buf_avail;

    // Extend by the difference, check if there was enough space
    if (!mman_realloc((void **) buf, sizeof(char), buf_len + diff))
      return false;

    buf_avail += diff;
  }

  // Write into buffer and update outside offset, if applicable
  int written = vsnprintf(&((*buf)[offs_v]), buf_avail, fmt, ap2);
  if (offs) *offs += written;
  va_end(ap2);
  return true;
}

bool strfmt(char **buf, size_t *offs, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);

  bool res = vstrfmt(buf, offs, fmt, ap);

  va_end(ap);
  return res;
}

char *strfmt_direct(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);

  scptr char *res = vstrfmt_direct(fmt, ap);

  va_end(ap);
  return (char *) mman_ref(res);
}

char *vstrfmt_direct(const char *fmt, va_list ap)
{
  scptr char *buf = (char *) mman_alloc(sizeof(char), 128, NULL);
  size_t buf_offs = 0;

  if (!vstrfmt(&buf, &buf_offs, fmt, ap))
    return NULL;

  return (char *) mman_ref(buf);
}
