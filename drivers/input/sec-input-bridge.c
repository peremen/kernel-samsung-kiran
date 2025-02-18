/*
 *  sec-input-bridge.c - Specific control input event bridge
 *  for Samsung Electronics
 *
 *  Copyright (C) 2010 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/input/sec-input-bridge.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#endif

#define SAFEMODE_INIT		0xcafecafe
#define SAFEMODE_TIMEOUT	5000 /* 5 seconds */

enum inpup_mode {
	INPUT_LOGDUMP = 0,
	INPUT_SAFEMODE,
	INPUT_MAX
};

struct device *sec_input_bridge;

struct sec_input_bridge {
	struct sec_input_bridge_platform_data *pdata;
	struct work_struct work;
	struct mutex lock;
	struct platform_device *dev;
	struct timer_list safemode_timer;
	/*
	 * Because this flag size is 32 byte, Max map table number is 32.
	 */
	u32 send_uevent_flag;
	u8 check_index[32];
	u32 safemode_flag;
};

static void input_bridge_set_ids(struct input_device_id *ids, unsigned int type,
				 unsigned int code)
{
	switch (type) {
	case EV_KEY:
		ids->flags = INPUT_DEVICE_ID_MATCH_KEYBIT;
		__set_bit(code, ids->keybit);
		break;

	case EV_REL:
		ids->flags = INPUT_DEVICE_ID_MATCH_RELBIT;
		__set_bit(code, ids->relbit);
		break;

	case EV_ABS:
		ids->flags = INPUT_DEVICE_ID_MATCH_ABSBIT;
		__set_bit(code, ids->absbit);
		break;

	case EV_MSC:
		ids->flags = INPUT_DEVICE_ID_MATCH_MSCIT;
		__set_bit(code, ids->mscbit);
		break;

	case EV_SW:
		ids->flags = INPUT_DEVICE_ID_MATCH_SWBIT;
		__set_bit(code, ids->swbit);
		break;

	case EV_LED:
		ids->flags = INPUT_DEVICE_ID_MATCH_LEDBIT;
		__set_bit(code, ids->ledbit);
		break;

	case EV_SND:
		ids->flags = INPUT_DEVICE_ID_MATCH_SNDBIT;
		__set_bit(code, ids->sndbit);
		break;

	case EV_FF:
		ids->flags = INPUT_DEVICE_ID_MATCH_FFBIT;
		__set_bit(code, ids->ffbit);
		break;

	case EV_PWR:
		/* do nothing */
		break;

	default:
		printk(KERN_ERR
		       "input_bridge_set_ids: unknown type %u (code %u)\n",
		       type, code);
		return;
	}

	ids->flags |= INPUT_DEVICE_ID_MATCH_EVBIT;
	__set_bit(type, ids->evbit);
}

static void input_bridge_work(struct work_struct *work)
{
	struct sec_input_bridge *bridge = container_of(work,
						       struct sec_input_bridge,
						       work);
	int state, i;
	char env_str[16];
	char *envp[] = { env_str, NULL };

	mutex_lock(&bridge->lock);

	for (i = 0; i < bridge->pdata->num_map; i++) {
		if (bridge->send_uevent_flag & (1 << i)) {
			if (bridge->pdata->mmap[i].enable_uevent) {
				printk(KERN_ERR
				     "!!!!sec-input-bridge: OK!!, KEY input matched , now send uevent!!!!\n");
				sprintf(env_str, "%s=%s",
					bridge->pdata->mmap[i].uevent_env_str,
					bridge->pdata->mmap[i]
						.uevent_env_value);
				printk(KERN_ERR
				    "<kobject_uevent_env for sec-input-bridge>, event: %s\n",
				    env_str);
				state =
				    kobject_uevent_env(&sec_input_bridge->kobj,
						       bridge->pdata->mmap[i].
						       uevent_action, envp);
				if (state != 0)
					printk(KERN_ERR
					       "<error, kobject_uevent_env fail> with action : %d\n",
					       bridge->pdata->mmap[i].
					       uevent_action);
			}
			if (bridge->pdata->mmap[i].pre_event_func) {
				bridge->pdata->mmap[i].
				pre_event_func(bridge->pdata->event_data);
			}

			bridge->send_uevent_flag &= ~(1 << i);
		}
	}

	if (bridge->pdata->lcd_warning_func)
		bridge->pdata->lcd_warning_func();

	mutex_unlock(&bridge->lock);

	printk(KERN_INFO "<sec-input-bridge> all process done !!!!\n");
}

