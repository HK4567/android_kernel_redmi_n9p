/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"mi-dsi-panel:[%s:%d] " fmt, __func__, __LINE__
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <video/mipi_display.h>

#include "dsi_panel.h"
#include "dsi_display.h"
#include "dsi_ctrl_hw.h"
#include "dsi_parser.h"
#include "dsi_mi_feature.h"
#include "xiaomi_frame_stat.h"

#define to_dsi_display(x) container_of(x, struct dsi_display, host)
#define XY_COORDINATE_NUM    2
#define MAX_LUMINANCE_NUM    2

extern struct frame_stat fm_stat;
struct dsi_panel *g_panel;
static void panelon_dimming_enable_delayed_work(struct work_struct *work);

int dsi_panel_parse_mi_config(struct dsi_panel *panel,
				struct device_node *of_node)
{
	int rc = 0;
	u32 xy_coordinate[XY_COORDINATE_NUM] = {0};
	u32 max_luminance[MAX_LUMINANCE_NUM] = {0};

	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;

	mi_cfg->dsi_panel = panel;
	g_panel = panel;

	mi_cfg->bl_is_big_endian = utils->read_bool(utils->data,
			"mi,mdss-dsi-bl-dcs-big-endian-type");

	mi_cfg->mi_feature_enabled = utils->read_bool(of_node,
			"mi,feature-enabled");
	if (mi_cfg->mi_feature_enabled) {
		pr_info("mi feature enabled\n");
	} else {
		pr_info("mi feature disabled\n");
		return 0;
	}

	rc = utils->read_u32(of_node,
		"mi,mdss-panel-on-dimming-delay", &mi_cfg->panel_on_dimming_delay);
	if (rc) {
		mi_cfg->panel_on_dimming_delay = 0;
		pr_info("panel on dimming delay disabled\n");
	} else {
		pr_info("panel on dimming delay %d ms\n", mi_cfg->panel_on_dimming_delay);
	}

	rc = utils->read_u32(of_node,
			"mi,disp-doze-backlight-threshold", &mi_cfg->doze_backlight_threshold);
	if (rc) {
		mi_cfg->doze_backlight_threshold = DOZE_MIN_BRIGHTNESS_LEVEL;
		pr_info("default doze backlight threshold is %d\n", DOZE_MIN_BRIGHTNESS_LEVEL);
	} else {
		pr_info("doze backlight threshold %d \n", mi_cfg->doze_backlight_threshold);
	}

	rc = utils->read_u32(of_node,
			"mi,disp-fod-off-dimming-delay", &mi_cfg->fod_off_dimming_delay);
	if (rc) {
		mi_cfg->fod_off_dimming_delay = DEFAULT_FOD_OFF_DIMMING_DELAY;
		pr_info("default fod_off_dimming_delay %d\n", DEFAULT_FOD_OFF_DIMMING_DELAY);
	} else {
		pr_info("fod_off_dimming_delay %d\n", mi_cfg->fod_off_dimming_delay);
	}

	mi_cfg->is_tddi_flag = utils->read_bool(of_node,
			"mi,is-tddi-flag");
	if (mi_cfg->is_tddi_flag)
		pr_info("panel is tddi\n");

	rc = utils->read_u32_array(of_node,
		"mi,mdss-dsi-panel-xy-coordinate",
		xy_coordinate, XY_COORDINATE_NUM);

	if (rc) {
		pr_info("%s:%d, Unable to read panel xy coordinate\n",
		       __func__, __LINE__);
		mi_cfg->xy_coordinate_cmds.is_read = false;
	} else {
		mi_cfg->xy_coordinate_cmds.cmds_rlen = xy_coordinate[0];
		mi_cfg->xy_coordinate_cmds.valid_bits = xy_coordinate[1];
		mi_cfg->xy_coordinate_cmds.is_read = true;
	}
	pr_info("0x%x 0x%x enabled:%d\n", xy_coordinate[0], xy_coordinate[1],
			mi_cfg->xy_coordinate_cmds.is_read);

