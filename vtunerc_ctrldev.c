/*
 * vtunerc: /dev/vtunerc device
 *
 * Copyright (C) 2010-11 Honza Petrous <jpetrous@smartimp.cz>
 * [Created 2010-03-23]
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/delay.h>

#include <linux/time.h>
#include <linux/poll.h>

#include "vtunerc_priv.h"

#define VTUNERC_CTRLDEV_MAJOR	266
#define VTUNERC_CTRLDEV_NAME	"vtunerc"

#define VTUNER_MSG_LEN (sizeof(struct vtuner_message))

extern int tscheck;

static ssize_t vtunerc_ctrldev_write(struct file *filp, const char *buff,
					size_t len, loff_t *off)
{
	struct vtunerc_ctx *vtunerc = filp->private_data;
	struct dvb_demux *demux = &vtunerc->demux;
	char *kernel_buf;
	int tailsize = len % 188;

	if (vtunerc->closing)
		return -EINTR;

	if (len < 188) {
		printk(PRINTK_ERR "%s: ERR: Data are shorter then TS packet size (188)\n", __func__);
		return -EINVAL;
	}

	len -= tailsize;
	kernel_buf = kmalloc(len, GFP_KERNEL);

	if (kernel_buf == NULL)
		return -ENOMEM;

	if (down_interruptible(&vtunerc->tswrite_sem))
		return -ERESTARTSYS;

	if (copy_from_user(kernel_buf, buff, len)) {
		printk(PRINTK_ERR "%s: ERR: in userdata passing\n", __func__);
		up(&vtunerc->tswrite_sem);
		return -EINVAL;
	}

	if (tscheck) {
		int i;

		for (i = 0; i < len; i += 188)
			if (kernel_buf[i] != 0x47) { /* start of TS packet */
				printk(PRINTK_ERR "%s: ERR: Data not start on packet boundary: index=%d data=%02x %02x %02x %02x %02x ...\n",
						__func__, i / 188, kernel_buf[i], kernel_buf[i + 1],
						kernel_buf[i + 2], kernel_buf[i + 3], kernel_buf[i + 4]);
				up(&vtunerc->tswrite_sem);
				return -EINVAL;
			}
	}

	vtunerc->stat_wr_data += len;
	dvb_dmx_swfilter_packets(demux, kernel_buf, len / 188);

	up(&vtunerc->tswrite_sem);

#ifdef CONFIG_PROC_FS
	/* TODO:  analyze injected data for statistics */
#endif

	kfree(kernel_buf);

	return len;
}

static ssize_t vtunerc_ctrldev_read(struct file *filp, char __user *buff,
		size_t len, loff_t *off)
{
	struct vtunerc_ctx *vtunerc = filp->private_data;

	vtunerc->stat_rd_data += len;

	/* read op is not using in current vtuner protocol */
	return 0 ;
}

static int vtunerc_ctrldev_open(struct inode *inode, struct file *filp)
{
	struct vtunerc_ctx *vtunerc;
	int minor;

	minor = MINOR(inode->i_rdev);
	vtunerc = filp->private_data = vtunerc_get_ctx(minor);
	if (vtunerc == NULL)
		return -ENOMEM;

	vtunerc->stat_ctrl_sess++;

	/*FIXME: clear pidtab */

	vtunerc->fd_opened++;
	vtunerc->closing = 0;

	return 0;
}

static int vtunerc_ctrldev_close(struct inode *inode, struct file *filp)
{
	struct vtunerc_ctx *vtunerc = filp->private_data;
	int minor;
	struct vtuner_message fakemsg;

	vtunerc->fd_opened--;
	vtunerc->closing = 1;

	minor = MINOR(inode->i_rdev);

	/* set FAKE response, to allow finish any waiters
	   in vtunerc_ctrldev_xchange_message() */
	vtunerc->ctrldev_response.type = 0;
	wake_up_interruptible(&vtunerc->ctrldev_wait_response_wq);

	/* clear pidtab */
	if (down_interruptible(&vtunerc->xchange_sem))
		return -ERESTARTSYS;
	memset(&fakemsg, 0, sizeof(fakemsg));
	vtunerc_ctrldev_xchange_message(vtunerc, &fakemsg, 0);
	up(&vtunerc->xchange_sem);

	return 0;
}