static void input_bridge_safemode_timer(unsigned long data)
{
	struct sec_input_bridge *sec_bridge =
		(struct sec_input_bridge *)data;

	if (sec_bridge->safemode_flag != SAFEMODE_INIT) {
		dev_info(&sec_bridge->dev->dev, "%s: LONG PRESS IS FAILED.\n", __func__);
	} else {
		dev_info(&sec_bridge->dev->dev, "%s: LONG PRESS IS DETECTED.\n", __func__);
		sec_bridge->send_uevent_flag |= 1 << INPUT_SAFEMODE;
		schedule_work(&sec_bridge->work);
	}
	return;
}

static void input_bridge_check_safemode(struct input_handle *handle, unsigned int type,
			       unsigned int code, int value)
{
	struct input_handler *sec_bridge_handler = handle->handler;
	struct sec_input_bridge *sec_bridge = sec_bridge_handler->private;
	struct sec_input_bridge_platform_data *pdata = sec_bridge->pdata;
	struct sec_input_bridge_mmap *mmap = &pdata->mmap[INPUT_SAFEMODE];
	struct sec_input_bridge_mkey *mkey_map = mmap->mkey_map;
	static int first_power_press = true;
	static int first_power_release = true;

	/* in probe time of the powerkey, powerkey press/release event is released */
	if (code == KEY_POWER) {
		if ((first_power_press == true) && (value == 1)) {
			first_power_press = false;
			dev_info(&sec_bridge->dev->dev,
				"%s: it is ignored the first powerkey press.\n", __func__);
			return;
		} else if ((first_power_release == true) && (value == 0)) {
			first_power_press = false;
			dev_info(&sec_bridge->dev->dev,
				"%s: it is ignored the first powerkey release.\n", __func__);
			return;
		}
	}

	if ((code != mkey_map[0].code) || (value != mkey_map[0].type)) {
		sec_bridge->safemode_flag = false;
		dev_info(&sec_bridge->dev->dev,
			"%s: safemode_flag is disabled.\n", __func__);
	}
	else {
		mod_timer(&sec_bridge->safemode_timer,\
				jiffies + msecs_to_jiffies(SAFEMODE_TIMEOUT));
		dev_info(&sec_bridge->dev->dev, "%s: timer is set. (%dms)\n",
					__func__, SAFEMODE_TIMEOUT);
	}
}

static void input_bridge_check_logdump(struct input_handle *handle, unsigned int type,
			       unsigned int code)
{
	struct input_handler *sec_bridge_handler = handle->handler;
	struct sec_input_bridge *sec_bridge = sec_bridge_handler->private;
	struct sec_input_bridge_platform_data *pdata = sec_bridge->pdata;
	struct sec_input_bridge_mmap *mmap = &pdata->mmap[INPUT_LOGDUMP];

	if (sec_bridge->check_index[INPUT_LOGDUMP] > mmap->num_mkey) {
		printk(KERN_ERR "sec_bridge->check_index[INPUT_LOGDUMP] = [%d]",\
			sec_bridge->check_index[INPUT_LOGDUMP]);
		sec_bridge->check_index[INPUT_LOGDUMP] = 0;
		return;
	}

	if (mmap->mkey_map[sec_bridge->check_index[INPUT_LOGDUMP]].code == code) {
		sec_bridge->check_index[INPUT_LOGDUMP]++;
		if ((sec_bridge->check_index[INPUT_LOGDUMP]) >= mmap->num_mkey) {
			sec_bridge->send_uevent_flag |= (1 << INPUT_LOGDUMP);
			schedule_work(&sec_bridge->work);
			sec_bridge->check_index[INPUT_LOGDUMP] = 0;
		}
	} else if (mmap->mkey_map[0].code == code)
		sec_bridge->check_index[INPUT_LOGDUMP] = 1;
	else
		sec_bridge->check_index[INPUT_LOGDUMP] = 0;
}

static void input_bridge_event(struct input_handle *handle, unsigned int type,
			       unsigned int code, int value)
{
	struct input_handler *sec_bridge_handler = handle->handler;
	struct sec_input_bridge *sec_bridge = sec_bridge_handler->private;
	int rep_check;

	if ((code != KEY_VOLUMEDOWN) && (code != KEY_VOLUMEUP) &&
	    (code != KEY_POWER) && (code != KEY_MENU))
		return;

	rep_check = test_bit(EV_REP, sec_bridge_handler->id_table->evbit);
	rep_check = (rep_check << 1) | 1;

	switch (type) {
	case EV_KEY:
		if (value & rep_check)
			input_bridge_check_logdump
					    (handle, type, code);
		if (sec_bridge->safemode_flag == SAFEMODE_INIT)
			input_bridge_check_safemode
				    (handle, type, code, value);
		break;

	default:
		break;
	}

}

