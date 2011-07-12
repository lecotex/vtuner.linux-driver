/*
 * vtunerc: Driver for Proxy Frontend
 *
 * Copyright (C) 2010-11 Honza Petrous <jpetrous@smartimp.cz>
 * [Inspired on proxy frontend by Emard <emard@softhome.net>]
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"

#include "vtunerc_priv.h"

struct dvb_proxyfe_state {
	struct dvb_frontend frontend;
	struct vtunerc_ctx *ctx;
};


static int dvb_proxyfe_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	struct vtuner_message msg;

	msg.type = MSG_READ_STATUS;
	vtunerc_ctrldev_xchange_message(ctx, &msg, 1);

	*status = msg.body.status;

	return 0;
}

static int dvb_proxyfe_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	struct vtuner_message msg;

	msg.type = MSG_READ_BER;
	vtunerc_ctrldev_xchange_message(ctx, &msg, 1);

	*ber = msg.body.ber;

	return 0;
}

static int dvb_proxyfe_read_signal_strength(struct dvb_frontend *fe,
						u16 *strength)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	struct vtuner_message msg;

	msg.type = MSG_READ_SIGNAL_STRENGTH;
	vtunerc_ctrldev_xchange_message(ctx, &msg, 1);

	*strength = msg.body.ss;

	return 0;
}

static int dvb_proxyfe_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	struct vtuner_message msg;

	msg.type = MSG_READ_SNR;
	vtunerc_ctrldev_xchange_message(ctx, &msg, 1);

	*snr = msg.body.snr;

	return 0;
}

static int dvb_proxyfe_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	struct vtuner_message msg;

	msg.type = MSG_READ_UCBLOCKS;
	vtunerc_ctrldev_xchange_message(ctx, &msg, 1);

	*ucblocks = msg.body.ucb;

	return 0;
}

static int dvb_proxyfe_get_frontend(struct dvb_frontend *fe,
					struct dvb_frontend_parameters *p)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	struct vtuner_message msg;

	msg.type = MSG_GET_FRONTEND;
	vtunerc_ctrldev_xchange_message(ctx, &msg, 1);

	switch (ctx->vtype) {
	case VT_S:
	case VT_S2:
		/*FIXME*/
		{
			struct dvb_qpsk_parameters *op = &p->u.qpsk;

			op->symbol_rate = msg.body.fe_params.u.qpsk.symbol_rate;
			op->fec_inner = msg.body.fe_params.u.qpsk.fec_inner;
		}
		break;
	case VT_T:
		{
			struct dvb_ofdm_parameters *op = &p->u.ofdm;

			op->bandwidth = msg.body.fe_params.u.ofdm.bandwidth;
			op->code_rate_HP = msg.body.fe_params.u.ofdm.code_rate_HP;
			op->code_rate_LP = msg.body.fe_params.u.ofdm.code_rate_LP;
			op->constellation = msg.body.fe_params.u.ofdm.constellation;
			op->transmission_mode = msg.body.fe_params.u.ofdm.transmission_mode;
			op->guard_interval = msg.body.fe_params.u.ofdm.guard_interval;
			op->hierarchy_information = msg.body.fe_params.u.ofdm.hierarchy_information;
		}
		break;
	case VT_C:
		/* FIXME: untested */
		{
			struct dvb_qam_parameters *op = &p->u.qam;

			op->symbol_rate = msg.body.fe_params.u.qam.symbol_rate;
			op->fec_inner = msg.body.fe_params.u.qam.fec_inner;
			op->modulation = msg.body.fe_params.u.qam.modulation;
		}
		break;
	default:
		printk(KERN_ERR "vtunerc%d: unregognized tuner vtype = %d\n", ctx->idx,
				ctx->vtype);
		return -EINVAL;
	}
	p->frequency = msg.body.fe_params.frequency;
	p->inversion = msg.body.fe_params.inversion;
	return 0;
}

