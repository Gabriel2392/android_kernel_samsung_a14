// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Exynos SoC series Pablo driver
 *
 * Exynos Pablo image subsystem functions
 *
 * Copyright (c) 2020 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "is-hw-yuvpp.h"
#include "is-err.h"

extern struct is_lib_support gPtr_lib_support;

static int __nocfi is_hw_ypp_open(struct is_hw_ip *hw_ip, u32 instance,
	struct is_group *group)
{
	int ret = 0;
	struct is_hw_ypp *hw_ypp = NULL;

	FIMC_BUG(!hw_ip);

	if (test_bit(HW_OPEN, &hw_ip->state))
		return 0;

	frame_manager_probe(hw_ip->framemgr, hw_ip->id, "HWYPP");
	frame_manager_open(hw_ip->framemgr, IS_MAX_HW_FRAME);

	hw_ip->priv_info = vzalloc(sizeof(struct is_hw_ypp));
	if (!hw_ip->priv_info) {
		mserr_hw("hw_ip->priv_info(null)", instance, hw_ip);
		ret = -ENOMEM;
		goto err_alloc;
	}

	hw_ypp = (struct is_hw_ypp *)hw_ip->priv_info;
#ifdef ENABLE_FPSIMD_FOR_USER
	is_fpsimd_get_func();
	ret = get_lib_func(LIB_FUNC_YUVPP, (void **)&hw_ypp->lib_func);
	is_fpsimd_put_func();
#else
	ret = get_lib_func(LIB_FUNC_YUVPP, (void **)&hw_ypp->lib_func);
#endif

	if (hw_ypp->lib_func == NULL) {
		mserr_hw("hw_ypp->lib_func(null)", instance, hw_ip);
		is_load_clear();
		ret = -EINVAL;
		goto err_lib_func;
	}
	msinfo_hw("get_lib_func is set\n", instance, hw_ip);

	hw_ypp->lib_support = &gPtr_lib_support;
	hw_ypp->lib[instance].func = hw_ypp->lib_func;

	ret = is_lib_isp_chain_create(hw_ip, &hw_ypp->lib[instance], instance);
	if (ret) {
		mserr_hw("chain create fail", instance, hw_ip);
		ret = -EINVAL;
		goto err_chain_create;
	}

	set_bit(HW_OPEN, &hw_ip->state);
	msdbg_hw(2, "open: [G:0x%x], framemgr[%s]", instance, hw_ip,
		GROUP_ID(group->id), hw_ip->framemgr->name);

	return 0;

err_chain_create:
err_lib_func:
	vfree(hw_ip->priv_info);
	hw_ip->priv_info = NULL;
err_alloc:
	frame_manager_close(hw_ip->framemgr);
	return ret;
}

static int is_hw_ypp_init(struct is_hw_ip *hw_ip, u32 instance,
	struct is_group *group, bool flag, u32 module_id)
{
	int ret = 0;
	struct is_hw_ypp *hw_ypp = NULL;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!hw_ip->priv_info);
	FIMC_BUG(!group);

	hw_ypp = (struct is_hw_ypp *)hw_ip->priv_info;

	hw_ypp->lib[instance].object = NULL;
	hw_ypp->lib[instance].func   = hw_ypp->lib_func;

	if (hw_ypp->lib[instance].object != NULL) {
		msdbg_hw(2, "object is already created\n", instance, hw_ip);
	} else {
		ret = is_lib_isp_object_create(hw_ip, &hw_ypp->lib[instance],
				instance, (u32)flag, module_id);
		if (ret) {
			mserr_hw("object create fail", instance, hw_ip);
			return -EINVAL;
		}
	}

	set_bit(HW_INIT, &hw_ip->state);
	return ret;
}

static int is_hw_ypp_deinit(struct is_hw_ip *hw_ip, u32 instance)
{
	int ret = 0;
	struct is_hw_ypp *hw_ypp;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!hw_ip->priv_info);

	hw_ypp = (struct is_hw_ypp *)hw_ip->priv_info;

	is_lib_isp_object_destroy(hw_ip, &hw_ypp->lib[instance], instance);
	hw_ypp->lib[instance].object = NULL;

	return ret;
}

