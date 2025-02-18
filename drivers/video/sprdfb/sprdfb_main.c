/*
 * Copyright (C) 2012 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)		"sprdfb: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/uaccess.h>
#include <asm/uaccess.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_address.h>
#endif

#include "sprdfb.h"
#include "sprdfb_panel.h"
#include <mach/board.h>
#ifdef CONFIG_FB_MMAP_CACHED
#include <asm/pgtable.h>
#include <linux/mm.h>
#endif
#if defined(CONFIG_SEC_DEBUG)
#include <mach/sec_debug.h>
#endif

enum{
	SPRD_IN_DATA_TYPE_ABGR888 = 0,
	SPRD_IN_DATA_TYPE_BGR565,
/*
	SPRD_IN_DATA_TYPE_RGB666,
	SPRD_IN_DATA_TYPE_RGB555,
	SPRD_IN_DATA_TYPE_PACKET,
*/ /*not support*/
	SPRD_IN_DATA_TYPE_LIMIT
};

#define SPRDFB_IN_DATA_TYPE SPRD_IN_DATA_TYPE_ABGR888

#define SPRDFB_FRAMES_TO_SKIP 	(1)

#define SPRDFB_DEFAULT_FPS (60)

#define SPRDFB_ESD_TIME_OUT_CMD	(2000)

#define SPRDFB_ESD_TIME_OUT_VIDEO	(1000)

extern bool sprdfb_panel_get(struct sprdfb_device *dev);
extern int sprdfb_panel_probe(struct platform_device *pdev,
					struct sprdfb_device *dev);
#if defined(CONFIG_LCD_CLASS_DEVICE)
extern int sprdfb_lcd_class_register(struct platform_device *pdev,
					struct sprdfb_device *dev);
#endif
extern void sprdfb_panel_remove(struct sprdfb_device *dev);

extern struct display_ctrl sprdfb_dispc_ctrl ;
#ifdef CONFIG_FB_SC8825
extern struct display_ctrl sprdfb_lcdc_ctrl;
#endif

#ifdef CONFIG_OF
extern uint32_t g_dispc_base_addr;
#endif
int recovery_mode;

static unsigned PP[16];
static int frame_count = 0;

static int sprdfb_check_var(struct fb_var_screeninfo *var, struct fb_info *fb);
static int sprdfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *fb);
static int sprdfb_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg);
static void sprdfb_powerdown(struct fb_info *fb);
static void sprdfb_unblank(struct fb_info *fb);

static int sprdfb_blank(int blank, struct fb_info *info);
static int sprdfb_open(struct fb_info *info, int user);
static int sprdfb_release(struct fb_info *info, int user);
#ifdef CONFIG_FB_MMAP_CACHED
static int sprdfb_mmap(struct fb_info *info, struct vm_area_struct *vma);
#endif

static struct fb_ops sprdfb_ops = {
	.owner = THIS_MODULE,
	.fb_open = sprdfb_open,
	.fb_release = sprdfb_release,
	.fb_blank = sprdfb_blank,
	.fb_check_var = sprdfb_check_var,
	.fb_pan_display = sprdfb_pan_display,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_ioctl = sprdfb_ioctl,
#ifdef CONFIG_FB_MMAP_CACHED
	.fb_mmap = sprdfb_mmap,
#endif
};

#ifdef CONFIG_FB_MMAP_CACHED
static int sprdfb_mmap(struct fb_info *info,struct vm_area_struct *vma)
{
	struct sprdfb_device *dev = NULL;
	if(NULL == info){
			printk(KERN_ERR "sprdfb: sprdfb_ioctl error. (Invalid Parameter)");
			return -1;
	}

	dev = info->par;
	printk("sprdfb: sprdfb_mmap,vma=0x%x\n",vma);
	vma->vm_page_prot = pgprot_cached(vma->vm_page_prot);
	dev->ctrl->set_vma(vma);

	return vm_iomap_memory(vma, info->fix.smem_start, info->fix.smem_len);
}
#endif

static int sprdfb_open(struct fb_info *info, int user)
{
	pr_info("%s:by %s\n", __func__, current->comm);

	return 0;
}

static int sprdfb_release(struct fb_info *info, int user)
{
	pr_info("%s:by %s\n", __func__, current->comm);

	return 0;
}