static long vtunerc_ctrldev_ioctl(struct file *file, unsigned int cmd,
					unsigned long arg)
{
	struct vtunerc_ctx *vtunerc = file->private_data;
	int len, ret = 0;
	int i;

	if (vtunerc->closing)
		return -EINTR;

	if (down_interruptible(&vtunerc->ioctl_sem))
		return -ERESTARTSYS;

	switch (cmd) {
	case VTUNER_SET_NAME:
		len = strlen((char *)arg) + 1;
		vtunerc->name = kmalloc(len, GFP_KERNEL);
		if (vtunerc->name == NULL) {
			printk(PRINTK_ERR "%s returns no mem\n", __func__);
			ret = -ENOMEM;
			break;
		}
		if (copy_from_user(vtunerc->name, (char *)arg, len)) {
			ret = -EFAULT;
			break;
		}
		break;

	case VTUNER_SET_MODES:
		for (i = 0; i < vtunerc->num_modes; i++)
			vtunerc->ctypes[i] = &(((char *)(arg))[i*32]);
		if (vtunerc->num_modes != 1) {
			printk(PRINTK_ERR "%s currently supported only num_modes = 1!\n",
					__func__);
			break;
		}
		/* follow into old code for compatibility */

	case VTUNER_SET_TYPE:
		if (strcasecmp((char *)arg, "DVB-S") == 0) {
			vtunerc->vtype = VT_S;
			printk(PRINTK_NOTICE "%s setting DVB-S tuner vtype\n",
					__func__);
		} else
		if (strcasecmp((char *)arg, "DVB-S2") == 0) {
			vtunerc->vtype = VT_S2;
			printk(PRINTK_NOTICE "%s setting DVB-S2 tuner vtype\n",
					__func__);
		} else
		if (strcasecmp((char *)arg, "DVB-T") == 0) {
			vtunerc->vtype = VT_T;
			printk(PRINTK_NOTICE "%s setting DVB-T tuner vtype\n",
					__func__);
		} else
		if (strcasecmp((char *)arg, "DVB-C") == 0) {
			vtunerc->vtype = VT_C;
			printk(PRINTK_NOTICE "%s setting DVB-C tuner vtype\n",
					__func__);
		} else {
			printk(PRINTK_ERR "%s unregognized tuner vtype '%s'\n",
					__func__, (char *)arg);
			ret = -ENODEV;
			break;
		}

		if ((vtunerc_frontend_init(vtunerc))) {
			vtunerc->vtype = 0;
			printk(PRINTK_ERR "%s failed to initialize tuner's internals\n",
					__func__);
			ret = -ENODEV;
			break;
		}

		break;


	case VTUNER_SET_FE_INFO:
		len = sizeof(struct dvb_frontend_info);
		vtunerc->feinfo = kmalloc(len, GFP_KERNEL);
		if (vtunerc->feinfo == NULL) {
			printk(PRINTK_ERR "%s return no mem<\n", __func__);
			ret = -ENOMEM;
			break;
		}
		if (copy_from_user(vtunerc->feinfo, (char *)arg, len)) {
			ret = -EFAULT;
			break;
		}
		break;

	case VTUNER_GET_MESSAGE:
		if (wait_event_interruptible(vtunerc->ctrldev_wait_request_wq,
					vtunerc->ctrldev_request.type != -1)) {
			ret = -ERESTARTSYS;
			break;
		}

		BUG_ON(vtunerc->ctrldev_request.type == -1);

		if (copy_to_user((char *)arg, &vtunerc->ctrldev_request,
					VTUNER_MSG_LEN)) {
			ret = -EFAULT;
			break;
		}

		vtunerc->ctrldev_request.type = -1;

		if (vtunerc->noresponse)
			up(&vtunerc->xchange_sem);

		break;

	case VTUNER_SET_RESPONSE:
		if (copy_from_user(&vtunerc->ctrldev_response, (char *)arg,
					VTUNER_MSG_LEN)) {
			ret = -EFAULT;
		}
		wake_up_interruptible(&vtunerc->ctrldev_wait_response_wq);

		break;

	case VTUNER_SET_NUM_MODES:
		vtunerc->num_modes = (int) arg;
		break;

	default:
		printk(PRINTK_ERR "vtunerc: ERR: unknown IOCTL 0x%x\n", cmd);
		ret = -ENOTTY; /* Linus: the only correct one return value for unsupported ioctl */

		break;
	}
	up(&vtunerc->ioctl_sem);

	return ret;
}