static int is_hw_ypp_close(struct is_hw_ip *hw_ip, u32 instance)
{
	int ret = 0;
	struct is_hw_ypp *hw_ypp;

	FIMC_BUG(!hw_ip);

	if (!test_bit(HW_OPEN, &hw_ip->state))
		return 0;

	FIMC_BUG(!hw_ip->priv_info);
	hw_ypp = (struct is_hw_ypp *)hw_ip->priv_info;
	FIMC_BUG(!hw_ypp->lib_support);

	is_lib_isp_chain_destroy(hw_ip, &hw_ypp->lib[instance], instance);
	vfree(hw_ip->priv_info);
	hw_ip->priv_info = NULL;
	frame_manager_close(hw_ip->framemgr);

	clear_bit(HW_OPEN, &hw_ip->state);

	return ret;
}

static int is_hw_ypp_enable(struct is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	int ret = 0;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -EINVAL;
	}

	atomic_inc(&hw_ip->run_rsccount);
	set_bit(HW_RUN, &hw_ip->state);

	return ret;
}

static int is_hw_ypp_disable(struct is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	int ret = 0;
	long timetowait;
	struct is_hw_ypp *hw_ypp;
	struct ypp_param_set *param_set;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	msinfo_hw("ypp_disable: Vvalid(%d)\n", instance, hw_ip,
		atomic_read(&hw_ip->status.Vvalid));

	FIMC_BUG(!hw_ip->priv_info);
	hw_ypp = (struct is_hw_ypp *)hw_ip->priv_info;
	param_set = &hw_ypp->param_set[instance];

	timetowait = wait_event_timeout(hw_ip->status.wait_queue,
		!atomic_read(&hw_ip->status.Vvalid),
		IS_HW_STOP_TIMEOUT);

	if (!timetowait) {
		mserr_hw("wait FRAME_END timeout (%ld)", instance,
			hw_ip, timetowait);
		ret = -ETIME;
	}

	param_set->fcount = 0;
	if (test_bit(HW_RUN, &hw_ip->state)) {
		/* TODO: need to kthread_flush when ypp use task */
		is_lib_isp_stop(hw_ip, &hw_ypp->lib[instance], instance);
	} else {
		msdbg_hw(2, "already disabled\n", instance, hw_ip);
	}

	if (atomic_dec_return(&hw_ip->run_rsccount) > 0)
		return 0;

	clear_bit(HW_RUN, &hw_ip->state);
	clear_bit(HW_CONFIG, &hw_ip->state);

	return ret;
}

static void is_hw_ypp_update_param(struct is_hw_ip *hw_ip, struct is_region *region,
	struct ypp_param_set *param_set, u32 lindex, u32 hindex, u32 instance)
{
	struct ypp_param *param;

	FIMC_BUG_VOID(!region);
	FIMC_BUG_VOID(!param_set);

	param = &region->parameter.ypp;
	param_set->instance_id = instance;

	/* check input */
	if ((lindex & LOWBIT_OF(PARAM_YPP_DMA_INPUT))
		|| (hindex & HIGHBIT_OF(PARAM_YPP_DMA_INPUT))) {
		memcpy(&param_set->dma_input, &param->dma_input,
			sizeof(struct param_dma_input));
	}

	if ((lindex & LOWBIT_OF(PARAM_YPP_DMA_LV2_INPUT))
		|| (hindex & HIGHBIT_OF(PARAM_YPP_DMA_LV2_INPUT))) {
		memcpy(&param_set->dma_input_lv2, &param->dma_input_lv2,
			sizeof(struct param_dma_input));
	}

	if ((lindex & LOWBIT_OF(PARAM_YPP_DMA_NOISE_INPUT))
		|| (hindex & HIGHBIT_OF(PARAM_YPP_DMA_NOISE_INPUT))) {
		memcpy(&param_set->dma_input_noise, &param->dma_input_noise,
			sizeof(struct param_dma_input));
	}

	if ((lindex & LOWBIT_OF(PARAM_YPP_DMA_DRC_INPUT))
		|| (hindex & HIGHBIT_OF(PARAM_YPP_DMA_DRC_INPUT))) {
		memcpy(&param_set->dma_input_drc, &param->dma_input_drc,
			sizeof(struct param_dma_input));
	}