	rc = utils->read_u32_array(of_node, "mi,mdss-dsi-panel-max-luminance",
			max_luminance, MAX_LUMINANCE_NUM);
	if (rc) {
		pr_info("%s:%d, Unable to read panel max luminance\n",
			   __func__, __LINE__);
		mi_cfg->max_luminance_cmds.is_read = false;
	} else {
		mi_cfg->max_luminance_cmds.cmds_rlen = max_luminance[0];
		mi_cfg->max_luminance_cmds.valid_bits = max_luminance[1];
		mi_cfg->max_luminance_cmds.is_read = true;
	}
	pr_info("0x%x 0x%x enabled:%d\n", max_luminance[0], max_luminance[1],
			mi_cfg->max_luminance_cmds.is_read);

	mi_cfg->esd_skip_reg_read = utils->read_bool(of_node,
			"mi,esd-skip-reg-read-before-wp");
	if (!mi_cfg->esd_skip_reg_read)
		pr_info("%s:%d, Unconfigured mi,esd-skip-reg-read-before-wp\n",
			__func__, __LINE__);

	rc = utils->read_u32(of_node, "mi,mdss-panel-idle-time", &mi_cfg->idle_time);
	if (rc) {
		mi_cfg->idle_time = 0;
	}
	rc = utils->read_u32(of_node, "mi,mdss-panel-idle-fps", &mi_cfg->idle_fps);
	if (rc) {
		mi_cfg->idle_fps = 0;
	}

	rc = utils->read_u32(of_node, "mi,mdss-dsi-post-off-msleep", &mi_cfg->post_off_msleep);
	if (rc) {
		mi_cfg->post_off_msleep = 0;
	}

	mi_cfg->fod_hbm_enabled = false;
	mi_cfg->skip_dimmingon = STATE_NONE;
	mi_cfg->fod_backlight_flag = false;
	mi_cfg->fod_flag = false;
	mi_cfg->in_aod = false;
	mi_cfg->fod_hbm_off_time = ktime_get();
	mi_cfg->fod_backlight_off_time = ktime_get();
	mi_cfg->panel_dead_flag = false;
	mi_cfg->tddi_doubleclick_flag = false;

	INIT_DELAYED_WORK(&mi_cfg->cmds_work, panelon_dimming_enable_delayed_work);
	return rc;
}

int dsi_panel_write_cmd_set(struct dsi_panel *panel,
				struct dsi_panel_cmd_set *cmd_sets)
{
	int rc = 0, i = 0;
	ssize_t len;
	struct dsi_cmd_desc *cmds;
	u32 count;
	enum dsi_cmd_set_state state;
	struct dsi_display_mode *mode;
	const struct mipi_dsi_host_ops *ops = panel->host->ops;

	if (!panel || !panel->cur_mode)
		return -EINVAL;

	mode = panel->cur_mode;

	cmds = cmd_sets->cmds;
	count = cmd_sets->count;
	state = cmd_sets->state;

	if (count == 0) {
		pr_debug("[%s] No commands to be sent for state\n", panel->name);
		goto error;
	}

	for (i = 0; i < count; i++) {
		if (state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		if (cmds->last_command)
			cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;

		len = ops->transfer(panel->host, &cmds->msg);
		if (len < 0) {
			rc = len;
			pr_err("failed to set cmds, rc=%d\n", rc);
			goto error;
		}
		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms*1000,
					((cmds->post_wait_ms*1000)+10));
		cmds++;
	}
error:
	return rc;
}

int dsi_panel_read_cmd_set(struct dsi_panel *panel,
				struct dsi_read_config *read_config)
{
	struct mipi_dsi_host *host;
	struct dsi_display *display;
	struct dsi_display_ctrl *ctrl;
	struct dsi_cmd_desc *cmds;
	enum dsi_cmd_set_state state;
	int i, rc = 0, count = 0;
	u32 flags = 0;

	if (panel == NULL || read_config == NULL)
		return -EINVAL;

	host = panel->host;
	if (host) {
		display = to_dsi_display(host);
		if (display == NULL)
			return -EINVAL;
	} else
		return -EINVAL;

	if (!panel->panel_initialized) {
		pr_info("Panel not initialized\n");
		return -EINVAL;
	}

	if (!read_config->is_read) {
		pr_info("read operation was not permitted\n");
		return -EPERM;
	}

	dsi_display_clk_ctrl(display->dsi_clk_handle,
		DSI_ALL_CLKS, DSI_CLK_ON);

	ctrl = &display->ctrl[display->cmd_master_idx];

	rc = dsi_display_cmd_engine_enable(display);
	if (rc) {
		pr_err("cmd engine enable failed\n");
		rc = -EPERM;
		goto exit_ctrl;
	}

