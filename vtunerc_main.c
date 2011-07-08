/*
 * vtunerc: Virtual adapter driver
 *
 * Copyright (C) 2010-11 Honza Petrous <jpetrous@smartimp.cz>
 * [Created 2010-03-23]
 * Sponsored by Smartimp s.r.o. for its NessieDVB.com box
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

#include <linux/module.h>	/* Specifically, a module */
#include <linux/kernel.h>	/* We're doing kernel work */
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <asm/uaccess.h>
#include <linux/delay.h>

#include "demux.h"
#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"
#include "dvbdev.h"

#include "vtuner.h"

#include "vtunerc_priv.h"

#define VTUNERC_MODULE_VERSION "1.1"

#define MSGHEADER "[vtunerc]: "

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define DRIVER_NAME		"vTuner proxy"

#define VTUNERC_PROC_FILENAME	"vtunerc%i"

#define VTUNERC_MAX_ADAPTERS	4

static struct vtunerc_ctx *vtunerc_tbl[VTUNERC_MAX_ADAPTERS] = { NULL };

int devices = 1;
int tscheck = 0;

static int pidtab_find_index(unsigned short *pidtab, int pid)
{
	int i = 0;

	while (i < MAX_PIDTAB_LEN) {
		if (pidtab[i] == pid)
			return i;
		i++;
	}

	return -1;
}

static int pidtab_add_pid(unsigned short *pidtab, int pid)
{
	int i;

	/* TODO: speed-up hint: add pid sorted */

	for (i = 0; i < MAX_PIDTAB_LEN; i++)
		if (pidtab[i] == PID_UNKNOWN) {
			pidtab[i] = pid;
			return 0;
		}

	return -1;
}

static int pidtab_del_pid(unsigned short *pidtab, int pid)
{
	int i;

	/* TODO: speed-up hint: delete sorted */

	for (i = 0; i < MAX_PIDTAB_LEN; i++)
		if (pidtab[i] == pid) {
			pidtab[i] = PID_UNKNOWN;
			/* TODO: move rest */
			return 0;
		}

	return -1;
}

static void pidtab_copy_to_msg(struct vtunerc_ctx *vtunerc,
				struct vtuner_message *msg)
{
	int i;

	for (i = 0; i < (MAX_PIDTAB_LEN - 1); i++)
		msg->body.pidlist[i] = vtunerc->pidtab[i]; /*TODO: optimize it*/
	msg->body.pidlist[MAX_PIDTAB_LEN - 1] = 0;
}

static int vtunerc_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct vtunerc_ctx *vtunerc = demux->priv;
	struct vtuner_message msg;

	switch (feed->type) {
	case DMX_TYPE_TS:
		break;
	case DMX_TYPE_SEC:
		break;
	case DMX_TYPE_PES:
		printk(MSGHEADER " feed type PES is not supported\n");
		return -EINVAL;
	default:
		printk(MSGHEADER " feed type %d is not supported\n",
				feed->type);
		return -EINVAL;
	}

	/* organize PID list table */

	if (pidtab_find_index(vtunerc->pidtab, feed->pid) < 0) {
		pidtab_add_pid(vtunerc->pidtab, feed->pid);

		pidtab_copy_to_msg(vtunerc, &msg);

		msg.type = MSG_PIDLIST;
		vtunerc_ctrldev_xchange_message(vtunerc, &msg, 0);
	}

	return 0;
}

static int vtunerc_stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct vtunerc_ctx *vtunerc = demux->priv;
	struct vtuner_message msg;

	/* organize PID list table */

	if (pidtab_find_index(vtunerc->pidtab, feed->pid) > -1) {
		pidtab_del_pid(vtunerc->pidtab, feed->pid);

		pidtab_copy_to_msg(vtunerc, &msg);

		msg.type = MSG_PIDLIST;
		vtunerc_ctrldev_xchange_message(vtunerc, &msg, 0);
	}

	return 0;
}

/* ----------------------------------------------------------- */