	if ((lindex & LOWBIT_OF(PARAM_YPP_DMA_HIST_INPUT))
		|| (hindex & HIGHBIT_OF(PARAM_YPP_DMA_HIST_INPUT))) {
		memcpy(&param_set->dma_input_hist, &param->dma_input_hist,
			sizeof(struct param_dma_input));
	}

	/* check output*/
	if ((lindex & LOWBIT_OF(PARAM_YPP_OTF_OUTPUT))
		|| (hindex & HIGHBIT_OF(PARAM_YPP_OTF_OUTPUT))) {
		memcpy(&param_set->otf_output, &param->otf_output,
			sizeof(struct param_otf_output));
	}
	if ((lindex & LOWBIT_OF(PARAM_YPP_STRIPE_INPUT))
		|| (hindex & HIGHBIT_OF(PARAM_YPP_STRIPE_INPUT))) {
		memcpy(&param_set->stripe_input, &param->stripe_input,
			sizeof(struct param_stripe_input));
	}

}

static int is_hw_ypp_shot(struct is_hw_ip *hw_ip, struct is_frame *frame,
	ulong hw_map)
{
	int ret = 0;
	int i, cur_idx;
	struct is_hw_ypp *hw_ypp;
	struct ypp_param_set *param_set;
	struct is_region *region;
	struct ypp_param *param;
	u32 lindex, hindex, fcount, instance;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!frame);

	instance = frame->instance;
	msdbgs_hw(2, "[F:%d]shot\n", instance, hw_ip, frame->fcount);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -EINVAL;
	}

	set_bit(hw_ip->id, &frame->core_flag);

	FIMC_BUG(!hw_ip->priv_info);
	hw_ypp = (struct is_hw_ypp *)hw_ip->priv_info;
	param_set = &hw_ypp->param_set[instance];
	region = hw_ip->region[instance];
	FIMC_BUG(!region);

	param = &region->parameter.ypp;
	fcount = frame->fcount + frame->cur_buf_index;

	if (frame->type == SHOT_TYPE_INTERNAL) {
		param_set->dma_input.cmd = DMA_INPUT_COMMAND_DISABLE;
		param_set->input_dva[0] = 0x0;
		hw_ip->internal_fcount[instance] = fcount;
		goto config;
	} else {
		FIMC_BUG(!frame->shot);
		/* per-frame control
		 * check & update size from region
		 */
		lindex = frame->shot->ctl.vendor_entry.lowIndexParam;
		hindex = frame->shot->ctl.vendor_entry.highIndexParam;

		if (hw_ip->internal_fcount[instance] != 0)
			hw_ip->internal_fcount[instance] = 0;
	}

	is_hw_ypp_update_param(hw_ip, region, param_set, lindex, hindex, instance);

	/* DMA settings */
	cur_idx = frame->cur_buf_index;
	if (param_set->dma_input.cmd != DMA_INPUT_COMMAND_DISABLE) {
		for (i = 0; i < param_set->dma_input.plane; i++) {
			param_set->input_dva[i] = (typeof(*param_set->input_dva))
				frame->dvaddr_buffer[i + cur_idx];
			if (param_set->input_dva[i] == 0) {
				msinfo_hw("[F:%d]dvaddr_buffer[%d] is zero",
					instance, hw_ip, frame->fcount, i);
				FIMC_BUG(1);
			}
		}
	}

	/* TODO: Set another RDMA (lv0/lv2, noise, drc, hist) */

config:
	param_set->instance_id = instance;
	param_set->fcount = fcount;

	/* multi-buffer */
	if (frame->num_buffers)
		hw_ip->num_buffers = frame->num_buffers;

	if (frame->shot) {
		ret = is_lib_isp_set_ctrl(hw_ip, &hw_ypp->lib[instance], frame);
		if (ret)
			mserr_hw("set_ctrl fail", instance, hw_ip);
	}

	is_lib_isp_shot(hw_ip, &hw_ypp->lib[instance], param_set, frame->shot);

	set_bit(HW_CONFIG, &hw_ip->state);

	return ret;
}