static void sprdfb_powerdown(struct fb_info *fb)
{
	struct sprdfb_device *dev = fb->par;

	if (dev->ctrl->suspend)
		dev->ctrl->suspend(dev);
}

static void sprdfb_unblank(struct fb_info *fb)
{
	struct sprdfb_device *dev = fb->par;

	if (dev->ctrl->resume)
		dev->ctrl->resume(dev);
}

void sprdfb_power_setting_idle(struct sprdfb_device *dev)
{
	int ret;

	if (dev->is_blank_setting) {
		pr_info("wait:blank_setting\n");
		ret = wait_for_completion_timeout(
				&dev->blank_set_comp,
			msecs_to_jiffies(600));
		if (ret < 0)
			ret = -ERESTARTSYS;
		else if (!ret)
			pr_err("%s wait for blank_set_comp timeout %d %d",
				__func__, ret, dev->is_blank_setting);
		if (ret <= 0) {
			dev->is_blank_setting = false;
			complete_all(&dev->blank_set_comp);
		}
		pr_info("done:blank_setting\n");
	}
}

static int sprdfb_blank(int blank, struct fb_info *info)
{
	struct sprdfb_device *dev = info->par;
#ifdef CONFIG_DRM_SPRD
	struct sprdfb_nb_event event;
#endif

	pr_info("%s:blank[%d]\n", __func__, blank);
	INIT_COMPLETION(dev->blank_set_comp);
	dev->is_blank_setting = true;

	switch (blank) {
	case FB_BLANK_UNBLANK:
#ifdef CONFIG_DRM_SPRD
		event.index = 0;
		event.data = (void *)SPRDFB_DPMS_ON;
		sprdfb_nb_send_event(SPRDFB_SET_DPMS,
			(void *)&event);
#endif
		/* resume */
		sprdfb_unblank(info);
		break;
	case FB_BLANK_POWERDOWN:
		/* suspend */
		sprdfb_powerdown(info);
#ifdef CONFIG_DRM_SPRD
		event.index = 0;
		event.data = (void *)SPRDFB_DPMS_OFF;
		sprdfb_nb_send_event(SPRDFB_SET_DPMS,
			(void *)&event);
#endif
		break;
	}

	dev->is_blank_setting = false;
	complete_all(&dev->blank_set_comp);

	dev->dbg_cnt = 3;
	pr_info("%s:done:blank[%d]\n", __func__, blank);

	return 0;
}

static int setup_fb_mem(struct sprdfb_device *dev, struct platform_device *pdev)
{
#ifdef CONFIG_PAN_DISPLAY
	uint32_t len, addr;
	bool use_reserve_mem;
    uint32_t reserve_mem[2];
    int ret;

#ifdef CONFIG_FB_LOW_RES_SIMU
	if((0!= dev->panel->display_width) && (0 != dev->panel->display_height)){
		len = dev->panel->display_width * dev->panel->display_height * (dev->bpp / 8) * (dev->framebuffer_nr);
	}else
#endif
	len = dev->panel->width * dev->panel->height * (dev->bpp / 8) * (dev->framebuffer_nr);

#ifdef CONFIG_OF
    use_reserve_mem = of_property_read_bool(pdev->dev.of_node, "sprd,fb_use_reservemem");
    if(use_reserve_mem){
        ret = of_property_read_u32_array(pdev->dev.of_node, "sprd,fb_mem",
				reserve_mem, 2);
		if(0 != ret){
		    printk(KERN_ERR "sprdfb: Failed to got framebuffer memory from dt file\n");
		}
    }
#else
#ifdef	CONFIG_FB_LCD_RESERVE_MEM
    use_reserve_mem = true;
    reserve_mem[0] = SPRD_FB_MEM_BASE;
    reserve_mem[1] = SPRD_FB_MEM_SIZE;
#else
    use_reserve_mem = false;
#endif
#endif

    if(!use_reserve_mem){
        addr = __get_free_pages(GFP_ATOMIC | __GFP_ZERO, get_order(len));
        if (!addr) {
        printk(KERN_ERR "sprdfb: Failed to allocate framebuffer memory\n");
            return -ENOMEM;
        }
        printk(KERN_INFO "sprdfb: got %d bytes mem at 0x%x\n", len, addr);

        dev->fb->fix.smem_start = __pa(addr);
        dev->fb->fix.smem_len = len;
        dev->fb->screen_base = (char*)addr;
    }else{
        dev->fb->fix.smem_start = reserve_mem[0];
        printk("sprdfb: setup_fb_mem--smem_start:%lx,len:%d,reserved len:%d\n",dev->fb->fix.smem_start,len,reserve_mem[1]);
        addr =  (uint32_t)ioremap(dev->fb->fix.smem_start, reserve_mem[1]);
        if (!addr) {
            printk(KERN_ERR "sprdfb: Unable to map framebuffer base: 0x%08x\n", addr);
            return -ENOMEM;
        }
        dev->fb->fix.smem_len = reserve_mem[1];
        dev->fb->screen_base = (char*)addr;
	}
	return 0;
#else
    dev->fb->fix.smem_start = 0;
    dev->fb->fix.smem_len = 0;
    dev->fb->screen_base = 0;
#endif
}