static unsigned int vtunerc_ctrldev_poll(struct file *filp, poll_table *wait)
{
	struct vtunerc_ctx *vtunerc = filp->private_data;
	unsigned int mask = 0;

	if (vtunerc->closing)
		return -EINTR;

	poll_wait(filp, &vtunerc->ctrldev_wait_request_wq, wait);

	if (vtunerc->ctrldev_request.type >= -1 ||
			vtunerc->ctrldev_response.type >= -1) {
		mask = POLLPRI;
	}

  return mask;
}

/* ------------------------------------------------ */

static const struct file_operations vtunerc_ctrldev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = vtunerc_ctrldev_ioctl,
	.write = vtunerc_ctrldev_write,
	.read  = vtunerc_ctrldev_read,
	.poll  = (void *) vtunerc_ctrldev_poll,
	.open  = vtunerc_ctrldev_open,
	.release  = vtunerc_ctrldev_close
};

static struct class *pclass;
static struct cdev cdev;
static dev_t chdev;
extern int devices;

int vtunerc_register_ctrldev()
{
	int idx;

	chdev = MKDEV(VTUNERC_CTRLDEV_MAJOR, 0);

	if (register_chrdev_region(chdev, devices, VTUNERC_CTRLDEV_NAME)) {
		printk(PRINTK_ERR "vtunerc: ERR: unable to get major %d\n",
				VTUNERC_CTRLDEV_MAJOR);
		return -EINVAL;
	}

	cdev_init(&cdev, &vtunerc_ctrldev_fops);

	cdev.owner = THIS_MODULE;
	cdev.ops = &vtunerc_ctrldev_fops;

	if (cdev_add(&cdev, chdev, devices) < 0)
		printk(PRINTK_WARN "vtunerc: WARN: unable to create dev\n");

	pclass = class_create(THIS_MODULE, "vtuner");
	if (IS_ERR(pclass)) {
		printk(PRINTK_ERR "vtunerc: ERR: unable to register major %d\n",
				VTUNERC_CTRLDEV_MAJOR);
		return PTR_ERR(pclass);
	}

	for (idx = 0; idx < devices; idx++) {
		struct device *clsdev;

		clsdev = device_create(pclass, NULL,
				MKDEV(VTUNERC_CTRLDEV_MAJOR, idx),
				/*ctx*/ NULL, "vtunerc%d", idx);

		printk(PRINTK_NOTICE "vtunerc: registered /dev/vtunerc%d\n",
				idx);
	}

	return 0;
}

void vtunerc_unregister_ctrldev()
{
	int idx;

	printk(PRINTK_NOTICE "vtunerc: unregistering\n");

	unregister_chrdev_region(chdev, devices);

	for (idx = 0; idx < devices; idx++)
		device_destroy(pclass, MKDEV(VTUNERC_CTRLDEV_MAJOR, idx));

	cdev_del(&cdev);

	class_destroy(pclass);
}


int vtunerc_ctrldev_xchange_message(struct vtunerc_ctx *vtunerc,
		struct vtuner_message *msg, int wait4response)
{
	if (down_interruptible(&vtunerc->xchange_sem))
		return -ERESTARTSYS;

	if (vtunerc->fd_opened < 1) {
		up(&vtunerc->xchange_sem);
		return 0;
	}

	BUG_ON(vtunerc->ctrldev_request.type != -1);

	memcpy(&vtunerc->ctrldev_request, msg, sizeof(struct vtuner_message));
	vtunerc->ctrldev_response.type = -1;
	vtunerc->noresponse = !wait4response;
	wake_up_interruptible(&vtunerc->ctrldev_wait_request_wq);

	if (!wait4response)
		return 0;

	if (wait_event_interruptible(vtunerc->ctrldev_wait_response_wq,
				vtunerc->ctrldev_response.type != -1)) {
		up(&vtunerc->xchange_sem);
		return -ERESTARTSYS;
	}

	BUG_ON(vtunerc->ctrldev_response.type == -1);

	memcpy(msg, &vtunerc->ctrldev_response, sizeof(struct vtuner_message));
	vtunerc->ctrldev_response.type = -1;

	up(&vtunerc->xchange_sem);

	return 0;
}