static int is_hw_ypp_set_param(struct is_hw_ip *hw_ip, struct is_region *region,
	u32 lindex, u32 hindex, u32 instance, ulong hw_map)
{
	int ret = 0;
	struct is_hw_ypp *hw_ypp;
	struct ypp_param_set *param_set;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -EINVAL;
	}

	FIMC_BUG(!hw_ip->priv_info);
	hw_ypp = (struct is_hw_ypp *)hw_ip->priv_info;
	param_set = &hw_ypp->param_set[instance];

	hw_ip->region[instance] = region;
	hw_ip->lindex[instance] = lindex;
	hw_ip->hindex[instance] = hindex;

	is_hw_ypp_update_param(hw_ip, region, param_set, lindex, hindex, instance);

	return ret;
}

static int is_hw_ypp_get_meta(struct is_hw_ip *hw_ip, struct is_frame *frame,
	ulong hw_map)
{
	int ret = 0;
	struct is_hw_ypp *hw_ypp;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!frame);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	FIMC_BUG(!hw_ip->priv_info);
	hw_ypp = (struct is_hw_ypp *)hw_ip->priv_info;

	ret = is_lib_isp_get_meta(hw_ip, &hw_ypp->lib[frame->instance], frame);
	if (ret)
		mserr_hw("get_meta fail", frame->instance, hw_ip);

	return ret;
}

static int is_hw_ypp_frame_ndone(struct is_hw_ip *hw_ip, struct is_frame *frame,
	u32 instance, enum ShotErrorType done_type)
{
	int ret = 0;
	int output_id;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!frame);

	output_id = IS_HW_CORE_END;
	if (test_bit(hw_ip->id, &frame->core_flag)) {
		ret = is_hardware_frame_done(hw_ip, frame, -1,
				output_id, done_type, false);
	}

	return ret;
}

static int is_hw_ypp_load_setfile(struct is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	int flag = 0, ret = 0;
	ulong addr;
	u32 size, index;
	struct is_hw_ypp *hw_ypp = NULL;
	struct is_hw_ip_setfile *setfile;
	enum exynos_sensor_position sensor_position;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map)) {
		msdbg_hw(2, "%s: hw_map(0x%lx)\n", instance, hw_ip, __func__, hw_map);
		return 0;
	}

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -ESRCH;
	}

	sensor_position = hw_ip->hardware->sensor_position[instance];
	setfile = &hw_ip->setfile[sensor_position];

	switch (setfile->version) {
	case SETFILE_V2:
		flag = false;
		break;
	case SETFILE_V3:
		flag = true;
		break;
	default:
		mserr_hw("invalid version (%d)", instance, hw_ip,
			setfile->version);
		return -EINVAL;
	}

	FIMC_BUG(!hw_ip->priv_info);
	hw_ypp = (struct is_hw_ypp *)hw_ip->priv_info;

	for (index = 0; index < setfile->using_count; index++) {
		addr = setfile->table[index].addr;
		size = setfile->table[index].size;
		ret = is_lib_isp_create_tune_set(&hw_ypp->lib[instance],
			addr, size, index, flag, instance);

		set_bit(index, &hw_ypp->lib[instance].tune_count);
	}

	set_bit(HW_TUNESET, &hw_ip->state);

	return ret;
}

static int is_hw_ypp_apply_setfile(struct is_hw_ip *hw_ip, u32 scenario,
	u32 instance, ulong hw_map)
{
	int ret = 0;
	u32 setfile_index = 0;
	struct is_hw_ypp *hw_ypp = NULL;
	struct is_hw_ip_setfile *setfile;
	enum exynos_sensor_position sensor_position;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map)) {
		msdbg_hw(2, "%s: hw_map(0x%lx)\n", instance, hw_ip, __func__, hw_map);
		return 0;
	}

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -ESRCH;
	}

	sensor_position = hw_ip->hardware->sensor_position[instance];
	setfile = &hw_ip->setfile[sensor_position];

	if (setfile->using_count == 0)
		return 0;

	setfile_index = setfile->index[scenario];
	if (setfile_index >= setfile->using_count) {
		mserr_hw("setfile index is out-of-range, [%d:%d]",
				instance, hw_ip, scenario, setfile_index);
		return -EINVAL;
	}

	msinfo_hw("setfile (%d) scenario (%d)\n", instance, hw_ip,
		setfile_index, scenario);

	FIMC_BUG(!hw_ip->priv_info);
	hw_ypp = (struct is_hw_ypp *)hw_ip->priv_info;

	ret = is_lib_isp_apply_tune_set(&hw_ypp->lib[instance], setfile_index, instance);

	return ret;
}