static int input_bridge_check_support_dev (struct input_dev *dev)
{
	struct sec_input_bridge *bridge;
	int i;

	if (!sec_input_bridge) {
		printk(KERN_ERR "%s: sec_input_bridge is null.", __func__);
		return -1;
	}

	bridge = dev_get_drvdata(sec_input_bridge);
	for(i = 0; i < bridge->pdata->num_dev; i++) {
		if (!strncmp(dev->name, bridge->pdata->support_dev_name[i],\
				strlen(dev->name)))
			return 0;
	}

	return -1;
}

static int input_bridge_connect(struct input_handler *handler,
				struct input_dev *dev,
				const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;


	if (input_bridge_check_support_dev(dev)) {
		printk(KERN_ERR "%s: unsupport device.[%s]",
			__func__, dev->name);
		return -ENODEV;
	    }

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "sec-input-bridge";

	error = input_register_handle(handle);
	if (error) {
		printk(KERN_ERR
		       "sec-input-bridge: Failed to register input bridge handler, "
		       "error %d\n", error);
		kfree(handle);
		return error;
	}

	error = input_open_device(handle);
	if (error) {
		printk(KERN_ERR
		       "sec-input-bridge: Failed to open input bridge device, "
		       "error %d\n", error);
		input_unregister_handle(handle);
		kfree(handle);
		return error;
	}

	return 0;
}

static void input_bridge_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static struct input_handler input_bridge_handler = {
	.event = input_bridge_event,
	.connect = input_bridge_connect,
	.disconnect = input_bridge_disconnect,
	.name = "sec-input-bridge",
};