static void setup_fb_info(struct sprdfb_device *dev)
{
	struct fb_info *fb = dev->fb;
	struct panel_spec *panel = dev->panel;
	int r;

	fb->fbops = &sprdfb_ops;
	fb->flags = FBINFO_DEFAULT;

	/* finish setting up the fb_info struct */
	strncpy(fb->fix.id, "sprdfb", 16);
	fb->fix.ypanstep = 1;
	fb->fix.type = FB_TYPE_PACKED_PIXELS;
	fb->fix.visual = FB_VISUAL_TRUECOLOR;
#ifdef CONFIG_FB_LOW_RES_SIMU
	if((0 != panel->display_width) && (0 != panel->display_height)){
		fb->fix.line_length = panel->display_width * dev->bpp / 8;

		fb->var.xres = panel->display_width;
		fb->var.yres = panel->display_height;
		fb->var.width = panel->width_mm;
		fb->var.height = panel->height_mm;

		fb->var.xres_virtual = panel->display_width;
		fb->var.yres_virtual = panel->display_height * (dev->framebuffer_nr);
	}else
#endif
	{
		fb->fix.line_length = panel->width * dev->bpp / 8;

		fb->var.xres = panel->width;
		fb->var.yres = panel->height;
		fb->var.width = panel->width_mm;
		fb->var.height = panel->height_mm;

		fb->var.xres_virtual = panel->width;
		fb->var.yres_virtual = panel->height * (dev->framebuffer_nr);
	}
	fb->var.bits_per_pixel = dev->bpp;
#ifdef CONFIG_FB_LOW_RES_SIMU
	if((0 != panel->display_width) && (0 != panel->display_height)){
		if(0 != dev->panel->fps){
			fb->var.pixclock = ((1000000000 /panel->display_width) * 1000) / (dev->panel->fps * panel->display_height);
		}else{
			fb->var.pixclock = ((1000000000 /panel->display_width) * 1000) / (SPRDFB_DEFAULT_FPS * panel->display_height);
		}
	}else
#endif
	{
		if(0 != dev->panel->fps){
			fb->var.pixclock = ((1000000000 /panel->width) * 1000) / (dev->panel->fps * panel->height);
		}else{
			fb->var.pixclock = ((1000000000 /panel->width) * 1000) / (SPRDFB_DEFAULT_FPS * panel->height);
		}
	}

	fb->var.accel_flags = 0;
	fb->var.yoffset = 0;

	/* only support two pixel format */
	if (dev->bpp == 32) { /* ABGR */
		fb->var.red.offset     = 24;
		fb->var.red.length     = 8;
		fb->var.red.msb_right  = 0;
		fb->var.green.offset   = 16;
		fb->var.green.length   = 8;
		fb->var.green.msb_right = 0;
		fb->var.blue.offset    = 8;
		fb->var.blue.length    = 8;
		fb->var.blue.msb_right = 0;
	} else { /*BGR*/
		fb->var.red.offset     = 11;
		fb->var.red.length     = 5;
		fb->var.red.msb_right  = 0;
		fb->var.green.offset   = 5;
		fb->var.green.length   = 6;
		fb->var.green.msb_right = 0;
		fb->var.blue.offset    = 0;
		fb->var.blue.length    = 5;
		fb->var.blue.msb_right = 0;
	}
	r = fb_alloc_cmap(&fb->cmap, 16, 0);
	fb->pseudo_palette = PP;

	PP[0] = 0;
	for (r = 1; r < 16; r++){
		PP[r] = 0xffffffff;
#if defined(CONFIG_SEC_DEBUG)
	/*{{ Mark for GetLog*/
	sec_getlog_supply_fbinfo(phys_to_virt(fb->fix.smem_start) , \
						fb->var.xres, fb->var.yres, \
						fb->var.bits_per_pixel, \
						dev->framebuffer_nr);
#endif
	}
}