static int is_hw_ypp_delete_setfile(struct is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	struct is_hw_ypp *hw_ypp = NULL;
	int i, ret = 0;
	struct is_hw_ip_setfile *setfile;
	enum exynos_sensor_position sensor_position;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map)) {
		msdbg_hw(2, "%s: hw_map(0x%lx)\n", instance, hw_ip, __func__, hw_map);
		return 0;
	}

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		msdbg_hw(2, "Not initialized\n", instance, hw_ip);
		return 0;
	}

	sensor_position = hw_ip->hardware->sensor_position[instance];
	setfile = &hw_ip->setfile[sensor_position];

	if (setfile->using_count == 0)
		return 0;

	FIMC_BUG(!hw_ip->priv_info);
	hw_ypp = (struct is_hw_ypp *)hw_ip->priv_info;

	for (i = 0; i < setfile->using_count; i++) {
		if (test_bit(i, &hw_ypp->lib[instance].tune_count)) {
			ret = is_lib_isp_delete_tune_set(&hw_ypp->lib[instance],
				(u32)i, instance);
			clear_bit(i, &hw_ypp->lib[instance].tune_count);
		}
	}

	clear_bit(HW_TUNESET, &hw_ip->state);

	return ret;
}

int is_hw_ypp_restore(struct is_hw_ip *hw_ip, u32 instance)
{
	int ret = 0;
	struct is_hw_ypp *hw_ypp = NULL;

	BUG_ON(!hw_ip);
	BUG_ON(!hw_ip->priv_info);

	if (!test_bit(HW_OPEN, &hw_ip->state))
		return -EINVAL;

	hw_ypp = (struct is_hw_ypp *)hw_ip->priv_info;

	ret = is_lib_isp_reset_recovery(hw_ip, &hw_ypp->lib[instance], instance);
	if (ret) {
		mserr_hw("is_lib_ypp_reset_recovery fail ret(%d)",
				instance, hw_ip, ret);
	}

	return ret;
}

const struct is_hw_ip_ops is_hw_ypp_ops = {
	.open			= is_hw_ypp_open,
	.init			= is_hw_ypp_init,
	.deinit			= is_hw_ypp_deinit,
	.close			= is_hw_ypp_close,
	.enable			= is_hw_ypp_enable,
	.disable		= is_hw_ypp_disable,
	.shot			= is_hw_ypp_shot,
	.set_param		= is_hw_ypp_set_param,
	.get_meta		= is_hw_ypp_get_meta,
	.frame_ndone		= is_hw_ypp_frame_ndone,
	.load_setfile		= is_hw_ypp_load_setfile,
	.apply_setfile		= is_hw_ypp_apply_setfile,
	.delete_setfile		= is_hw_ypp_delete_setfile,
	.restore		= is_hw_ypp_restore
};

int is_hw_ypp_probe(struct is_hw_ip *hw_ip, struct is_interface *itf,
	struct is_interface_ischain *itfc, int id, const char *name)
{
	int ret = 0;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!itf);
	FIMC_BUG(!itfc);

	/* initialize device hardware */
	hw_ip->id   = id;
	snprintf(hw_ip->name, sizeof(hw_ip->name), "%s", name);
	hw_ip->ops  = &is_hw_ypp_ops;
	hw_ip->itf  = itf;
	hw_ip->itfc = itfc;
	atomic_set(&hw_ip->fcount, 0);
	hw_ip->is_leader = true;
	atomic_set(&hw_ip->status.Vvalid, V_BLANK);
	atomic_set(&hw_ip->rsccount, 0);
	atomic_set(&hw_ip->run_rsccount, 0);
	init_waitqueue_head(&hw_ip->status.wait_queue);

	clear_bit(HW_OPEN, &hw_ip->state);
	clear_bit(HW_INIT, &hw_ip->state);
	clear_bit(HW_CONFIG, &hw_ip->state);
	clear_bit(HW_RUN, &hw_ip->state);
	clear_bit(HW_TUNESET, &hw_ip->state);

	sinfo_hw("probe done\n", hw_ip);

	return ret;
}