#ifdef CONFIG_PROC_FS
#define MAXBUF 512
/**
 * @brief  procfs file handler
 * @param  buffer:
 * @param  start:
 * @param  offset:
 * @param  size:
 * @param  eof:
 * @param  data:
 * @return =0: success <br/>
 *         <0: if any error occur
 */
int vtunerc_read_proc(char *buffer, char **start, off_t offset, int size,
			int *eof, void *data)
{
	char outbuf[MAXBUF] = "[ vtunerc driver, version "
				VTUNERC_MODULE_VERSION " ]\n";
	int blen, i, pcnt;
	struct vtunerc_ctx *vtunerc = (struct vtunerc_ctx *)data;

	blen = strlen(outbuf);
	sprintf(outbuf+blen, "  sessions: %u\n", vtunerc->stat_ctrl_sess);
	blen = strlen(outbuf);
	sprintf(outbuf+blen, "  read    : %u\n", vtunerc->stat_rd_data);
	blen = strlen(outbuf);
	sprintf(outbuf+blen, "  write   : %u\n", vtunerc->stat_wr_data);
	blen = strlen(outbuf);
	sprintf(outbuf+blen, "  PID tab :");
	pcnt = 0;
	for (i = 0; i < MAX_PIDTAB_LEN; i++) {
		blen = strlen(outbuf);
		if (vtunerc->pidtab[i] != PID_UNKNOWN) {
			sprintf(outbuf+blen, " %x", vtunerc->pidtab[i]);
			pcnt++;
		}
	}
	blen = strlen(outbuf);
	sprintf(outbuf+blen, " (len=%d)\n", pcnt);

	blen = strlen(outbuf);

	if (size < blen)
		return -EINVAL;

	if (offset != 0)
		return 0;

	strcpy(buffer, outbuf);

	/* signal EOF */
	*eof = 1;

	return blen;

}
#endif

static char *my_strdup(const char *s)
{
	char *rv = kmalloc(strlen(s)+1, GFP_KERNEL);
	if (rv)
		strcpy(rv, s);
	return rv;
}

struct vtunerc_ctx *vtunerc_get_ctx(int minor)
{
	if (minor >= VTUNERC_MAX_ADAPTERS)
		return NULL;

	return vtunerc_tbl[minor];
}