static int __init mode_get(char *str)
{
	if (strcmp(str,"2") == 0)
		recovery_mode = 1;
	else
		recovery_mode = 0;
	return 1;
}
__setup("bootmode=", mode_get);

static void fb_free_resources(struct sprdfb_device *dev)
{
	if (dev == NULL)
		return;

	if (&dev->fb->cmap != NULL) {
		fb_dealloc_cmap(&dev->fb->cmap);
	}
	if (dev->fb->screen_base) {
		free_pages((unsigned long)dev->fb->screen_base,
				get_order(dev->fb->fix.smem_len));
	}
	unregister_framebuffer(dev->fb);
	framebuffer_release(dev->fb);
}

static int sprdfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *fb)
{
	int32_t ret;
	struct sprdfb_device *dev = fb->par;

	pr_debug("sprdfb: [%s]\n", __FUNCTION__);

	if(frame_count < SPRDFB_FRAMES_TO_SKIP) {
		frame_count++;
		return 0;
    }

	if(0 == dev->enable){
		printk(KERN_ERR "sprdfb: [%s]: Invalid Device status %d", __FUNCTION__, dev->enable);
		return -1;
	}

	ret = dev->ctrl->refresh(dev);
	if (ret) {
		printk(KERN_ERR "sprdfb: failed to refresh !!!!\n");
		return -1;
	}

	return 0;
}

#include <video/sprd_fb.h>
static int sprdfb_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg)
{
	int result = -1;
	struct sprdfb_device *dev = NULL;
#ifdef CONFIG_FB_LCD_OVERLAY_SUPPORT
	overlay_info local_overlay_info;
	overlay_display local_overlay_display;
	int layer_index;
#endif
	void __user *argp = (void __user *)arg;

	if(NULL == info){
		printk(KERN_ERR "sprdfb: sprdfb_ioctl error. (Invalid Parameter)");
		return -1;
	}

	dev = info->par;

	sprdfb_power_setting_idle(dev);

	switch(cmd){
#ifdef CONFIG_FB_LCD_OVERLAY_SUPPORT
	case SPRD_FB_SET_OVERLAY:
		memset(&local_overlay_info,0,sizeof(local_overlay_info));
		if (copy_from_user(&local_overlay_info, argp, sizeof(local_overlay_info))){
			printk("sprdfb: SET_OVERLAY copy failed!\n");
			return -EFAULT;
		}

		if(NULL != dev->ctrl->enable_overlay)
			result = dev->ctrl->enable_overlay(dev, &local_overlay_info);
		break;
	case SPRD_FB_UNSET_OVERLAY:
		if (copy_from_user(&layer_index, argp, sizeof(layer_index))){
			printk("sprdfb: UNSET_OVERLAY copy failed!\n");
			return -EFAULT;
		}

		if(NULL != dev->ctrl->disable_overlay)
			result = dev->ctrl->disable_overlay(dev, layer_index);
		break;
	case SPRD_FB_DISPLAY_OVERLAY:
		memset(&local_overlay_display,0,sizeof(local_overlay_display));
		if (copy_from_user(&local_overlay_display, argp, sizeof(local_overlay_display))){
			printk("sprdfb: DISPLAY_OVERLAY copy failed!\n");
			return -EFAULT;
		}
		if(NULL != dev->ctrl->display_overlay){
			result = dev->ctrl->display_overlay(dev, &local_overlay_display);
		}
		break;
#endif
#ifdef CONFIG_FB_VSYNC_SUPPORT
	case FBIO_WAITFORVSYNC:
		pr_debug(KERN_INFO "sprdfb: [%s]: FBIO_WAITFORVSYNC\n", __FUNCTION__);
		if(NULL != dev->ctrl->wait_for_vsync){
			result = dev->ctrl->wait_for_vsync(dev);
		}
		break;
#endif

#ifdef CONFIG_FB_DYNAMIC_FREQ_SCALING
	case SPRD_FB_CHANGE_FPS:
		{
			int fps;
			result = copy_from_user(&fps, argp, sizeof(fps));
			if (result) {
				pr_err("%s: copy_from_user failed", __func__);
				return result;
			}
			pr_info("%s: fps will be changed to %d via ioctl\n",
					__func__, fps);
			result = sprdfb_chg_clk_intf(dev,
					SPRDFB_DYNAMIC_FPS, fps);
			if (result) {
				pr_err("%s: fps is set fail. fps=%d, ret=%d\n",
						__func__, fps, result);
				return result;
			}
			break;
		}
#endif

	case SPRD_FB_IS_REFRESH_DONE:
		pr_debug(KERN_INFO "sprdfb: [%s]: SPRD_FB_IS_REFRESH_DONE\n", __FUNCTION__);
		if(NULL != dev->ctrl->is_refresh_done){
			result = dev->ctrl->is_refresh_done(dev);
		}
		break;

	default:
		printk(KERN_INFO "sprdfb: [%s]: unknown cmd(%d)\n", __FUNCTION__, cmd);
		break;
	}

	pr_debug(KERN_INFO "sprdfb: [%s]: return %d\n",__FUNCTION__, result);
	return result;
}