	if (display->tx_cmd_buf == NULL) {
		rc = dsi_host_alloc_cmd_tx_buffer(display);
		if (rc) {
			pr_err("failed to allocate cmd tx buffer memory\n");
			goto exit;
		}
	}

	count = read_config->read_cmd.count;
	cmds = read_config->read_cmd.cmds;
	state = read_config->read_cmd.state;
	if (count == 0) {
		pr_err("No commands to be sent\n");
		goto exit;
	}
	if (cmds->last_command) {
		cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
		flags |= DSI_CTRL_CMD_LAST_COMMAND;
	}
	if (state == DSI_CMD_SET_STATE_LP)
		cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;
	flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ |
		  DSI_CTRL_CMD_CUSTOM_DMA_SCHED);

	memset(read_config->rbuf, 0x0, sizeof(read_config->rbuf));
	cmds->msg.rx_buf = read_config->rbuf;
	cmds->msg.rx_len = read_config->cmds_rlen;

	rc = dsi_ctrl_cmd_transfer(ctrl->ctrl, &(cmds->msg), &flags);
	if (rc <= 0) {
		pr_err("rx cmd transfer failed rc=%d\n", rc);
		goto exit;
	}

	for (i = 0; i < read_config->cmds_rlen; i++) //debug
		pr_info("0x%x ", read_config->rbuf[i]);
	pr_info("\n");

exit:
	dsi_display_cmd_engine_disable(display);
exit_ctrl:
	dsi_display_clk_ctrl(display->dsi_clk_handle,
		DSI_ALL_CLKS, DSI_CLK_OFF);

	return rc;
}

static int dsi_panel_get_lockdown_from_cmdline(unsigned char *plockdowninfo)
{
	int ret = -1;
	char lockdown_str[40] = {'\0'};
	char *match = (char *) strnstr(saved_command_line,
				"msm_drm.panel_lockdown=",
				strlen(saved_command_line));

	if (match && plockdowninfo) {
		memcpy(lockdown_str, (match + strlen("msm_drm.panel_lockdown=")),
			sizeof(lockdown_str) - 1);
		if (sscanf(lockdown_str, "0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x",
				&plockdowninfo[0], &plockdowninfo[1], &plockdowninfo[2], &plockdowninfo[3],
				&plockdowninfo[4], &plockdowninfo[5], &plockdowninfo[6], &plockdowninfo[7])
					!= 8) {
			pr_err("failed to parse lockdown info from cmdline !\n");
		} else {
			pr_info("lockdown info from cmdline = 0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,"
					"0x%02x,0x%02x,0x%02x",
					plockdowninfo[0], plockdowninfo[1], plockdowninfo[2], plockdowninfo[3],
					plockdowninfo[4], plockdowninfo[5], plockdowninfo[6], plockdowninfo[7]);
			ret = 0;
		}
	}
	return ret;
}

/* note: return value <= 0 is error; > 0 is success */
ssize_t dsi_panel_lockdown_info_read(unsigned char *plockdowninfo)
{
	int rc = 0;
	int i = 0;
	struct dsi_read_config ld_read_config;
	struct dsi_panel_cmd_set cmd_sets = {0};

	if (!dsi_panel_get_lockdown_from_cmdline(plockdowninfo))
		return 1;
	else if (!g_panel || !plockdowninfo) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	while (!g_panel->cur_mode || !g_panel->cur_mode->priv_info) {
		pr_debug("[%s][%s] waitting for panel priv_info initialized!\n",
				__func__, g_panel->name);
		msleep_interruptible(1000);
	}

	mutex_lock(&g_panel->panel_lock);
	if (g_panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_MI_READ_LOCKDOWN_INFO].cmds) {
		cmd_sets.cmds = g_panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_MI_READ_LOCKDOWN_INFO].cmds;
		cmd_sets.count = 1;
		cmd_sets.state = g_panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_MI_READ_LOCKDOWN_INFO].state;
		rc = dsi_panel_write_cmd_set(g_panel, &cmd_sets);
		if (rc) {
			pr_err("[%s][%s] failed to send cmds, rc=%d\n", __func__, g_panel->name, rc);
			rc = -EIO;
			goto done;
		}

		ld_read_config.is_read = 1;
		ld_read_config.cmds_rlen = 8;
		ld_read_config.read_cmd = cmd_sets;
		ld_read_config.read_cmd.cmds = &cmd_sets.cmds[1];
		rc = dsi_panel_read_cmd_set(g_panel, &ld_read_config);
		if (rc <= 0) {
			pr_err("[%s][%s] failed to read cmds, rc=%d\n", __func__, g_panel->name, rc);
			rc = -EIO;
			goto done;
		}

		for (i = 0; i < 8; i++) {
			pr_info("[%s][%d]0x%02x", __func__, __LINE__, ld_read_config.rbuf[i]);
			plockdowninfo[i] = ld_read_config.rbuf[i];
		}
	}