static int __init vtunerc_init(void)
{
	struct vtunerc_ctx *vtunerc;
	struct dvb_demux *dvbdemux;
	struct dmx_demux *dmx;
	int ret = -EINVAL, i, idx;

	printk(KERN_INFO "vTunerc DVB multi adapter driver, version "
			VTUNERC_MODULE_VERSION
			", (c) 2010-11 Honza Petrous, SmartImp.cz\n");

	request_module("dvb-core"); /* FIXME: dunno which way it should work :-/ */

	for (idx = 0; idx < devices; idx++) {
		vtunerc = kzalloc(sizeof(struct vtunerc_ctx), GFP_KERNEL);
		if (!vtunerc)
			return -ENOMEM;

		vtunerc_tbl[idx] = vtunerc;

		vtunerc->idx = idx;
		vtunerc->ctrldev_request.type = -1;
		vtunerc->ctrldev_response.type = -1;
		init_waitqueue_head(&vtunerc->ctrldev_wait_request_wq);
		init_waitqueue_head(&vtunerc->ctrldev_wait_response_wq);

		/* dvb */

		/* create new adapter */
		ret = dvb_register_adapter(&vtunerc->dvb_adapter, DRIVER_NAME,
					   THIS_MODULE, NULL, adapter_nr);
		if (ret < 0)
			goto err_kfree;

		vtunerc->dvb_adapter.priv = vtunerc;

		memset(&vtunerc->demux, 0, sizeof(vtunerc->demux));
		dvbdemux = &vtunerc->demux;
		dvbdemux->priv = vtunerc;
		dvbdemux->filternum = MAX_PIDTAB_LEN;
		dvbdemux->feednum = MAX_PIDTAB_LEN;
		dvbdemux->start_feed = vtunerc_start_feed;
		dvbdemux->stop_feed = vtunerc_stop_feed;
		dvbdemux->dmx.capabilities = 0;
		ret = dvb_dmx_init(dvbdemux);
		if (ret < 0)
			goto err_dvb_unregister_adapter;

		dmx = &dvbdemux->dmx;

		vtunerc->hw_frontend.source = DMX_FRONTEND_0;
		vtunerc->mem_frontend.source = DMX_MEMORY_FE;
		vtunerc->dmxdev.filternum = MAX_PIDTAB_LEN;
		vtunerc->dmxdev.demux = dmx;

		ret = dvb_dmxdev_init(&vtunerc->dmxdev, &vtunerc->dvb_adapter);
		if (ret < 0)
			goto err_dvb_dmx_release;

		ret = dmx->add_frontend(dmx, &vtunerc->hw_frontend);
		if (ret < 0)
			goto err_dvb_dmxdev_release;

		ret = dmx->add_frontend(dmx, &vtunerc->mem_frontend);
		if (ret < 0)
			goto err_remove_hw_frontend;

		ret = dmx->connect_frontend(dmx, &vtunerc->hw_frontend);
		if (ret < 0)
			goto err_remove_mem_frontend;

		sema_init(&vtunerc->xchange_sem, 1);
		sema_init(&vtunerc->ioctl_sem, 1);
		sema_init(&vtunerc->tswrite_sem, 1);

		/* init pid table */
		for (i = 0; i < MAX_PIDTAB_LEN; i++)
			vtunerc->pidtab[i] = PID_UNKNOWN;

#ifdef CONFIG_PROC_FS
		{
			char procfilename[64];

			sprintf(procfilename, VTUNERC_PROC_FILENAME,
					vtunerc->idx);
			vtunerc->procname = my_strdup(procfilename);
			if (create_proc_read_entry(vtunerc->procname, 0, NULL,
							vtunerc_read_proc,
							vtunerc) == 0)
				printk(MSGHEADER
					"Unable to register '%s' proc file\n",
					vtunerc->procname);
		}
#endif
	}

	vtunerc_register_ctrldev();

out:
	return ret;

	dmx->disconnect_frontend(dmx);
err_remove_mem_frontend:
	dmx->remove_frontend(dmx, &vtunerc->mem_frontend);
err_remove_hw_frontend:
	dmx->remove_frontend(dmx, &vtunerc->hw_frontend);
err_dvb_dmxdev_release:
	dvb_dmxdev_release(&vtunerc->dmxdev);
err_dvb_dmx_release:
	dvb_dmx_release(dvbdemux);
err_dvb_unregister_adapter:
	dvb_unregister_adapter(&vtunerc->dvb_adapter);
err_kfree:
	kfree(vtunerc);
	goto out;
}

static void __exit vtunerc_exit(void)
{
	struct dvb_demux *dvbdemux;
	struct dmx_demux *dmx;
	int idx;

	vtunerc_unregister_ctrldev();

	for (idx = 0; idx < devices; idx++) {
		struct vtunerc_ctx *vtunerc = vtunerc_tbl[idx];
#ifdef CONFIG_PROC_FS
		remove_proc_entry(vtunerc->procname, NULL);
		kfree(vtunerc->procname);
#endif

		vtunerc_frontend_clear(vtunerc);

		dvbdemux = &vtunerc->demux;
		dmx = &dvbdemux->dmx;

		dmx->disconnect_frontend(dmx);
		dmx->remove_frontend(dmx, &vtunerc->mem_frontend);
		dmx->remove_frontend(dmx, &vtunerc->hw_frontend);
		dvb_dmxdev_release(&vtunerc->dmxdev);
		dvb_dmx_release(dvbdemux);
		dvb_unregister_adapter(&vtunerc->dvb_adapter);
		kfree(vtunerc);
	}

	printk(MSGHEADER " unloaded successfully\n");
}

module_init(vtunerc_init);
module_exit(vtunerc_exit);

MODULE_AUTHOR("Honza Petrous");
MODULE_DESCRIPTION("virtual DVB device");
MODULE_LICENSE("GPL");
MODULE_VERSION(VTUNERC_MODULE_VERSION);

module_param(devices, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(devices, "Number of virtual adapters (default is 1)");

module_param(tscheck, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(tscheck, "Check TS packet validity (default is 0)");

