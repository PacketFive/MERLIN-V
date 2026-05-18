// SPDX-License-Identifier: GPL-2.0-only
/*
 * syscall.c — merlin(2) syscall multiplexer.
 *
 * Mirrors the structure of kernel/bpf/syscall.c.  A single
 * SYSCALL_DEFINE3(merlin, ...) dispatches on cmd, calling into the
 * appropriate subsystem (loader, maps, links, ...).
 *
 * Cross-references:
 *   docs/design/03-kernel-interfaces.md §2   Command set and UAPI shape.
 *   docs/design/uapi/linux/merlin.h          enum merlin_cmd, union merlin_attr.
 *
 * For out-of-tree builds the actual SYSCALL_DEFINE is replaced by a
 * character device ioctl so we can test without patching the kernel
 * syscall table.  The syscall wrapper is conditional on CONFIG_MERLIN_SYSCALL
 * (default: use ioctl for out-of-tree; set y for in-tree use).
 *
 * The ioctl device is /dev/merlin (misc device, minor auto-assigned).
 */

#include <linux/anon_inodes.h>
#include <linux/capability.h>
#include <linux/filter.h>     /* BPF filter, CAP_BPF */
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include "include/merlin_internal.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PacketFive");
MODULE_DESCRIPTION("MERLIN-V in-kernel JIT VM (out-of-tree prototype)");
MODULE_VERSION("0.1.0");

/* -----------------------------------------------------------------------
 * Capability check
 *
 * In-tree: we would define CAP_MERLIN as a new capability bit.  For the
 * prototype, we gate on CAP_BPF (same privilege tier as eBPF loading).
 * ----------------------------------------------------------------------- */