#ifdef CONFIG_OF
static int sec_input_bridge_parse_dt(struct device *dev,
			struct sec_input_bridge_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	struct property *prop;
	enum mkey_check_option option;
	unsigned int *map;
	const char *out_prop;
	int i, j, rc, map_size;

	rc = of_property_read_u32(np, "input_bridge,num_map",\
			(unsigned int *)&pdata->num_map);
	if (rc) {
		dev_err(dev, "failed to get num_map.\n");
		goto error;
	}

	rc = of_property_read_u32(np, "input_bridge,map_key",\
			(unsigned int *)&option);
	if (rc) {
		dev_err(dev, "Unable to read %s\n", "input_bridge,map_key");
		goto error;
	}

	pdata->mmap = devm_kzalloc(dev,
			sizeof(struct sec_input_bridge_mmap)*pdata->num_map, GFP_KERNEL);
	if (!pdata->mmap) {
		dev_err(dev, "%s: Failed to allocate memory.\n", __func__);
		rc = -ENOMEM;
		goto error;
	}

	for (i = 0; i < pdata->num_map; i++) {
		rc = of_property_read_string_index(np, "input_bridge,map_codes", i, &out_prop);
		if (rc < 0) {
			dev_err(dev, "failed to get %d props string.\n", i);
			goto error;

		}
		prop = of_find_property(np, out_prop, NULL);
		if (!prop) {
			rc = -EINVAL;
			goto error;
		}
		map_size = prop->length / sizeof(unsigned int);
		pdata->mmap[i].num_mkey = map_size;
		map = devm_kzalloc(dev, sizeof(unsigned int)*map_size, GFP_KERNEL);
		if (!map) {
			dev_err(dev, "%s: Failed to allocate map memory.\n", __func__);
			rc = -ENOMEM;
			goto error;
		}

		rc = of_property_read_u32_array(np, out_prop, map, map_size);
		if (rc && (rc != -EINVAL)) {
			dev_err(dev, "Unable to read %s\n", out_prop);
			goto error;
		}
		pdata->mmap[i].mkey_map = devm_kzalloc(dev,
					sizeof(struct sec_input_bridge_mkey)*\
					map_size, GFP_KERNEL);
		if (!pdata->mmap[i].mkey_map) {
			dev_err(dev, "%s: Failed to allocate memory\n", __func__);
			rc = -ENOMEM;
			goto error;
		}

		for (j = 0; j < map_size; j++) {
			pdata->mmap[i].mkey_map[j].type = option;
			pdata->mmap[i].mkey_map[j].code = map[j];
		}

		rc = of_property_read_string_index(np, "input_bridge,env_str", i, &out_prop);
		if (rc < 0) {
			dev_err(dev, "failed to get %d props string\n", i);
			goto error;
		}
		pdata->mmap[i].uevent_env_str = (char *)out_prop;

		rc = of_property_read_string_index(np, "input_bridge,env_value", i, &out_prop);
		if (rc) {
			dev_err(dev, "failed to get %d props string\n", i);
			goto error;
		}
		pdata->mmap[i].uevent_env_value = (char *)out_prop;

		pdata->mmap[i].enable_uevent = (unsigned char)of_property_read_bool\
						(np, "input_bridge,enable_uevent");

		rc = of_property_read_u32(np, "input_bridge,uevent_action",\
					(u32 *)&pdata->mmap[i].uevent_action);
		if (rc) {
			dev_err(dev, "failed to get uevent_action.\n");
			goto error;
		}
	}

	rc = of_property_read_u32(np, "input_bridge,num_dev",\
			(unsigned int *)&pdata->num_dev);
	if (rc) {
		dev_err(dev, "failed to get num_dev.\n");
		goto error;
	}

	pdata->support_dev_name = devm_kzalloc(dev,
			sizeof(char*)*pdata->num_dev, GFP_KERNEL);
	if (!pdata->support_dev_name) {
		dev_err(dev, "%s: Failed to allocate memory."\
			"[support_dev_name]\n", __func__);
		rc = -ENOMEM;
		goto error;
	}

	for (i = 0; i < pdata->num_dev; i++) {
		rc = of_property_read_string_index(np, "input_bridge,dev_name_str", i, &out_prop);
		if (rc < 0) {
			dev_err(dev, "failed to get %d dev_name_str string\n", i);
			goto error;
		}
		pdata->support_dev_name[i] = (char *)out_prop;
	}

#ifdef DEBUG_BRIDGE
	for (i = 0; i < pdata->num_map; i++) {

		dev_info(dev, "%s: pdata->mmap[%d].num_mkey=[%d]\n",
				__func__, i, pdata->mmap[i].num_mkey);
		for(j = 0; j < pdata->mmap[i].num_mkey; j++) {
			dev_info(dev,
				"%s: pdata->mmap[%d].mkey_map[%d].type=[%d]\n",
				__func__, i, j, pdata->mmap[i].mkey_map[j].type);
			dev_info(dev,
				"%s: pdata->mmap[%d].mkey_map[%d].code=[%d]\n",
				__func__, i, j, pdata->mmap[i].mkey_map[j].code);
			dev_info(dev,
				"%s: pdata->mmap[%d].mkey_map[%d].option=[%d]\n",
				__func__, i, j, pdata->mmap[i].mkey_map[j].option);
		}
		dev_info(dev, "%s: pdata->mmap[%d].uevent_env_str=[%s]\n",
				__func__, i, pdata->mmap[i].uevent_env_str);
		dev_info(dev, "%s: pdata->mmap[%d].uevent_env_value=[%s]\n",
				__func__, i, pdata->mmap[i].uevent_env_value);
		dev_info(dev, "%s: pdata->mmap[%d].enable_uevent=[%d]\n",
				__func__, i, pdata->mmap[i].enable_uevent);
		dev_info(dev, "%s: pdata->mmap[%d].uevent_action=[%d]\n",
				__func__, i, pdata->mmap[i].uevent_action);
	}
	dev_info(dev, "%s: pdata->num_dev=[%d]\n", __func__, pdata->num_dev);
	for (i = 0; i < pdata->num_dev; i++)
		dev_info(dev, "%s: pdata->support_dev_name[%d] = [%s]\n",
			__func__, i, pdata->support_dev_name [i]);
#endif
	return 0;
error:
	return rc;
}

static struct of_device_id input_bridge_of_match[] = {
	{ .compatible = "samsung_input_bridge", },
	{ },
};
MODULE_DEVICE_TABLE(of, input_bridge_of_match);
#else
static int sec_input_bridge_parse_dt(struct device *dev,
			struct sec_input_bridge_platform_data *pdata)
{
	dev_err(dev, "%s\n", __func__);
	return -ENODEV;
}
#endif