static int sprdfb_check_var(struct fb_var_screeninfo *var, struct fb_info *fb)
{
	if ((var->xres != fb->var.xres) ||
		(var->yres != fb->var.yres) ||
		(var->xres_virtual != fb->var.xres_virtual) ||
		(var->yres_virtual != fb->var.yres_virtual) ||
		(var->xoffset != fb->var.xoffset) ||
#ifndef BIT_PER_PIXEL_SURPPORT
		(var->bits_per_pixel != fb->var.bits_per_pixel) ||
#endif
		(var->grayscale != fb->var.grayscale))
			return -EINVAL;
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void sprdfb_early_suspend (struct early_suspend* es)
{
	struct sprdfb_device *dev = container_of(es, struct sprdfb_device, early_suspend);
	struct fb_info *fb = dev->fb;

	pr_info("%s\n", __func__);
	fb_set_suspend(fb, FBINFO_STATE_SUSPENDED);

	if (!lock_fb_info(fb)) {
		return ;
	}
	dev->ctrl->suspend(dev);
	unlock_fb_info(fb);
	pr_info("%s:done\n", __func__);
}

static void sprdfb_late_resume (struct early_suspend* es)
{
	struct sprdfb_device *dev = container_of(es, struct sprdfb_device, early_suspend);
	struct fb_info *fb = dev->fb;

	pr_info("%s\n", __func__);
	if (!lock_fb_info(fb)) {
		return ;
	}
	dev->ctrl->resume(dev);
	unlock_fb_info(fb);

	fb_set_suspend(fb, FBINFO_STATE_RUNNING);
	pr_info("%s:done\n", __func__);
}
#endif

#ifdef CONFIG_FB_ESD_SUPPORT
static void ESD_work_func(struct work_struct *work)
{
	struct sprdfb_device *dev = container_of(work, struct sprdfb_device, ESD_work.work);

	pr_debug("sprdfb: [%s] enter!\n", __FUNCTION__);

	//do real ESD check
	//mdelay(1000);
	if(NULL != dev->ctrl->ESD_check){
		dev->ctrl->ESD_check(dev);
	}

	if(0 != dev->enable){
		pr_debug("sprdfb: reschedule ESD workqueue!\n");
		schedule_delayed_work(&dev->ESD_work, msecs_to_jiffies(dev->ESD_timeout_val));
		dev->ESD_work_start = true;
	}else{
		printk("sprdfb: DON't reschedule ESD workqueue since device not avialbe!!\n");
	}

	pr_debug("sprdfb: [%s] leave!\n", __FUNCTION__);
}
#endif

#ifdef SPRDFB_OVERLAY_DEBUG
static ssize_t sprdfb_overlay_info_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct sprdfb_device *fb_dev = (struct sprdfb_device *)fbi->par;
	char *layer_type = 0;
	int pos = 0;

	switch (fb_dev->overlay_data.layer_index) {
		case SPRD_LAYER_IMG:
			layer_type = "IMG";
			break;
		case SPRD_LAYER_OSD:
			layer_type = "OSD";
			break;
		case SPRD_LAYER_BOTH:
			layer_type = "OSD+IMG";
			break;
		default:
			layer_type = "INVALID";
			break;
	}

	pr_debug("%s:layer[%d]osd[0x%p]img[0x%p]\n", __func__,
			fb_dev->overlay_data.layer_index,
			fb_dev->overlay_data.osd_buffer,
			fb_dev->overlay_data.img_buffer);

	pos += sprintf(buf+pos, "layer[%s], osd[0x%p], img[0x%p], ",
			layer_type,
			fb_dev->overlay_data.osd_buffer,
			fb_dev->overlay_data.img_buffer);
	pos += sprintf(buf+pos, "y_endian[%d], uv_endian[%d], ",
			fb_dev->overlay_data.y_endian,
			fb_dev->overlay_data.uv_endian);
	pos += sprintf(buf+pos, "dst: x[%d], y[%d], w[%d], h[%d]\n",
			fb_dev->overlay_data.rect.x,
			fb_dev->overlay_data.rect.y,
			fb_dev->overlay_data.rect.w,
			fb_dev->overlay_data.rect.h);

	return pos;
}

