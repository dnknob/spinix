#ifndef ASSERTK_H
#define ASSERTK_H

#define __ASSERTK_STRINGIFY(x) #x
#define __ASSERTK_TO_STRING(x) __ASSERTK_STRINGIFY(x)

#if __STDC_VERSION__ >= 199901L
void _kassert_99(const char *expr, const char *func, const char *file, const char *line);
#else
void _kassert(const char *expr, const char *file, const char *line);
#endif

#undef assertk

#ifdef NDEBUG
    /* Release build - assertions compile to nothing */
    #define assertk(expr) ((void)0)
#else
    /* Debug build - assertions are active */
    #if __STDC_VERSION__ >= 199901L
        /* C99 or later - include function name */
        #define assertk(expr) \
            ((expr) ? (void)0 : \
                _kassert_99(#expr, __func__, __FILE__, __ASSERTK_TO_STRING(__LINE__)))
    #else
        /* Pre-C99 - no function name available */
        #define assertk(expr) \
            ((expr) ? (void)0 : \
                _kassert(#expr, __FILE__, __ASSERTK_TO_STRING(__LINE__)))
    #endif
#endif

#endif