static int sec_input_bridge_probe(struct platform_device *pdev)
{

	struct sec_input_bridge_platform_data *pdata;
	struct sec_input_bridge *bridge;
	struct input_device_id *input_bridge_ids;
	int state, i, j, k;
	int total_num_key = 0;

	if (pdev->dev.of_node) {
		pdata = devm_kzalloc(&pdev->dev,
			sizeof(struct sec_input_bridge_platform_data),
				GFP_KERNEL);
		if (!pdata) {
			dev_err(&pdev->dev, "Failed to allocate pdata memory\n");
			state = -ENOMEM;
			goto error_1;
		}
		state = sec_input_bridge_parse_dt(&pdev->dev, pdata);
		if (state)
			goto error_1;
	} else {
		pdata = pdev->dev.platform_data;
		if (!pdata) {
			dev_err(&pdev->dev, "Fail samsung input bridge platform data.\n");
			state = -ENOMEM;
			goto error_1;
		}
	}

	if (pdata->num_map == 0) {
		dev_err(&pdev->dev,
			"No samsung input bridge platform data. num_mkey is NULL.\n");
		state = -EINVAL;
		goto error_1;
	}

	bridge = kzalloc(sizeof(struct sec_input_bridge), GFP_KERNEL);
	if (!bridge) {
		dev_err(&pdev->dev, "Failed to allocate bridge memory\n");
		state = -ENOMEM;
		goto error_1;
	}

	bridge->send_uevent_flag = 0;
	bridge->safemode_flag = SAFEMODE_INIT;

	for (i = 0; i < pdata->num_map; i++)
		total_num_key += pdata->mmap[i].num_mkey;

	input_bridge_ids =
		kzalloc(sizeof(struct input_device_id[(total_num_key + 1)]),
			GFP_KERNEL);
	if (!input_bridge_ids) {
		dev_err(&pdev->dev, "Failed to allocate input_bridge_ids memory\n");
		state = -ENOMEM;
		goto error_2;
	}
	memset(input_bridge_ids, 0x00, sizeof(input_bridge_ids));

	for (i = 0, k = 0; i < pdata->num_map; i++) {
		for (j = 0; j < pdata->mmap[i].num_mkey; j++) {
			input_bridge_set_ids(&input_bridge_ids[k++],
					     pdata->mmap[i].mkey_map[j].type,
					     pdata->mmap[i].mkey_map[j].code);
		}
	}

	input_bridge_handler.private = bridge;
	input_bridge_handler.id_table = input_bridge_ids;

	state = input_register_handler(&input_bridge_handler);
	if (state) {
		dev_err(&pdev->dev, "Failed to register input_bridge_handler\n");
		goto error_3;
	}

	bridge->dev = pdev;
	bridge->pdata = pdata;
	platform_set_drvdata(pdev, bridge);

	setup_timer(&bridge->safemode_timer,
			input_bridge_safemode_timer,
			(unsigned long)bridge);
	INIT_WORK(&bridge->work, input_bridge_work);
	mutex_init(&bridge->lock);

	sec_input_bridge = device_create(sec_class, NULL, 0, NULL,
						"sec_input_bridge");
	if (IS_ERR(sec_input_bridge)) {
		dev_err(&pdev->dev, "%s: Failed to create device"\
				"(sec_input_bridge)!\n", __func__);
		goto error_3;
	}
	dev_set_drvdata(sec_input_bridge, bridge);

	return 0;

error_3:
	kfree(input_bridge_ids);
error_2:
	kfree(bridge);
error_1:
	return state;

}

static int sec_input_bridge_remove(struct platform_device *dev)
{
	struct sec_input_bridge *bridge = platform_get_drvdata(dev);

	cancel_work_sync(&bridge->work);
	mutex_destroy(&bridge->lock);
	kfree(input_bridge_handler.id_table);
	input_unregister_handler(&input_bridge_handler);
	kfree(bridge);
	platform_set_drvdata(dev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int sec_input_bridge_suspend(struct platform_device *dev,
				    pm_message_t state)
{
	return 0;
}

static int sec_input_bridge_resume(struct platform_device *dev)
{
	return 0;
}
#else
#define sec_input_bridge_suspend		NULL
#define sec_input_bridge_resume		NULL
#endif

static struct platform_driver sec_input_bridge_driver = {
	.probe = sec_input_bridge_probe,
	.remove = sec_input_bridge_remove,
	.suspend = sec_input_bridge_suspend,
	.resume = sec_input_bridge_resume,
	.driver = {
		   .name = "samsung_input_bridge",
#ifdef CONFIG_OF
		   .of_match_table = input_bridge_of_match,
#endif
		   },
};

static int __init sec_input_bridge_init(void)
{
	return platform_driver_register(&sec_input_bridge_driver);
}

static void __exit sec_input_bridge_exit(void)
{
	platform_driver_unregister(&sec_input_bridge_driver);
}

fs_initcall(sec_input_bridge_init);
module_exit(sec_input_bridge_exit);

MODULE_AUTHOR("SLP");
MODULE_DESCRIPTION("Input Event -> Specific Control Bridge");
MODULE_LICENSE("GPL");
