// SPDX-License-Identifier: GPL-2.0-only
/*
 * core.c — MERLIN-V program object lifecycle.
 *
 * Manages the per-program IDR, reference counting, anonymous file
 * descriptors, and the deferred-free work queue.
 *
 * Cross-references:
 *   docs/design/03-kernel-interfaces.md §2  (syscall, object model)
 *   include/merlin_internal.h               (struct merlin_prog, IDR decls)
 */

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include "include/merlin_internal.h"

/* -----------------------------------------------------------------------
 * Global ID registries
 * ----------------------------------------------------------------------- */
DEFINE_IDR(merlin_prog_idr);
DEFINE_IDR(merlin_map_idr);
DEFINE_IDR(merlin_link_idr);
DEFINE_SPINLOCK(merlin_idr_lock);

EXPORT_SYMBOL_GPL(merlin_prog_idr);
EXPORT_SYMBOL_GPL(merlin_map_idr);
EXPORT_SYMBOL_GPL(merlin_link_idr);
EXPORT_SYMBOL_GPL(merlin_idr_lock);

/* -----------------------------------------------------------------------
 * merlin_prog file operations
 *
 * The prog fd carries the refcount that keeps the prog alive while
 * user space has the fd open.
 * ----------------------------------------------------------------------- */
static int merlin_prog_release(struct inode *inode, struct file *file)
{
	struct merlin_prog *prog = file->private_data;

	merlin_prog_put(prog);
	return 0;
}

static const struct file_operations merlin_prog_fops = {
	.owner   = THIS_MODULE,
	.release = merlin_prog_release,
	.llseek  = no_llseek,
};

/* -----------------------------------------------------------------------
 * merlin_prog_new_fd — wrap a prog in an anon inode, return fd
 *
 * The returned fd holds a reference.  Caller must have already acquired
 * an extra reference (e.g. via refcount_inc) before calling.
 * ----------------------------------------------------------------------- */
int merlin_prog_new_fd(struct merlin_prog *prog)
{
	int fd;

	fd = anon_inode_getfd("[merlin_prog]", &merlin_prog_fops, prog,
			      O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		merlin_prog_put(prog);  /* drop the ref we promised to consume */
		return fd;
	}
	return fd;
}

/* -----------------------------------------------------------------------
 * Reference counting and deferred free
 * ----------------------------------------------------------------------- */

static void merlin_prog_free_work(struct work_struct *work)
{
	struct merlin_prog *prog = container_of(work, struct merlin_prog,
						work_free);
	unsigned long flags;
	struct merlin_jit_image *img;

	/* Remove from IDR */
	spin_lock_irqsave(&merlin_idr_lock, flags);
	idr_remove(&merlin_prog_idr, prog->id);
	spin_unlock_irqrestore(&merlin_idr_lock, flags);

	/* Free JIT image — the RCU grace period was already drained before
	 * we could reach refcount 0 (all prog_fn callers hold rcu_read_lock,
	 * so when the prog fd + all links are gone the grace period has
	 * already elapsed).
	 */
	img = rcu_dereference_protected(prog->jit_image, true);
	if (img) {
		if (img->image)
			vfree(img->image);
		kfree(img);
	}

	kfree(prog->bytecode);
	kfree(prog);
}

void merlin_prog_put(struct merlin_prog *prog)
{
	if (!prog)
		return;
	if (refcount_dec_and_test(&prog->refs))
		schedule_work(&prog->work_free);
}
EXPORT_SYMBOL_GPL(merlin_prog_put);

/* -----------------------------------------------------------------------
 * Lookup helpers
 * ----------------------------------------------------------------------- */

struct merlin_prog *merlin_prog_get_by_id(u32 id)
{
	struct merlin_prog *prog;
	unsigned long flags;

	spin_lock_irqsave(&merlin_idr_lock, flags);
	prog = idr_find(&merlin_prog_idr, id);
	if (prog && !refcount_inc_not_zero(&prog->refs))
		prog = NULL;
	spin_unlock_irqrestore(&merlin_idr_lock, flags);
	return prog;
}
EXPORT_SYMBOL_GPL(merlin_prog_get_by_id);

struct merlin_prog *merlin_prog_get_by_fd(int fd)
{
	struct file *f = fget(fd);
	struct merlin_prog *prog;

	if (!f)
		return ERR_PTR(-EBADF);
	if (f->f_op != &merlin_prog_fops) {
		fput(f);
		return ERR_PTR(-EINVAL);
	}
	prog = f->private_data;
	refcount_inc(&prog->refs);
	fput(f);
	return prog;
}
EXPORT_SYMBOL_GPL(merlin_prog_get_by_fd);

/* -----------------------------------------------------------------------
 * IDR allocation helper (called from loader.c after prog is ready)
 * ----------------------------------------------------------------------- */
int merlin_prog_alloc_id(struct merlin_prog *prog)
{
	unsigned long flags;
	int id;

	spin_lock_irqsave(&merlin_idr_lock, flags);
	id = idr_alloc(&merlin_prog_idr, prog, 1, 0, GFP_ATOMIC);
	spin_unlock_irqrestore(&merlin_idr_lock, flags);
	if (id < 0)
		return id;
	prog->id = id;
	return 0;
}
