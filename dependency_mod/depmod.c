#include <linux/version.h>
#include <asm/atomic.h>
#include <asm/byteorder.h>
#include <asm/div64.h>
#include <asm-generic/bitops/le.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <linux/backing-dev.h>
#include <linux/bio.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bit_spinlock.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/capability.h>
#include <linux/compat.h>
#include <linux/compiler.h>
#include <linux/dcache.h>
#include <linux/errno.h>
#include <linux/exportfs.h>
#include <linux/falloc.h>
#include <linux/file.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list_bl.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mount.h>
#include <linux/mpage.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/net.h>
#include <linux/nls.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/percpu.h>
#include <linux/prefetch.h>
#include <linux/proc_fs.h>
#include <linux/quotaops.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/security.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/swap.h>
#include <linux/sysctl.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/uio.h>
#include <linux/vfs.h>
#include <linux/vmalloc.h>
#include <linux/writeback.h>
#include <linux/xattr.h>
#include <net/sock.h>

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,38))
#include <linux/smp_lock.h>
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))
#include <linux/config.h>
#endif


