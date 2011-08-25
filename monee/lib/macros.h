#ifndef LIB_MACROS_H
#define LIB_MACROS_H

#define xstr(x) _xstr(x)
#define _xstr(...) #__VA_ARGS__

#define in_range(val, begin, end, type) \
  ((type)(val) >= (type)(begin) && (type)(val) < (type)(end))

#endif