static DEVICE_ATTR(overlay_info, S_IRUGO, sprdfb_overlay_info_show, NULL);

static struct attribute *sprdfb_overlay_info_attrs[] = {
	&dev_attr_overlay_info.attr,
	NULL,
};

static struct attribute_group sprdfb_ov_info_attrs_group = {
	.attrs = sprdfb_overlay_info_attrs,
};
#endif

#ifdef CONFIG_DRM_SPRD
static BLOCKING_NOTIFIER_HEAD(sprdfb_nb_list);

int sprdfb_nb_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(
		&sprdfb_nb_list, nb);
}

int sprdfb_nb_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(
		&sprdfb_nb_list, nb);
}

int sprdfb_nb_send_event(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(
		&sprdfb_nb_list, val, v);
}
#endif

static int sprdfb_probe(struct platform_device *pdev)
{
	struct fb_info *fb = NULL;
	struct sprdfb_device *dev = NULL;
	int ret = 0;
#ifdef CONFIG_OF
	struct resource r;

	pr_debug(KERN_INFO "sprdfb: [%s]\n", __FUNCTION__);
#else
	pr_debug(KERN_INFO "sprdfb: [%s], id = %d\n", __FUNCTION__, pdev->id);
#endif

	fb = framebuffer_alloc(sizeof(struct sprdfb_device), &pdev->dev);
	if (!fb) {
		printk(KERN_ERR "sprdfb: sprdfb_probe allocate buffer fail.\n");
		ret = -ENOMEM;
		goto err0;
	}

	dev = fb->par;
	dev->fb = fb;
#ifdef CONFIG_OF
	dev->of_dev = &(pdev->dev);

	dev->dev_id = of_alias_get_id(pdev->dev.of_node, "lcd");
	printk("sprdfb: [%s] id = %d\n", __FUNCTION__, dev->dev_id);
#else
	dev->dev_id = pdev->id;
#endif

	dev->framebuffer_nr = 1;

	if((SPRDFB_MAINLCD_ID != dev->dev_id) &&(SPRDFB_SUBLCD_ID != dev->dev_id)){
		printk(KERN_ERR "sprdfb: sprdfb_probe fail. (unsupported device id)\n");
		goto err0;
	}

	switch(SPRDFB_IN_DATA_TYPE){
	case SPRD_IN_DATA_TYPE_ABGR888:
		dev->bpp = 32;
		break;
	case SPRD_IN_DATA_TYPE_BGR565:
		dev->bpp = 16;
		break;
	default:
		dev->bpp = 32;
		break;
	}

	if(SPRDFB_MAINLCD_ID == dev->dev_id){
		dev->ctrl = &sprdfb_dispc_ctrl;
#ifdef CONFIG_OF
		if(0 != of_address_to_resource(pdev->dev.of_node, 0, &r)){
			printk(KERN_ERR "sprdfb: sprdfb_probe fail. (can't get register base address)\n");
			goto err0;
		}
		g_dispc_base_addr = r.start;
		printk("sprdfb: set g_dispc_base_addr = 0x%x\n", g_dispc_base_addr);
#endif
#ifdef CONFIG_FB_SC8825
	}else{
		dev->ctrl = &sprdfb_lcdc_ctrl;
#endif
	}

	dev->logo_buffer_addr_v = 0;

	sprdfb_panel_get(dev);

	dev->ctrl->early_init(dev);

	if(!dev->panel_ready){
		if (!sprdfb_panel_probe(pdev, dev)) {
			ret = -EIO;
		}
	} else {
#if defined(CONFIG_LCD_CLASS_DEVICE)
		if(!sprdfb_lcd_class_register(pdev, dev)) {
			printk("sprdfb: failed to register lcd_class\n");
			ret = -EIO;
			goto cleanup;
		}
#endif
	}

	ret = setup_fb_mem(dev, pdev);
	if (ret) {
		goto cleanup;
	}

	setup_fb_info(dev);
	/* register framebuffer device */
	ret = register_framebuffer(fb);
	if (ret) {
		printk(KERN_ERR "sprdfb: sprdfb_probe register framebuffer fail.\n");
		goto cleanup;
	}
	platform_set_drvdata(pdev, dev);

	init_completion(&dev->blank_set_comp);

	sprdfb_create_sysfs(dev);
	dev->ctrl->init(dev);

#ifdef CONFIG_HAS_EARLYSUSPEND
	dev->early_suspend.suspend = sprdfb_early_suspend;
	dev->early_suspend.resume  = sprdfb_late_resume;
	dev->early_suspend.level   = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	register_early_suspend(&dev->early_suspend);
#endif

#ifdef CONFIG_FB_ESD_SUPPORT
	pr_debug("sprdfb: Init ESD work queue!\n");
	INIT_DELAYED_WORK(&dev->ESD_work, ESD_work_func);
//	sema_init(&dev->ESD_lock, 1);

	if(SPRDFB_PANEL_IF_DPI == dev->panel_if_type){
		dev->ESD_timeout_val = SPRDFB_ESD_TIME_OUT_VIDEO;
	}else{
		dev->ESD_timeout_val = SPRDFB_ESD_TIME_OUT_CMD;
	}

	dev->ESD_work_start = false;
	dev->check_esd_time = 0;
	dev->reset_dsi_time = 0;
	dev->panel_reset_time = 0;
#endif

#ifdef SPRDFB_OVERLAY_DEBUG
	ret = sysfs_create_group(&fb->dev->kobj, &sprdfb_ov_info_attrs_group);
	if (ret)
		pr_err("sysfs group creation failed, ret = %d\n", ret);
#endif

	return 0;

cleanup:
	sprdfb_panel_remove(dev);
	dev->ctrl->uninit(dev);
	fb_free_resources(dev);
err0:
	dev_err(&pdev->dev, "failed to probe sprdfb\n");
	return ret;
}