static int dvb_proxyfe_set_frontend(struct dvb_frontend *fe,
					struct dvb_frontend_parameters *p)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	struct vtuner_message msg;

	memset(&msg, 0, sizeof(msg));
	msg.body.fe_params.frequency = p->frequency;
	msg.body.fe_params.inversion = p->inversion;

	switch (ctx->vtype) {
	case VT_S:
	case VT_S2:
		{
			struct dvb_qpsk_parameters *op = &p->u.qpsk;
			struct dtv_frontend_properties *props = &fe->dtv_property_cache;

			msg.body.fe_params.u.qpsk.symbol_rate = op->symbol_rate;
			msg.body.fe_params.u.qpsk.fec_inner = op->fec_inner;

			if (ctx->vtype == VT_S2 && props->delivery_system == SYS_DVBS2) {
				/* DELIVERY SYSTEM: S2 delsys in use */
				msg.body.fe_params.u.qpsk.fec_inner = 9;

				/* MODULATION */
				if (props->modulation == PSK_8)
					/* signal PSK_8 modulation used */
					msg.body.fe_params.u.qpsk.fec_inner += 9;

				/* FEC */
				switch (props->fec_inner) {
				case FEC_1_2:
					msg.body.fe_params.u.qpsk.fec_inner += 1;
					break;
				case FEC_2_3:
					msg.body.fe_params.u.qpsk.fec_inner += 2;
					break;
				case FEC_3_4:
					msg.body.fe_params.u.qpsk.fec_inner += 3;
					break;
				case FEC_4_5:
					msg.body.fe_params.u.qpsk.fec_inner += 8;
					break;
				case FEC_5_6:
					msg.body.fe_params.u.qpsk.fec_inner += 4;
					break;
				/*case FEC_6_7: // undefined
					msg.body.fe_params.u.qpsk.fec_inner += 2;
					break;*/
				case FEC_7_8:
					msg.body.fe_params.u.qpsk.fec_inner += 5;
					break;
				case FEC_8_9:
					msg.body.fe_params.u.qpsk.fec_inner += 6;
					break;
				/*case FEC_AUTO: // undefined
					msg.body.fe_params.u.qpsk.fec_inner += 2;
					break;*/
				case FEC_3_5:
					msg.body.fe_params.u.qpsk.fec_inner += 7;
					break;
				case FEC_9_10:
					msg.body.fe_params.u.qpsk.fec_inner += 9;
					break;
				default:
					; /*FIXME: what now? */
					break;
				}

				/* ROLLOFF */
				switch (props->rolloff) {
				case ROLLOFF_20:
					msg.body.fe_params.inversion |= 0x08;
					break;
				case ROLLOFF_25:
					msg.body.fe_params.inversion |= 0x04;
					break;
				case ROLLOFF_35:
				default:
					break;
				}

				/* PILOT */
				switch (props->pilot) {
				case PILOT_ON:
					msg.body.fe_params.inversion |= 0x10;
					break;
				case PILOT_AUTO:
					msg.body.fe_params.inversion |= 0x20;
					break;
				case PILOT_OFF:
				default:
					break;
				}
			}
		}
		break;
	case VT_T:
		{
			struct dvb_ofdm_parameters *op = &p->u.ofdm;

			msg.body.fe_params.u.ofdm.bandwidth = op->bandwidth;
			msg.body.fe_params.u.ofdm.code_rate_HP = op->code_rate_HP;
			msg.body.fe_params.u.ofdm.code_rate_LP = op->code_rate_LP;
			msg.body.fe_params.u.ofdm.constellation = op->constellation;
			msg.body.fe_params.u.ofdm.transmission_mode = op->transmission_mode;
			msg.body.fe_params.u.ofdm.guard_interval = op->guard_interval;
			msg.body.fe_params.u.ofdm.hierarchy_information = op->hierarchy_information;
		}
		break;
	case VT_C:
		/* FIXME: untested */
		{
			struct dvb_qam_parameters *op = &p->u.qam;

			msg.body.fe_params.u.qam.symbol_rate = op->symbol_rate;
			msg.body.fe_params.u.qam.fec_inner = op->fec_inner;
			msg.body.fe_params.u.qam.modulation = op->modulation;
		}
		break;
	default:
		printk(KERN_ERR "vtunerc%d: unregognized tuner vtype = %d\n",
				ctx->idx, ctx->vtype);
		return -EINVAL;
	}

	msg.type = MSG_SET_FRONTEND;
	vtunerc_ctrldev_xchange_message(ctx, &msg, 1);

	return 0;
}