done:
	mutex_unlock(&g_panel->panel_lock);
	return rc;
}
EXPORT_SYMBOL(dsi_panel_lockdown_info_read);

static void handle_dsi_read_data(struct dsi_panel_mi_cfg *mi_panel,
		struct dsi_read_config *read_config)
{
	int i = 0;
	int param_nb = 0, write_len = 0;
	u32 read_cnt = 0, bit_valide = 0;
	u8 *pRead_data = NULL;

	if (!mi_panel || !read_config) {
		pr_err("invalid params\n");
		return;
	}

	pRead_data = mi_panel->panel_read_data;
	read_cnt = read_config->cmds_rlen;
	bit_valide = read_config->valid_bits;

	for (i = 0; i < read_cnt; i++) {
		if ((bit_valide & 0x1) && ((pRead_data + 8) <
					(mi_panel->panel_read_data + BUF_LEN_MAX))) {
			write_len = scnprintf(pRead_data, 8, "p%d=%d", param_nb,
					read_config->rbuf[i]);
			pRead_data += write_len;
			param_nb ++;
		}
		bit_valide = bit_valide >> 1;
	}
	pr_info("read %s from panel\n", mi_panel->panel_read_data);

	return;
}

static ssize_t dsi_panel_xy_coordinate_read(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_mi_cfg *mi_cfg  = NULL;
	struct dsi_panel_cmd_set cmd_sets = {0};

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	if (!mi_cfg) {
		pr_err("mi panel is validate\n");
		return -EINVAL;
	}

	while (!panel->cur_mode || !panel->cur_mode->priv_info ||
			!panel->panel_initialized) {
		pr_debug("[%s][%s] waitting for panel priv_info initialized!\n",
				__func__, panel->name);
		msleep_interruptible(1000);
	}

	if (panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_READ_XY_COORDINATE].cmds) {
		cmd_sets.cmds = panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_READ_XY_COORDINATE].cmds;
		cmd_sets.count = 1;
		cmd_sets.state = panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_READ_XY_COORDINATE].state;
		rc = dsi_panel_write_cmd_set(panel, &cmd_sets);
		if (rc) {
			pr_err("[%s][%s] failed to send cmds, rc=%d\n", __func__, panel->name, rc);
			rc = -EIO;
			return rc;
		}

		mi_cfg->xy_coordinate_cmds.read_cmd = cmd_sets;
		mi_cfg->xy_coordinate_cmds.read_cmd.cmds = &cmd_sets.cmds[1];
		rc = dsi_panel_read_cmd_set(panel, &mi_cfg->xy_coordinate_cmds);
		if (rc > 0)
			handle_dsi_read_data(mi_cfg, &mi_cfg->xy_coordinate_cmds);
		else
			pr_err("[%s][%s] failed to read cmds, rc=%d\n", __func__, panel->name, rc);
	}

	return rc;
}