static int sprdfb_remove(struct platform_device *pdev)
{
	struct sprdfb_device *dev = platform_get_drvdata(pdev);

	printk("sprdfb: [%s]\n",__FUNCTION__);
	sprdfb_remove_sysfs(dev);
	sprdfb_panel_remove(dev);
	dev->ctrl->uninit(dev);
	fb_free_resources(dev);
	return 0;
}

static void sprdfb_shutdown(struct platform_device *pdev)
{
	struct sprdfb_device *dev = platform_get_drvdata(pdev);
	struct fb_info *fb = dev->fb;

	printk("sprdfb: [%s]\n",__FUNCTION__);
#ifdef CONFIG_LCD_ESD_RECOVERY
	esd_det_disable(dev->panel->esd_info);
#endif
	fb_set_suspend(fb, FBINFO_STATE_SUSPENDED);
	if (!lock_fb_info(fb))
		return;
	dev->ctrl->suspend(dev);
	unlock_fb_info(fb);
	return;
}

#ifdef CONFIG_OF
static const struct of_device_id sprdfb_dt_ids[] = {
	{ .compatible = "sprd,sprdfb", },
	{}
};
#endif

static struct platform_driver sprdfb_driver = {
	.probe = sprdfb_probe,
	.remove = sprdfb_remove,
	.shutdown = sprdfb_shutdown,
	.driver = {
		.name = "sprd_fb",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(sprdfb_dt_ids),
#endif
	},
};


static int __init sprdfb_init(void)
{
	return platform_driver_register(&sprdfb_driver);
}

static void __exit sprdfb_exit(void)
{
	return platform_driver_unregister(&sprdfb_driver);
}

module_init(sprdfb_init);
module_exit(sprdfb_exit);
