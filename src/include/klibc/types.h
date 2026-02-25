#ifndef KERNEL_TYPES_H
#define KERNEL_TYPES_H

#if !defined(__GNUC__) && !defined(__clang__)
#  error "This kernel requires GCC or Clang."
#endif

#ifndef NULL
#  ifdef __cplusplus
#    define NULL 0
#  else
#    define NULL ((void *)0)
#  endif
#endif

#ifndef __cplusplus
typedef _Bool bool;
#  define true  1
#  define false 0
#endif

typedef unsigned char       uint8_t;    /*  8-bit unsigned */
typedef unsigned short      uint16_t;   /* 16-bit unsigned */
typedef unsigned int        uint32_t;   /* 32-bit unsigned */
typedef unsigned long       uint64_t;   /* 64-bit unsigned */

typedef signed char         int8_t;     /*  8-bit signed   */
typedef signed short        int16_t;    /* 16-bit signed   */
typedef signed int          int32_t;    /* 32-bit signed   */
typedef signed long         int64_t;    /* 64-bit signed   */

typedef uint8_t             uint_least8_t;
typedef uint16_t            uint_least16_t;
typedef uint32_t            uint_least32_t;
typedef uint64_t            uint_least64_t;

typedef int8_t              int_least8_t;
typedef int16_t             int_least16_t;
typedef int32_t             int_least32_t;
typedef int64_t             int_least64_t;

typedef unsigned char       uint_fast8_t;
typedef unsigned long       uint_fast16_t;
typedef unsigned long       uint_fast32_t;
typedef unsigned long       uint_fast64_t;

typedef signed char         int_fast8_t;
typedef signed long         int_fast16_t;
typedef signed long         int_fast32_t;
typedef signed long         int_fast64_t;

#if defined(__x86_64__) || defined(__aarch64__)
  typedef unsigned long     uintptr_t;  /* unsigned pointer-width integer */
  typedef signed long       intptr_t;   /* signed   pointer-width integer */
  typedef unsigned long     size_t;     /* object / memory size           */
  typedef signed long       ssize_t;    /* signed size / byte count       */
  typedef signed long       ptrdiff_t;  /* pointer difference             */
#else  /* 32-bit (x86, ARM32, etc.) */
  typedef uint32_t          uintptr_t;
  typedef int32_t           intptr_t;
  typedef uint32_t          size_t;
  typedef int32_t           ssize_t;
  typedef int32_t           ptrdiff_t;
#endif

typedef unsigned long       uintmax_t;
typedef signed long         intmax_t;

typedef uintptr_t           phys_addr_t;   /* physical memory address      */
typedef uintptr_t           virt_addr_t;   /* virtual  memory address      */
typedef uint64_t            off_t;         /* file / device offset         */
typedef uint32_t            uid_t;         /* user  identifier             */
typedef uint32_t            gid_t;         /* group identifier             */
typedef int32_t             pid_t;         /* process identifier           */
typedef int32_t             tid_t;         /* thread  identifier           */
typedef uint64_t            dev_t;         /* device identifier            */
typedef uint64_t            ino_t;         /* inode  number                */
typedef uint32_t            mode_t;        /* file permission mode         */
typedef uint32_t            nlink_t;       /* hard-link count              */
typedef int64_t             time_t;        /* UNIX timestamp (seconds)     */
typedef int64_t             clock_t;       /* CPU clock ticks              */

typedef uint8_t             byte_t;        /* single byte                  */
typedef uint16_t            word_t;        /* 16-bit word                  */
typedef uint32_t            dword_t;       /* 32-bit double-word           */
typedef uint64_t            qword_t;       /* 64-bit quad-word             */

typedef volatile uint8_t    reg8_t;        /* 8-bit  MMIO / hardware reg   */
typedef volatile uint16_t   reg16_t;       /* 16-bit MMIO / hardware reg   */
typedef volatile uint32_t   reg32_t;       /* 32-bit MMIO / hardware reg   */
typedef volatile uint64_t   reg64_t;       /* 64-bit MMIO / hardware reg   */

#define UINT8_MAX    ((uint8_t)  0xFF)
#define UINT16_MAX   ((uint16_t) 0xFFFF)
#define UINT32_MAX   ((uint32_t) 0xFFFFFFFFUL)
#define UINT64_MAX   ((uint64_t) 0xFFFFFFFFFFFFFFFFUL)

#define INT8_MIN     ((int8_t)   0x80)
#define INT8_MAX     ((int8_t)   0x7F)
#define INT16_MIN    ((int16_t)  0x8000)
#define INT16_MAX    ((int16_t)  0x7FFF)
#define INT32_MIN    ((int32_t)  0x80000000L)
#define INT32_MAX    ((int32_t)  0x7FFFFFFFL)
#define INT64_MIN    ((int64_t)  0x8000000000000000L)
#define INT64_MAX    ((int64_t)  0x7FFFFFFFFFFFFFFFL)

#define SIZE_MAX     UINTPTR_MAX

#if defined(__x86_64__) || defined(__aarch64__)
#  define UINTPTR_MAX  UINT64_MAX
#  define INTPTR_MAX   INT64_MAX
#  define INTPTR_MIN   INT64_MIN
#else
#  define UINTPTR_MAX  UINT32_MAX
#  define INTPTR_MAX   INT32_MAX
#  define INTPTR_MIN   INT32_MIN
#endif

#define PACKED          __attribute__((packed))
#define ALIGNED(n)      __attribute__((aligned(n)))
#define NORETURN        __attribute__((noreturn))
#define UNUSED          __attribute__((unused))
#define USED            __attribute__((used))
#define INLINE          static inline __attribute__((always_inline))
#define NOINLINE        __attribute__((noinline))
#define PURE            __attribute__((pure))
#define CONST_FUNC      __attribute__((const))
#define LIKELY(x)       __builtin_expect(!!(x), 1)
#define UNLIKELY(x)     __builtin_expect(!!(x), 0)
#define DEPRECATED      __attribute__((deprecated))
#define WEAK            __attribute__((weak))

#define STATIC_ASSERT(cond, msg) \
    typedef char static_assert_##msg[(cond) ? 1 : -1] UNUSED

STATIC_ASSERT(sizeof(uint8_t)  == 1, uint8_t_must_be_1_byte);
STATIC_ASSERT(sizeof(uint16_t) == 2, uint16_t_must_be_2_bytes);
STATIC_ASSERT(sizeof(uint32_t) == 4, uint32_t_must_be_4_bytes);
STATIC_ASSERT(sizeof(uint64_t) == 8, uint64_t_must_be_8_bytes);

#define ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))

#define ALIGN_UP(x, align)  (((x) + ((align) - 1)) & ~((align) - 1))

#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))

#define IS_ALIGNED(x, align) (((x) & ((align) - 1)) == 0)

#ifndef offsetof
#  define offsetof(type, member)  __builtin_offsetof(type, member)
#endif

#define container_of(ptr, type, member) \
    ((type *)((uint8_t *)(ptr) - offsetof(type, member)))

#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#define MAX(a, b)   ((a) > (b) ? (a) : (b))

#define CLAMP(val, lo, hi)  MIN(MAX((val), (lo)), (hi))

#define STRINGIFY(x)    #x
#define TOSTRING(x)     STRINGIFY(x)

#define UNUSED_VAR(x)   ((void)(x))

#endif