static ssize_t dsi_panel_max_luminance_read(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_mi_cfg *mi_cfg  = NULL;
	struct dsi_panel_cmd_set cmd_sets = {0};
	struct dsi_read_config r_cfg = {0};

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	if (!mi_cfg) {
		pr_err("mi panel is validate\n");
		return -EINVAL;
	}

	while(!panel->cur_mode || !panel->cur_mode->priv_info || !panel->panel_initialized) {
		pr_debug("[%s][%s] waitting for panel priv_info initialized!\n", __func__, panel->name);
		msleep_interruptible(1000);
	}

	if (panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_MI_MAX_LUMINANCE_WRITE].cmds) {
		cmd_sets.cmds = panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_MI_MAX_LUMINANCE_WRITE].cmds;
		cmd_sets.count = 1;
		cmd_sets.state = panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_MI_MAX_LUMINANCE_WRITE].state;
		rc = dsi_panel_write_cmd_set(panel, &cmd_sets);
		if (rc) {
			pr_err("[%s][%s] failed to send cmds, rc=%d\n", __func__, panel->name, rc);
			rc = -EIO;
			return rc;
		}
	}

	if (panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_MI_MAX_LUMINANCE_READ].cmds) {
		r_cfg = mi_cfg->max_luminance_cmds;
		r_cfg.read_cmd.cmds = panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_MI_MAX_LUMINANCE_READ].cmds;
		r_cfg.read_cmd.count = 1;
		r_cfg.read_cmd.state = panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_MI_MAX_LUMINANCE_READ].state;

		rc = dsi_panel_read_cmd_set(panel, &r_cfg);
		if (rc > 0)
			handle_dsi_read_data(mi_cfg, &r_cfg);
		else
			pr_err("[%s][%s] failed to read cmds, rc=%d\n", __func__, panel->name, rc);
	}

	mi_cfg->esd_skip_reg_read = false;

	return rc;
}

void dsi_panel_doubleclick_enable(bool on)
{
	g_panel->mi_cfg.tddi_doubleclick_flag = on;
}
EXPORT_SYMBOL(dsi_panel_doubleclick_enable);


