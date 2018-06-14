#ifndef REAL_TYPE_DEFINED
#define REAL_TYPE_DEFINED

#ifndef SIZEOF_REAL
#   define SIZEOF_REAL 8
#endif

#if SIZEOF_REAL == 8

    typedef double real;
#define REAL_EPSILON DBL_EPSILON
#define     HUGE_VALR HUGE_VAL

#elif SIZEOF_REAL == 4

    typedef float real;
#define REAL_EPSILON FLT_EPSILON
#define     HUGE_VALR ((real)(HUGE_VAL))

#else

#   error bad SIZEOF_REAL

#endif


#endif  // REAL_TYPE_DEFINED