static int dvb_proxyfe_get_property(struct dvb_frontend *fe, struct dtv_property* tvp)
{
	return 0;
}

static enum dvbfe_algo dvb_proxyfe_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_SW;
}

static int dvb_proxyfe_sleep(struct dvb_frontend *fe)
{
	return 0;
}

static int dvb_proxyfe_init(struct dvb_frontend *fe)
{
	return 0;
}

static int dvb_proxyfe_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	struct vtuner_message msg;

	msg.body.tone = tone;
	msg.type = MSG_SET_TONE;
	vtunerc_ctrldev_xchange_message(ctx, &msg, 1);

	return 0;
}

static int dvb_proxyfe_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	struct vtuner_message msg;

	msg.body.voltage = voltage;
	msg.type = MSG_SET_VOLTAGE;
	vtunerc_ctrldev_xchange_message(ctx, &msg, 1);

	return 0;
}

static int dvb_proxyfe_send_diseqc_msg(struct dvb_frontend *fe, struct dvb_diseqc_master_cmd *cmd)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	struct vtuner_message msg;

	memcpy(&msg.body.diseqc_master_cmd, cmd, sizeof(struct dvb_diseqc_master_cmd));
	msg.type = MSG_SEND_DISEQC_MSG;
	vtunerc_ctrldev_xchange_message(ctx, &msg, 1);

	return 0;
}

static int dvb_proxyfe_send_diseqc_burst(struct dvb_frontend *fe, fe_sec_mini_cmd_t burst)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	struct vtuner_message msg;

	msg.body.burst = burst;
	msg.type = MSG_SEND_DISEQC_BURST;
	vtunerc_ctrldev_xchange_message(ctx, &msg, 1);

	return 0;
}

static void dvb_proxyfe_release(struct dvb_frontend *fe)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;

	kfree(state);
}

static struct dvb_frontend_ops dvb_proxyfe_ofdm_ops;

static struct dvb_frontend *dvb_proxyfe_ofdm_attach(struct vtunerc_ctx *ctx)
{
	struct dvb_proxyfe_state *state = NULL;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct dvb_proxyfe_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &dvb_proxyfe_ofdm_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	state->ctx = ctx;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops dvb_proxyfe_qpsk_ops;

static struct dvb_frontend *dvb_proxyfe_qpsk_attach(struct vtunerc_ctx *ctx, int can_2g_modulation)
{
	struct dvb_proxyfe_state *state = NULL;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct dvb_proxyfe_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &dvb_proxyfe_qpsk_ops, sizeof(struct dvb_frontend_ops));
	if (can_2g_modulation) {
		state->frontend.ops.info.caps |= FE_CAN_2G_MODULATION;
		strcpy(state->frontend.ops.info.name, "vTuner proxyFE DVB-S2");
	}
	state->frontend.demodulator_priv = state;
	state->ctx = ctx;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops dvb_proxyfe_qam_ops;

static struct dvb_frontend *dvb_proxyfe_qam_attach(struct vtunerc_ctx *ctx)
{
	struct dvb_proxyfe_state *state = NULL;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct dvb_proxyfe_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &dvb_proxyfe_qam_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	state->ctx = ctx;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops dvb_proxyfe_ofdm_ops = {

	.info = {
		.name			= "vTuner proxyFE DVB-T",
		.type			= FE_OFDM,
		.frequency_min		= 51000000,
		.frequency_max		= 863250000,
		.frequency_stepsize	= 62500,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
				FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
				FE_CAN_FEC_7_8 | FE_CAN_FEC_8_9 | FE_CAN_FEC_AUTO |
				FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
				FE_CAN_TRANSMISSION_MODE_AUTO |
				FE_CAN_GUARD_INTERVAL_AUTO |
				FE_CAN_HIERARCHY_AUTO,
	},

	.release = dvb_proxyfe_release,

	.init = dvb_proxyfe_init,
	.sleep = dvb_proxyfe_sleep,

	.set_frontend = dvb_proxyfe_set_frontend,
	.get_frontend = dvb_proxyfe_get_frontend,