int dsi_panel_disp_param_set(struct dsi_panel *panel, u32 param)
{
	int rc = 0;
	uint32_t temp = 0;
	u32 fod_backlight = 0;
	struct dsi_panel_mi_cfg *mi_cfg  = NULL;
	static u8 backlight_delta = 0;
	u32 resend_backlight;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;
	if (!mi_cfg->mi_feature_enabled) {
		pr_err("mi feature not enable, exit!\n");
		goto exit;
	}

	pr_debug("param_type = 0x%x\n", param);

	if (!panel->panel_initialized
		&& (param & 0x0F000000) != DISPPARAM_FOD_BACKLIGHT_ON
		&& (param & 0x0F000000) != DISPPARAM_FOD_BACKLIGHT_OFF) {
		pr_err("panel not ready!\n");
		goto exit;
	}

	if ((param & 0x00F00000) == 0xD00000) {
		fod_backlight = (param & 0x01FFF);
		param = (param & 0x0FF00000);
	}

	/* set smart fps status */
	if (param & 0xF0000000) {
		fm_stat.enabled = param & 0x01;
		DSI_DEBUG("smart dfps enable = [%d]\n", fm_stat.enabled);
	}

	temp = param & 0x0000000F;
	switch (temp) {
	case DISPPARAM_WARM:
	case DISPPARAM_DEFAULT:
	case DISPPARAM_COLD:
	case DISPPARAM_PAPERMODE8:
	case DISPPARAM_PAPERMODE1:
	case DISPPARAM_PAPERMODE2:
	case DISPPARAM_PAPERMODE3:
	case DISPPARAM_PAPERMODE4:
	case DISPPARAM_PAPERMODE5:
	case DISPPARAM_PAPERMODE6:
	case DISPPARAM_PAPERMODE7:
		pr_debug("DISPPARAM_WARM[0x1]~DISPPARAM_PAPERMODE7[0xc] not supported!\n");
		break;
	case DISPPARAM_WHITEPOINT_XY:
		pr_info("read xy coordinate\n");
		dsi_panel_xy_coordinate_read(panel);
		break;
	case DISPPARAM_MAX_LUMINANCE_READ:
		pr_info("read max luminance nit value");
		dsi_panel_max_luminance_read(panel);
		break;
	default:
		break;
	}

	temp = param & 0x000000F0;
	switch (temp) {
	case DISPPARAM_CE_ON:
		pr_info("ceon\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CEON);
		break;
	case DISPPARAM_CE_OFF:
		pr_info("ceoff\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CEOFF);
		break;
	default:
		break;
	}

	temp = param & 0x00000F00;
	switch (temp) {
	case DISPPARAM_CABCUI_ON:
		pr_info("cabcuion\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABCUION);
		break;
	case DISPPARAM_CABCSTILL_ON:
		pr_info("cabcstillon\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABCSTILLON);
		break;
	case DISPPARAM_CABCMOVIE_ON:
		pr_info("cabcmovieon\n");
		dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABCMOVIEON);
		break;
	case DISPPARAM_CABC_OFF:
		pr_info("cabcoff\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABCOFF);
		break;
	case DISPPARAM_SKIN_CE_CABCUI_ON:
		pr_info("skince cabcuion\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SKINCE_CABCUION);
		break;
	case DISPPARAM_SKIN_CE_CABCSTILL_ON:
		pr_info("skince cabcstillon\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SKINCE_CABCSTILLON);
		break;
	case DISPPARAM_SKIN_CE_CABCMOVIE_ON:
		pr_info("skince cabcmovieon\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SKINCE_CABCMOVIEON);
		break;
	case DISPPARAM_SKIN_CE_CABC_OFF:
		pr_info("skince cabcoff\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SKINCE_CABCOFF);
		break;
	case DISPPARAM_CABC_DIMMING:
		pr_info("panel cabc dimming\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABC_DIMMING);
		break;
	case DISPPARAM_DIMMING_OFF:
		pr_info("dimmingoff\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
		break;
	case DISPPARAM_DIMMING:
		pr_info("dimmingon\n");
		if (mi_cfg->skip_dimmingon != STATE_DIM_BLOCK) {
			if (ktime_after(ktime_get(), mi_cfg->fod_hbm_off_time)
				&& ktime_after(ktime_get(), mi_cfg->fod_backlight_off_time)) {
				dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGON);
			} else {
				pr_info("skip dimmingon due to hbm off\n");
			}
		} else {
			pr_info("skip dimmingon due to hbm on\n");
		}
		break;
	default:
		break;
	}

	temp = param & 0x0000F000;
	switch (temp) {
	case DISPPARAM_ACL_L1:
		pr_info("acl level 1\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ACL_L1);
		break;
	case DISPPARAM_ACL_L2:
		pr_info("acl level 2\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ACL_L2);
		break;
	case DISPPARAM_ACL_L3:
		pr_info("acl level 3\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ACL_L3);
		break;
	case DISPPARAM_ACL_OFF:
		pr_info("acl off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ACL_OFF);
		break;
	default:
		break;
	}

	temp = param & 0x000F0000;
	switch (temp) {
	case DISPPARAM_LCD_HBM_L1_ON:
		pr_info("lcd hbm l1 on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISP_LCD_HBM_L1_ON);
		break;
	case DISPPARAM_LCD_HBM_L2_ON:
		pr_info("lcd hbm  l2 on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISP_LCD_HBM_L2_ON);
		break;
	case DISPPARAM_LCD_HBM_OFF:
		pr_info("lcd hbm off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISP_LCD_HBM_OFF);
		break;
	case DISPPARAM_HBM_ON:
		pr_info("hbm on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_ON);
		break;
	case DISPPARAM_HBM_FOD_ON:
		pr_info("hbm fod on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD_ON);
		mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
		mi_cfg->fod_hbm_enabled = true;
		break;
	case DISPPARAM_HBM_FOD2NORM:
		pr_info("hbm fod to normal mode\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD2NORM);
		break;
	case DISPPARAM_HBM_FOD_OFF:
		pr_info("hbm fod off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD_OFF);
		mi_cfg->skip_dimmingon = STATE_DIM_RESTORE;
		mi_cfg->fod_hbm_enabled = false;
		mi_cfg->fod_hbm_off_time = ktime_add_ms(ktime_get(),
				mi_cfg->fod_off_dimming_delay);
		break;
	case DISPPARAM_HBM_OFF:
		pr_info("hbm off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_OFF);
		break;
	default:
		break;
	}

	temp = param & 0x00F00000;
	switch (temp) {
	case DISPPARAM_NORMALMODE1:
		pr_info("normal mode1\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_NORMAL1);
		break;
	case DISPPARAM_P3:
		pr_info("dci p3 mode\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CRC_DCIP3);
		break;
	case DISPPARAM_SRGB:
		pr_info("sRGB\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SRGB);
		break;
	case DISPPARAM_DOZE_BRIGHTNESS_HBM:
#ifdef CONFIG_FACTORY_BUILD
		pr_info("doze hbm On\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
		mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
#else
		if (mi_cfg->in_aod) {
			pr_info("doze hbm On\n");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
			mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
		}
#endif
		break;
	case DISPPARAM_DOZE_BRIGHTNESS_LBM:
#ifdef CONFIG_FACTORY_BUILD
		pr_info("doze lbm On\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
		mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
#else
		if (mi_cfg->in_aod) {
			pr_info("doze lbm On\n");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
			mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
		}
#endif
		break;
	case DISPPARAM_DOZE_OFF:
		pr_info("doze Off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP);
		break;
	case DISPPARAM_HBM_BACKLIGHT_RESEND:
		backlight_delta++;
		if (mi_cfg->bl_last_level >= panel->bl_config.bl_max_level - 1)
			resend_backlight = mi_cfg->bl_last_level -
				((backlight_delta%2 == 0) ? 1 : 2);
		else
			resend_backlight = mi_cfg->bl_last_level +
				((backlight_delta%2 == 0) ? 1 : 2);

		pr_info("backlight resend: bl_last_level = %d; resend_backlight = %d\n",
				mi_cfg->bl_last_level, resend_backlight);
		rc = dsi_panel_update_backlight(panel, resend_backlight);
		break;
	case DISPPARAM_FOD_BACKLIGHT:
		if (fod_backlight == 0x690)
			fod_backlight = 600;
		else if (fod_backlight == 0x7FF)
			fod_backlight = panel->bl_config.bl_max_level;

		if (fod_backlight == 0x1000) {
			pr_info("FOD backlight restore bl_last_level=%d\n",
				mi_cfg->bl_last_level);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
			rc = dsi_panel_update_backlight(panel, mi_cfg->bl_last_level);
		} else if (fod_backlight >= 0) {
			pr_info("FOD backlight set");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
			rc = dsi_panel_update_backlight(panel, fod_backlight);
			mi_cfg->fod_target_backlight = fod_backlight;
			mi_cfg->skip_dimmingon = STATE_NONE;
		}
		break;
	case DISPPARAM_CRC_OFF:
		pr_info("crc off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CRC_OFF);
		break;
	default:
		break;
	}

	temp = param & 0x0F000000;
	switch (temp) {
	case DISPPARAM_FOD_BACKLIGHT_ON:
		pr_info("fod_backlight_flag on\n");
		mi_cfg->fod_backlight_flag = true;
		break;
	case DISPPARAM_FOD_BACKLIGHT_OFF:
		pr_info("fod_backlight_flag off\n");
		mi_cfg->fod_backlight_flag = false;
		break;
	case DISPPARAM_ELVSS_DIMMING_ON:
		pr_info("elvss dimming on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ELVSS_DIMMING_OFF);
		break;
	case DISPPARAM_ELVSS_DIMMING_OFF:
		pr_info("elvss dimming off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ELVSS_DIMMING_OFF);
		break;
	case DISPPARAM_FLAT_MODE_ON:
		pr_info("flat mode on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);
		break;
	case DISPPARAM_FLAT_MODE_OFF:
		pr_info("flat mode off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_OFF);
		break;
	default:
		break;
	}

	temp = param & 0xF0000000;
	switch (temp) {
	case DISPPARAM_DFPS_LEVEL1:
		pr_debug("DFPS:30fps\n");
		panel->mi_cfg.panel_max_frame_rate = false;
		break;
	case DISPPARAM_DFPS_LEVEL2:
		pr_debug("DFPS:48fps\n");
		panel->mi_cfg.panel_max_frame_rate = false;
		break;
	case DISPPARAM_DFPS_LEVEL3:
		pr_debug("DFPS:50fps\n");
		panel->mi_cfg.panel_max_frame_rate = false;
		break;
	case DISPPARAM_DFPS_LEVEL4:
		pr_debug("DFPS:60fps\n");
		panel->mi_cfg.panel_max_frame_rate = false;
		break;
	case DISPPARAM_DFPS_LEVEL5:
		pr_debug("DFPS:90fps\n");
		panel->mi_cfg.panel_max_frame_rate = false;
		break;
	case DISPPARAM_DFPS_LEVEL6:
		pr_debug("DFPS:120fps\n");
		panel->mi_cfg.panel_max_frame_rate = true;
		break;
	default:
		break;
	}

exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

static void panelon_dimming_enable_delayed_work(struct work_struct *work)
{
	struct dsi_panel *panel = NULL;
	struct dsi_panel_mi_cfg *mi_cfg = container_of(work,
			struct dsi_panel_mi_cfg, cmds_work.work);

	if (mi_cfg)
		panel = container_of(mi_cfg, struct dsi_panel, mi_cfg);

	dsi_panel_disp_param_set(panel, DISPPARAM_CABC_DIMMING);
}

