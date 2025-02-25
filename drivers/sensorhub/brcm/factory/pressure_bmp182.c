/*
 *  Copyright (C) 2012, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */
#include "../ssp.h"

#define LPS25H_REV	6
#define	VENDOR_BOSCH		"BOSCH"
#define	CHIP_ID_BOSCH		"BMP180"
#define	VENDOR_STM		"STM"
#define	CHIP_ID_LPS25H		"LPS25H"

#define CALIBRATION_FILE_PATH		"/efs/FactoryApp/baro_delta"

#define	PR_ABS_MAX	8388607		/* 24 bit 2'compl */
#define	PR_ABS_MIN	-8388608

/*************************************************************************/
/* factory Sysfs                                                         */
/*************************************************************************/

static ssize_t sea_level_pressure_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	sscanf(buf, "%d", &data->sealevelpressure);

	if (data->sealevelpressure == 0) {
		pr_info("%s, our->temperature = 0\n", __func__);
		data->sealevelpressure = -1;
	}

	pr_info("[SSP] %s sea_level_pressure = %d\n",
		__func__, data->sealevelpressure);
	return size;
}

int pressure_open_calibration(struct ssp_data *data)
{
	char chBuf[10] = {0,};
	int iErr = 0;
	mm_segment_t old_fs;
	struct file *cal_filp = NULL;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	cal_filp = filp_open(CALIBRATION_FILE_PATH, O_RDONLY, 0660);
	if (IS_ERR(cal_filp)) {
		iErr = PTR_ERR(cal_filp);
		if (iErr != -ENOENT)
			pr_err("[SSP]: %s - Can't open calibration file(%d)\n",
				__func__, iErr);
		set_fs(old_fs);
		return iErr;
	}
	iErr = cal_filp->f_op->read(cal_filp,
		chBuf, 10 * sizeof(char), &cal_filp->f_pos);
	if (iErr < 0) {
		pr_err("[SSP]: %s - Can't read the cal data from file (%d)\n",
			__func__, iErr);
		return iErr;
	}
	filp_close(cal_filp, current->files);
	set_fs(old_fs);

	iErr = kstrtoint(chBuf, 10, &data->iPressureCal);
	if (iErr < 0) {
		pr_err("[SSP]: %s - kstrtoint failed. %d\n", __func__, iErr);
		return iErr;
	}

	ssp_dbg("[SSP]: open barometer calibration %d\n", data->iPressureCal);

	if (data->iPressureCal < PR_ABS_MIN || data->iPressureCal > PR_ABS_MAX)
		pr_err("[SSP]: %s - wrong offset value!!!\n", __func__);

	return iErr;
}

static ssize_t pressure_cabratioin_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct ssp_data *data = dev_get_drvdata(dev);
	int iPressureCal = 0, iErr = 0;

	iErr = kstrtoint(buf, 10, &iPressureCal);
	if (iErr < 0) {
		pr_err("[SSP]: %s - kstrtoint failed.(%d)\n", __func__, iErr);
		return iErr;
	}

	if (iPressureCal < PR_ABS_MIN || iPressureCal > PR_ABS_MAX)
		return -EINVAL;

	data->iPressureCal = (s32)iPressureCal;

	return size;
}

static ssize_t pressure_cabratioin_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	pressure_open_calibration(data);

	return sprintf(buf, "%d\n", data->iPressureCal);
}

static ssize_t eeprom_check_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char chTempBuf  = 0;
	int iRet = 0;
	struct ssp_data *data = dev_get_drvdata(dev);

	struct ssp_msg *msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	msg->cmd = PRESSURE_FACTORY;
	msg->length = 1;
	msg->options = AP2HUB_READ;
	msg->buffer = &chTempBuf;
	msg->free_buffer = 0;

	iRet = ssp_spi_sync(data, msg, 3000);

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - Pressure Selftest Timeout!!\n", __func__);
		goto exit;
	}

	ssp_dbg("[SSP]: %s - %u\n", __func__, chTempBuf);

	exit:
	return snprintf(buf, PAGE_SIZE, "%d", chTempBuf);
}

/* sysfs for vendor & name */
static ssize_t pressure_vendor_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
#if defined(LPS25H_REV)
	struct ssp_data *data = dev_get_drvdata(dev);

	if(data->ap_rev >= LPS25H_REV)
		return sprintf(buf, "%s\n", VENDOR_STM);
	else
		return sprintf(buf, "%s\n", VENDOR_BOSCH);
#else
	return sprintf(buf, "%s\n", VENDOR_BOSCH);
#endif
}

static ssize_t pressure_name_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
#if defined (LPS25H_REV)
	struct ssp_data *data = dev_get_drvdata(dev);

	if(data->ap_rev >= LPS25H_REV)
		return sprintf(buf, "%s\n", CHIP_ID_LPS25H);
	else
		return sprintf(buf, "%s\n", CHIP_ID_BOSCH);
#else
	return sprintf(buf, "%s\n", CHIP_ID_BOSCH);
#endif
}

static DEVICE_ATTR(vendor,  S_IRUGO, pressure_vendor_show, NULL);
static DEVICE_ATTR(name,  S_IRUGO, pressure_name_show, NULL);
static DEVICE_ATTR(eeprom_check, S_IRUGO, eeprom_check_show, NULL);
static DEVICE_ATTR(calibration,  S_IRUGO | S_IWUSR | S_IWGRP,
	pressure_cabratioin_show, pressure_cabratioin_store);
static DEVICE_ATTR(sea_level_pressure, /*S_IRUGO |*/ S_IWUSR | S_IWGRP,
	NULL, sea_level_pressure_store);

static struct device_attribute *pressure_attrs[] = {
	&dev_attr_vendor,
	&dev_attr_name,
	&dev_attr_calibration,
	&dev_attr_sea_level_pressure,
	&dev_attr_eeprom_check,
	NULL,
};

#if defined (LPS25H_REV)
static struct device_attribute *pressure_attrs_lps25h[] = {
	&dev_attr_vendor,
	&dev_attr_name,
	&dev_attr_calibration,
	&dev_attr_sea_level_pressure,
	NULL,
};
#endif

void initialize_pressure_factorytest(struct ssp_data *data)
{
#if defined (LPS25H_REV)
	if(data->ap_rev >= LPS25H_REV)
		sensors_register(data->prs_device, data, pressure_attrs_lps25h,
			"barometer_sensor");
	else
		sensors_register(data->prs_device, data, pressure_attrs,
			"barometer_sensor");
#else
	sensors_register(data->prs_device, data, pressure_attrs,
		"barometer_sensor");
#endif
}

void remove_pressure_factorytest(struct ssp_data *data)
{
#if defined (LPS25H_REV)
	if(data->ap_rev >= LPS25H_REV)
		sensors_unregister(data->prs_device, pressure_attrs_lps25h);
	else
		sensors_unregister(data->prs_device, pressure_attrs);
#else
	sensors_unregister(data->prs_device, pressure_attrs);
#endif
}