static int merlin_check_cap(void)
{
	if (!capable(CAP_BPF) && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

/* -----------------------------------------------------------------------
 * Per-command handlers
 * ----------------------------------------------------------------------- */

static long merlin_cmd_prog_load(const union merlin_attr __user *uattr,
				 u32 attr_sz)
{
	struct merlin_prog *prog = NULL;
	int rc, fd;

	rc = merlin_prog_load(uattr, attr_sz, &prog);
	if (rc)
		return rc;

	/* Takes the refcount we got from merlin_prog_load */
	fd = merlin_prog_new_fd(prog);
	if (fd < 0) {
		merlin_prog_put(prog);
		return fd;
	}
	return fd;
}

static long merlin_cmd_prog_get_info(const union merlin_attr __user *uattr,
				     u32 attr_sz)
{
	union merlin_attr attr;
	struct merlin_prog *prog;
	struct merlin_prog_info info;
	int rc = 0;

	memset(&attr, 0, sizeof(attr));
	if (copy_from_user(&attr, uattr, min_t(u32, attr_sz, sizeof(attr))))
		return -EFAULT;

	prog = merlin_prog_get_by_fd(attr.get_info.fd);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	memset(&info, 0, sizeof(info));
	info.type     = prog->prog_type;
	info.id       = prog->id;
	info.profile  = prog->profile;
	info.load_time = prog->load_time_ns;
	info.run_cnt  = atomic64_read(&prog->run_cnt);
	info.run_time_ns = atomic64_read(&prog->run_time_ns);
	memcpy(info.tag, prog->tag, MERLIN_PROG_TAG_LEN);
	strscpy(info.name, prog->name, MERLIN_OBJ_NAME_LEN);

	if (attr.get_info.info_len && attr.get_info.info) {
		if (copy_to_user((void __user *)(uintptr_t)attr.get_info.info,
				 &info,
				 min_t(u32, attr.get_info.info_len,
				       sizeof(info))))
			rc = -EFAULT;
	}

	merlin_prog_put(prog);
	return rc;
}

static long merlin_cmd_prog_test_run(const union merlin_attr __user *uattr,
				     u32 attr_sz)
{
	union merlin_attr attr;
	struct merlin_prog *prog;
	void *ctx_buf = NULL;
	u64 retval;
	ktime_t t0, t1;
	int rc = 0;

	memset(&attr, 0, sizeof(attr));
	if (copy_from_user(&attr, uattr, min_t(u32, attr_sz, sizeof(attr))))
		return -EFAULT;

	prog = merlin_prog_get_by_fd(attr.test_run.prog_fd);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	if (!prog->prog_fn) {
		rc = -ENOEXEC;
		goto out;
	}

	/* Optionally provide a ctx buffer from user space */
	if (attr.test_run.ctx_size_in && attr.test_run.ctx_in) {
		ctx_buf = kvmalloc(attr.test_run.ctx_size_in, GFP_KERNEL);
		if (!ctx_buf) {
			rc = -ENOMEM;
			goto out;
		}
		if (copy_from_user(ctx_buf,
				   (void __user *)(uintptr_t)attr.test_run.ctx_in,
				   attr.test_run.ctx_size_in)) {
			rc = -EFAULT;
			goto out;
		}
	}

	t0 = ktime_get();
	retval = prog->prog_fn(ctx_buf);
	t1 = ktime_get();

	atomic64_inc(&prog->run_cnt);
	atomic64_add(ktime_to_ns(ktime_sub(t1, t0)), &prog->run_time_ns);

	/* Write back results */
	{
		union merlin_attr out_attr;

		if (copy_from_user(&out_attr, uattr,
				   min_t(u32, attr_sz, sizeof(out_attr))))
			goto out;
		out_attr.test_run.retval      = (u32)retval;
		out_attr.test_run.duration_ns =
			(u32)ktime_to_ns(ktime_sub(t1, t0));
		if (copy_to_user((void __user *)uattr, &out_attr,
				 min_t(u32, attr_sz, sizeof(out_attr))))
			rc = -EFAULT;
	}

out:
	kvfree(ctx_buf);
	merlin_prog_put(prog);
	return rc;
}

static long merlin_cmd_map_create(const union merlin_attr __user *uattr,
				  u32 attr_sz)
{
	return merlin_map_create(uattr, attr_sz);
}

/* -----------------------------------------------------------------------
 * Main dispatch table
 * ----------------------------------------------------------------------- */
static long merlin_do_cmd(enum merlin_cmd cmd,
			  const union merlin_attr __user *uattr,
			  u32 attr_sz)
{
	int rc;

	rc = merlin_check_cap();
	if (rc)
		return rc;

	switch (cmd) {
	case MERLIN_PROG_LOAD:
		return merlin_cmd_prog_load(uattr, attr_sz);
	case MERLIN_PROG_TEST_RUN:
		return merlin_cmd_prog_test_run(uattr, attr_sz);
	case MERLIN_PROG_GET_INFO_BY_FD:
		return merlin_cmd_prog_get_info(uattr, attr_sz);
	case MERLIN_MAP_CREATE:
		return merlin_cmd_map_create(uattr, attr_sz);

	/* Stubs for commands not yet implemented in the prototype */
	case MERLIN_PROG_GET_NEXT_ID:
	case MERLIN_PROG_GET_FD_BY_ID:
	case MERLIN_MAP_LOOKUP_ELEM:
	case MERLIN_MAP_UPDATE_ELEM:
	case MERLIN_MAP_DELETE_ELEM:
	case MERLIN_MAP_GET_NEXT_KEY:
	case MERLIN_MAP_GET_FD_BY_ID:
	case MERLIN_MAP_GET_INFO_BY_FD:
	case MERLIN_LINK_CREATE:
	case MERLIN_LINK_UPDATE:
	case MERLIN_LINK_DETACH:
	case MERLIN_LINK_GET_INFO_BY_FD:
	case MERLIN_BTF_LOAD:
	case MERLIN_BTF_GET_FD_BY_ID:
	case MERLIN_OBJ_PIN:
	case MERLIN_OBJ_GET:
	case MERLIN_PROG_GET_ATTESTATION:
	case MERLIN_MAP_BATCH_TXN_BEGIN:
	case MERLIN_MAP_BATCH_TXN_STAGE:
	case MERLIN_MAP_BATCH_TXN_COMMIT:
	case MERLIN_MAP_BATCH_TXN_ABORT:
	case MERLIN_NS_CREATE:
	case MERLIN_NS_GET_FD_BY_ID:
	case MERLIN_KEYRING_BIND:
		return -EOPNOTSUPP;

	default:
		return -EINVAL;
	}
}

/* -----------------------------------------------------------------------
 * Out-of-tree: expose as a misc device with a single ioctl.
 *
 * Usage from user space:
 *
 *   int fd = open("/dev/merlin", O_RDWR);
 *   int ret = ioctl(fd, MERLIN_IOC_CMD, &args);
 *
 * where args = struct merlin_ioc_args { u32 cmd; u32 attr_sz; u64 attr_ptr; }.
 * ----------------------------------------------------------------------- */

#define MERLIN_IOC_MAGIC   'M'
#define MERLIN_IOC_CMD     _IOWR(MERLIN_IOC_MAGIC, 1, struct merlin_ioc_args)

struct merlin_ioc_args {
	__u32 cmd;
	__u32 attr_sz;
	__u64 attr_ptr;   /* user pointer to union merlin_attr */
};

static long merlin_dev_ioctl(struct file *file, unsigned int ioctl_cmd,
			     unsigned long arg)
{
	struct merlin_ioc_args ioc;

	if (ioctl_cmd != MERLIN_IOC_CMD)
		return -ENOTTY;

	if (copy_from_user(&ioc, (void __user *)arg, sizeof(ioc)))
		return -EFAULT;

	if (ioc.cmd >= __MAX_MERLIN_CMD)
		return -EINVAL;

	return merlin_do_cmd((enum merlin_cmd)ioc.cmd,
			     (union merlin_attr __user *)(uintptr_t)ioc.attr_ptr,
			     ioc.attr_sz);
}

static const struct file_operations merlin_dev_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = merlin_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = merlin_dev_ioctl,
#endif
	.llseek         = no_llseek,
};

static struct miscdevice merlin_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "merlin",
	.fops  = &merlin_dev_fops,
	.mode  = 0600,
};

/* -----------------------------------------------------------------------
 * Module init / exit
 * ----------------------------------------------------------------------- */
static int __init merlin_init(void)
{
	int rc;

	rc = misc_register(&merlin_miscdev);
	if (rc) {
		pr_err("merlin: failed to register misc device: %d\n", rc);
		return rc;
	}

	pr_info("merlin: MERLIN-V in-kernel JIT VM loaded (prototype)\n");
	pr_info("merlin: device /dev/merlin minor=%d\n",
		merlin_miscdev.minor);
	return 0;
}

static void __exit merlin_exit(void)
{
	misc_deregister(&merlin_miscdev);
	pr_info("merlin: unloaded\n");
}

module_init(merlin_init);
module_exit(merlin_exit);