	.read_status = dvb_proxyfe_read_status,
	.read_ber = dvb_proxyfe_read_ber,
	.read_signal_strength = dvb_proxyfe_read_signal_strength,
	.read_snr = dvb_proxyfe_read_snr,
	.read_ucblocks = dvb_proxyfe_read_ucblocks,
};

static struct dvb_frontend_ops dvb_proxyfe_qam_ops = {

	.info = {
		.name			= "vTuner proxyFE DVB-C",
		.type			= FE_QAM,
		.frequency_stepsize	= 62500,
		.frequency_min		= 51000000,
		.frequency_max		= 858000000,
		.symbol_rate_min	= (57840000/2)/64,     /* SACLK/64 == (XIN/2)/64 */
		.symbol_rate_max	= (57840000/2)/4,      /* SACLK/4 */
		.caps = FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 |
			FE_CAN_QAM_128 | FE_CAN_QAM_256 |
			FE_CAN_FEC_AUTO | FE_CAN_INVERSION_AUTO
	},

	.release = dvb_proxyfe_release,

	.init = dvb_proxyfe_init,
	.sleep = dvb_proxyfe_sleep,

	.set_frontend = dvb_proxyfe_set_frontend,
	.get_frontend = dvb_proxyfe_get_frontend,

	.read_status = dvb_proxyfe_read_status,
	.read_ber = dvb_proxyfe_read_ber,
	.read_signal_strength = dvb_proxyfe_read_signal_strength,
	.read_snr = dvb_proxyfe_read_snr,
	.read_ucblocks = dvb_proxyfe_read_ucblocks,
};

static struct dvb_frontend_ops dvb_proxyfe_qpsk_ops = {

	.info = {
		.name			= "vTuner proxyFE DVB-S",
		.type			= FE_QPSK,
		.frequency_min		= 950000,
		.frequency_max		= 2150000,
		.frequency_stepsize	= 250,           /* kHz for QPSK frontends */
		.frequency_tolerance	= 29500,
		.symbol_rate_min	= 1000000,
		.symbol_rate_max	= 45000000,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK
	},

	.release = dvb_proxyfe_release,

	.init = dvb_proxyfe_init,
	.sleep = dvb_proxyfe_sleep,

	.get_frontend = dvb_proxyfe_get_frontend,
	.get_property = dvb_proxyfe_get_property,
	.get_frontend_algo = dvb_proxyfe_get_frontend_algo,
	.set_frontend = dvb_proxyfe_set_frontend,

	.read_status = dvb_proxyfe_read_status,
	.read_ber = dvb_proxyfe_read_ber,
	.read_signal_strength = dvb_proxyfe_read_signal_strength,
	.read_snr = dvb_proxyfe_read_snr,
	.read_ucblocks = dvb_proxyfe_read_ucblocks,

	.set_voltage = dvb_proxyfe_set_voltage,
	.set_tone = dvb_proxyfe_set_tone,

	.diseqc_send_master_cmd         = dvb_proxyfe_send_diseqc_msg,
	.diseqc_send_burst              = dvb_proxyfe_send_diseqc_burst,

};

int /*__devinit*/ vtunerc_frontend_init(struct vtunerc_ctx *ctx)
{
	int ret;

	if (ctx->fe) {
		printk(KERN_NOTICE "vtunerc%d: frontend already initialized as type=%d\n",
				ctx->idx, ctx->vtype);
		return 0;
	}

	switch (ctx->vtype) {
	case VT_S:
		ctx->fe = dvb_proxyfe_qpsk_attach(ctx, 0);
		break;
	case VT_S2:
		ctx->fe = dvb_proxyfe_qpsk_attach(ctx, 1);
		break;
	case VT_T:
		ctx->fe = dvb_proxyfe_ofdm_attach(ctx);
		break;
	case VT_C:
		ctx->fe = dvb_proxyfe_qam_attach(ctx);
		break;
	default:
		printk(KERN_ERR "vtunerc%d: unregognized tuner vtype = %d\n",
				ctx->idx, ctx->vtype);
		return -EINVAL;
	}
	ret = dvb_register_frontend(&ctx->dvb_adapter, ctx->fe);

	return 0;
}

int /*__devinit*/ vtunerc_frontend_clear(struct vtunerc_ctx *ctx)
{
	return ctx->fe ? dvb_unregister_frontend(ctx->fe) : 0;
}
