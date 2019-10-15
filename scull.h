#ifndef __SCULL_H__
#define __SUCLL_H__

#undef PDEBUG

#ifdef SCULL_DEBUG
#ifdef __KERNEL__
#define PDEBUG(fmt, args...) printk("<%s: %d> "fmt, ##args);
#else
#define PDEBUG(fmt, args...) printf("<%s: %d> "fmt, ##args);
#endif
#else
#define PDEBUG(fmt, args...)
#endif

#undef PDEBUG
#define PDEBUG(fmt, args...)

#endif
