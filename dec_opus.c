/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / Opus audio decoder filter
 *  based on libopus (https://opus-codec.org/)
 *
 *  This decoder expects Opus packets already demultiplexed and framed by
 *  an upstream reframer (e.g. the "oggdmx" filter, which already parses
 *  the OpusHead header and sets SAMPLE_RATE/NUM_CHANNELS on its output PID).
 */

#include <gpac/filters.h>
#include <gpac/constants.h>
#include <string.h>

#include <opus.h>

/* Opus max frame size: 120ms @ 48kHz */
#define OPUS_MAX_FRAME_SAMPLES 5760

typedef struct
{
	GF_FilterPid *ipid, *opid;

	Bool configured;
	u32 sample_rate, num_channels;

	OpusDecoder *dec;
} GF_OpusDecCtx;

static GF_Err opusdec_init_decoder(GF_OpusDecCtx *ctx)
{
	int err;
	if (ctx->dec)
	{
		opus_decoder_destroy(ctx->dec);
		ctx->dec = NULL;
	}
	if (!ctx->sample_rate) ctx->sample_rate = 48000;
	if (!ctx->num_channels) ctx->num_channels = 2;

	ctx->dec = opus_decoder_create((opus_int32)ctx->sample_rate, (int)ctx->num_channels, &err);
	if (!ctx->dec || err != OPUS_OK)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[Opus] opus_decoder_create failed: %d\n", err));
		return GF_NOT_SUPPORTED;
	}
	return GF_OK;
}

static void opusdec_copy_props(GF_OpusDecCtx *ctx)
{
	gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_AUDIO_FORMAT, &PROP_UINT(GF_AUDIO_FMT_S16));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_SAMPLE_RATE, &PROP_UINT(ctx->sample_rate));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_NUM_CHANNELS, &PROP_UINT(ctx->num_channels));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CHANNEL_LAYOUT, &PROP_LONGUINT((ctx->num_channels == 1) ? GF_AUDIO_CH_FRONT_CENTER : GF_AUDIO_CH_FRONT_LEFT | GF_AUDIO_CH_FRONT_RIGHT));
}

static GF_Err opusdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	const GF_PropertyValue *p;
	GF_Err e;
	GF_OpusDecCtx *ctx = gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	ctx->ipid = pid;
	if (!ctx->opid)
	{
		ctx->opid = gf_filter_pid_new(filter);
	}

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_SAMPLE_RATE);
	ctx->sample_rate = p ? p->value.uint : 48000;
	p = gf_filter_pid_get_property(pid, GF_PROP_PID_NUM_CHANNELS);
	ctx->num_channels = p ? p->value.uint : 2;

	e = opusdec_init_decoder(ctx);
	if (e) return e;
	ctx->configured = GF_TRUE;

	gf_filter_pid_set_framing_mode(pid, GF_TRUE);
	opusdec_copy_props(ctx);

	return GF_OK;
}

static void opusdec_finalize(GF_Filter *filter)
{
	GF_OpusDecCtx *ctx = gf_filter_get_udta(filter);
	if (ctx->dec) opus_decoder_destroy(ctx->dec);
}

static GF_Err opusdec_process(GF_Filter *filter)
{
	GF_OpusDecCtx *ctx = gf_filter_get_udta(filter);
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	opus_int16 out_buf[OPUS_MAX_FRAME_SAMPLES * 2];
	u32 size;
	int nb_samples;

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);
	if (!data || !size)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OK;
	}

	nb_samples = opus_decode(ctx->dec, data, (opus_int32)size, out_buf, OPUS_MAX_FRAME_SAMPLES, 0);
	if (nb_samples < 0)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[Opus] opus_decode failed: %d\n", nb_samples));
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OK;
	}

	dst_pck = gf_filter_pck_new_alloc(ctx->opid, nb_samples * ctx->num_channels * sizeof(opus_int16), &output);
	if (!dst_pck)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}
	memcpy(output, out_buf, nb_samples * ctx->num_channels * sizeof(opus_int16));

	gf_filter_pck_merge_properties(pck, dst_pck);
	gf_filter_pck_set_dependency_flags(dst_pck, 0);
	gf_filter_pck_send(dst_pck);

	gf_filter_pid_drop_packet(ctx->ipid);
	return GF_OK;
}

static const GF_FilterCapability OpusDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_AUDIO),
		CAP_BOOL(GF_CAPS_INPUT_EXCLUDED, GF_PROP_PID_UNFRAMED, GF_TRUE),
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_CODECID, GF_CODECID_OPUS),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_AUDIO),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister OpusDecoderRegister = {
	.name = "opusdec",
	GF_FS_SET_DESCRIPTION("Opus decoder")
		GF_FS_SET_HELP("This filter decodes Opus audio using libopus. Expects packets already demultiplexed by e.g. the oggdmx filter.")
			.private_size = sizeof(GF_OpusDecCtx),
	.priority = 1,
	SETCAPS(OpusDecCaps),
	.configure_pid = opusdec_configure_pid,
	.finalize = opusdec_finalize,
	.process = opusdec_process,
};

const GF_FilterRegister * EMSCRIPTEN_KEEPALIVE dynCall_opusdec_register(GF_FilterSession *session)
{
	return &OpusDecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_opusdec(void) {
    gf_filter_auto_register("opusdec", dynCall_opusdec_register);
}
