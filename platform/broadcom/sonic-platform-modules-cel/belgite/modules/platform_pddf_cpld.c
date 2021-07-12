/*
 * smc.c - The CPLD driver for E1031 System Management.
 * The driver implement sysfs to access CPLD register on the E1031 via LPC bus.
 * Copyright (C) 2018 Celestica Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <uapi/linux/stat.h>


#define DRIVER_NAME "pddf.cpld"

/**
 * CPLD register address for read and write.
 */
#define VERSION         0xA100
#define SCRATCH         0xA101

/* SEPERATE RESET
 * [7:5] RESERVED
 * [4]   RESET 10GPHY_LED 
 * [3]   RESET GPHY_LED  
 * [2]   RESET PCA9548_1 
 * [1]   RESET PCA9548_0
 * [0]   RESET I210 
 * 1: not reset, 0: reset
 */
#define SPR_RESET       0xA106

/* BCM56277_RESET 
 * [7:4] RESERVED
 * [3]   BCM56277 CHIP_RST_OUT 
 * [2]   RESET BCM56277 
 * [1]   RESET BCM56277_PE 
 * [0]   RESET BCM56277_IPROC 
 * 1: not reset, 0: reset
 */
#define BCM56277_RESET       0xA107

/* PSU STATUS
 * [7]  PSUR_ALERT
 * [6]  PSUL_ALERT
 * [5]  PSUR_PRS
 * [4]  PSUL_PRS
 * [3]  PSUR_PWOK
 * [2]  PSUL_PWOK
 * [1]  PSUR_ENB
 * [0]  PSUL_ENB
 */
#define PSU_STAT        0xA141
#define PSUR_ALERT      7
#define PSUL_ALERT      6
#define PSUR_PRS        5
#define PSUL_PRS        4
#define PSUR_PWOK       3
#define PSUL_PWOK       2
#define PSUR_ENB        1
#define PSUL_ENB        0

/* FAN LED CTRL
 * [7:3]  RESERVED
 * [2:0]  LED CTRL
 */
#define FAN_LED_1       0xA133
#define FAN_LED_2       0xA137
#define FAN_LED_3       0xA13B

enum FAN_LED {
    fan_led_grn = 0,
    fan_led_grn_bnk,
    fan_led_amb,
    fan_led_amb_bnk,
    fan_led_off
} fan_led;

#define FAN_3           2
#define FAN_2           1
#define FAN_1           0

/* SYSTEM LED
 * [7:6] RESERVED
 * [5:4] SYS LED Sel Control 
 * [3:2] Reserved 
 * [1:0] Sys Stat Blink Control 
 */
#define LED_SYS         0xA143

enum SYS_LED {
    sys_led_on = 0,
    sys_led_amb,
    sys_led_grn,
    sys_led_off
} sys_led;

enum LED_CTRL {
    led_off = 0,
    led_blk_1hz,
    led_blk_4hz,
    led_on
} led_ctrl;

#define LED_OFF        "off"
#define LED_GREEN      "green"
#define LED_AMBER      "amber"
#define LED_HZ_GBNK    "grn_bnk_1hz"
#define LED_HZ_ABNK    "amb_bnk_1hz"
#define LED_QHZ_GBNK   "grn_bnk_4hz"
#define LED_QHZ_ABNK   "amb_bnk_4hz"
#define LED_HZ_GABNK   "grn_amb_1hz"
#define LED_QHZ_GABNK  "grn_amb_4hz"


/* ALARM LED
 * [7:6] RESERVED
 * [5:4] ALARM LED Sel Control
 * [3:2] Reserved
 * [1:0] Alarm Stat Blink Control
 */
#define LED_ALARM         0xA144

enum ALARM_LED {
    alarm_led_off = 0,
    alarm_led_grn,
    alarm_led_amb
} alarm_led;

#define LED_PWR 	 0xA142

enum PWR_LED {
    pwr_led_off = 0,
    pwr_led_grn,
    pwr_led_amb
} pwr_led;

/* SFP PORT INT TRIGGER MODE
 * [7:6] RESERVED
 * [5:4] RXLOS 
 * [3:2] MODABS
 * [1:0] TXFAULT
 * 00: falling edge,
 * 01: rising edge,
 * 10: Both edges,
 * 11: low level detect
 */
#define PRT_MDSL_TXFLT       0xA147
#define PRT_INT_RXLOS        0xA148
#define PRT_PRSNT_MODABS     0xA149
#define PRT_LPMOD_TXDIS      0xA14A

//#define TXFAULT_TRIG    0
//#define MODABS_TRIG     2
//#define RXLOS_TRIG      4


/* SFP PORT STATUS
 * [7:4] RESERVED
 * [3:0] TX_FAULT / MODABS / RXLOS
 */
//#define SFP_TXFAULT     0x0242
//#define SFP_MODABS      0x0243
//#define SFP_RXLOS       0x0244

/* SFP PORT INTERRUPT
 * [7:4] RESERVED
 * [3:0] TX_FAULT / MODABS / RXLOS
 * 1: int, 0: no int
 */
//#define TXFAULT_INT     0x0246
//#define MODABS_INT      0x0247
//#define RXLOS_INT       0x0248

/* INTERRUPT MASK REGISTER
 * [7:4] RESERVED
 * [3:0] TX_FAULT / MODABS / RXLOS
 * 1: mask, 0: not mask
 */
//#define TXFAULT_MSK     0x024A
//#define MODABS_MSK      0x024B
//#define RXLOS_MSK       0x024C

/* SFP PORT CTRL
 * [7:4] RATE SEL (RS0/RS1)
 * [3:0] TX_DIS
 */
//#define SFP_TXCTRL      0x0255

struct pddf_cpld_data {
    struct mutex       cpld_lock;
    uint16_t           read_addr;
    struct device      *fpp_node;
    struct device      *sfp_devices[4];
};

struct sfp_device_data {
    int portid;
};

struct class *celplatform;
struct pddf_cpld_data *cpld_data;

struct index_device_attribute {
    struct device_attribute dev_attr;
    int index;
};

static ssize_t scratch_show(struct device *dev, struct device_attribute *devattr,
                            char *buf)
{
    unsigned char data = 0;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(SCRATCH);
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "0x%2.2x\n", data);
}

static ssize_t scratch_store(struct device *dev, struct device_attribute *devattr,
                             const char *buf, size_t count)
{
    unsigned long data;
    char *last;

    mutex_lock(&cpld_data->cpld_lock);
    data = (uint16_t)strtoul(buf, &last, 16);
    if (data == 0 && buf == last) {
        mutex_unlock(&cpld_data->cpld_lock);
        return -EINVAL;
    }
    outb(data, SCRATCH);
    mutex_unlock(&cpld_data->cpld_lock);
    return count;
}


static ssize_t version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int len = 0;
    mutex_lock(&cpld_data->cpld_lock);
    len = sprintf(buf, "0x%2.2x\n", inb(VERSION));
    mutex_unlock(&cpld_data->cpld_lock);
    return len;
}

static ssize_t getreg_store(struct device *dev, struct device_attribute *devattr,
                            const char *buf, size_t count)
{
    uint16_t addr;
    char *last;

    addr = (uint16_t)strtoul(buf, &last, 16);
    if (addr == 0 && buf == last) {
        return -EINVAL;
    }
    cpld_data->read_addr = addr;
    return count;
}

static ssize_t getreg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int len = 0;
    mutex_lock(&cpld_data->cpld_lock);
    len = sprintf(buf, "0x%2.2x\n", inb(cpld_data->read_addr));
    mutex_unlock(&cpld_data->cpld_lock);
    return len;
}

static ssize_t setreg_store(struct device *dev, struct device_attribute *devattr,
                            const char *buf, size_t count)
{
    uint16_t addr;
    uint8_t value;
    char *tok;
    char clone[count];
    char *pclone = clone;
    char *last;

    strcpy(clone, buf);

    mutex_lock(&cpld_data->cpld_lock);
    tok = strsep((char**)&pclone, " ");
    if (tok == NULL) {
        mutex_unlock(&cpld_data->cpld_lock);
        return -EINVAL;
    }
    addr = (uint16_t)strtoul(tok, &last, 16);
    if (addr == 0 && tok == last) {
        mutex_unlock(&cpld_data->cpld_lock);
        return -EINVAL;
    }

    tok = strsep((char**)&pclone, " ");
    if (tok == NULL) {
        mutex_unlock(&cpld_data->cpld_lock);
        return -EINVAL;
    }
    value = (uint8_t)strtoul(tok, &last, 16);
    if (value == 0 && tok == last) {
        mutex_unlock(&cpld_data->cpld_lock);
        return -EINVAL;
    }

    outb(value, addr);
    mutex_unlock(&cpld_data->cpld_lock);
    return count;
}

/**
 * @brief          Show status led
 * @param  dev     kernel device
 * @param  devattr kernel device attribute
 * @param  buf     buffer for get value
 * @return         led state - off/on/blink
 */
static ssize_t sys_led_show(struct device *dev, struct device_attribute *devattr,
                               char *buf)
{
    unsigned char data = 0;
    unsigned char color = 0;
    unsigned char control = 0;

    mutex_lock(&cpld_data->cpld_lock);
    data = inb(LED_SYS);
    mutex_unlock(&cpld_data->cpld_lock);
    color = (data & 0x30) >> 4;
    control = (data & 0x3);

    switch(color){
        case sys_led_on:
	   if ( control == led_blk_1hz){
	  	return sprintf(buf, "%s\n", LED_HZ_GABNK); 
	   }else if(control == led_blk_4hz){
	        return sprintf(buf, "%s\n", LED_QHZ_GABNK);
	   }else{
	        break;
	   }
	case sys_led_amb:
	    if ( control == led_blk_1hz){
                return sprintf(buf, "%s\n", LED_HZ_ABNK);
           }else if(control == led_blk_4hz){
                return sprintf(buf, "%s\n", LED_QHZ_ABNK);
           }else if(control == led_on){
		return sprintf(buf, "%s\n", LED_AMBER);
	   }else{
                break;
           }
	case sys_led_grn:
	      if ( control == led_blk_1hz){
                return sprintf(buf, "%s\n", LED_HZ_GBNK);
           }else if(control == led_blk_4hz){
                return sprintf(buf, "%s\n", LED_QHZ_GBNK);
           }else if(control == led_on){
	   	return sprintf(buf, "%s\n", LED_GREEN);
	   }else{
                break;
           }
	default:
	      break;
    }

    return sprintf(buf, "%s\n", LED_OFF); 
}

/**
 * @brief          Set the status led
 * @param  dev     kernel device
 * @param  devattr kernel device attribute
 * @param  buf     buffer of set value - off/on/blink
 * @param  count   number of bytes in buffer
 * @return         number of bytes written, or error code < 0.
 */
static ssize_t sys_led_store(struct device *dev, struct device_attribute *devattr,
                                const char *buf, size_t count)
{
    unsigned char data;


    if(sysfs_streq(buf, LED_OFF)){
	data = sys_led_off << 4;
	data += led_off; 
    }else if(sysfs_streq(buf, LED_GREEN)){
        data = sys_led_grn << 4;
        data += led_on;
    }else if(sysfs_streq(buf, LED_AMBER)){
    	data = sys_led_amb << 4;
        data += led_on;
    }else if(sysfs_streq(buf, LED_HZ_GBNK)){
        data = sys_led_grn << 4;
        data += led_blk_1hz;
    }else if(sysfs_streq(buf, LED_HZ_ABNK)){
    data = sys_led_amb << 4;
            data += led_blk_1hz; 
    }else if(sysfs_streq(buf, LED_QHZ_GBNK)){
   	 data = sys_led_grn << 4;
            data += led_blk_4hz; 
    }else if(sysfs_streq(buf, LED_QHZ_ABNK)){
     data = sys_led_amb << 4;
            data += led_blk_4hz;

    }else if(sysfs_streq(buf, LED_HZ_GABNK)){
     data = sys_led_on << 4;
            data += led_blk_1hz;
    }else if(sysfs_streq(buf, LED_QHZ_GABNK)){
	  data = sys_led_on<< 4;
            data += led_blk_4hz;
    }else{
	    count = -EINVAL; 
    }
 
    mutex_lock(&cpld_data->cpld_lock);
    outb(data, LED_SYS);
    mutex_unlock(&cpld_data->cpld_lock);
    return count;
}

/**
 * @brief          Show master led
 * @param  dev     kernel device
 * @param  devattr kernel device attribute
 * @param  buf     buffer for get value
 * @return         led state - off/green/amber
 */
static ssize_t alarm_led_show(struct device *dev, struct device_attribute *devattr,
                               char *buf)
{
    unsigned char data = 0;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(LED_ALARM);
    mutex_unlock(&cpld_data->cpld_lock);
    data = data & 0x3;
    return sprintf(buf, "%s\n",
                   data == alarm_led_grn ? "on" : data == alarm_led_amb ? "amber" : "off");
}

/**
 * @brief          Set the master led
 * @param  dev     kernel device
 * @param  devattr kernel device attribute
 * @param  buf     buffer of set value - off/green/amber
 * @param  count   number of bytes in buffer
 * @return         number of bytes written, or error code < 0.
 */
static ssize_t alarm_led_store(struct device *dev, struct device_attribute *devattr,
                                const char *buf, size_t count)
{
    unsigned char led_status, data;

    if (sysfs_streq(buf, "off")) {
        led_status = alarm_led_off;
    } else if (sysfs_streq(buf, "green")) {
        led_status = alarm_led_grn;
    } else if (sysfs_streq(buf, "amber")) {
        led_status = alarm_led_amb;
    } else {
        count = -EINVAL;
        return count;
    }
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(LED_ALARM);
    data = data & ~(0x3);
    data = data | led_status;
    outb(data, LED_ALARM);
    mutex_unlock(&cpld_data->cpld_lock);
    return count;
}

static ssize_t pwr_led_show(struct device *dev, struct device_attribute *devattr,
                               char *buf)
{
    unsigned char data = 0;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(LED_ALARM);
    mutex_unlock(&cpld_data->cpld_lock);
    data = data & 0x3;
    return sprintf(buf, "%s\n",
                   data == alarm_led_grn ? "on" : data == alarm_led_amb ? "amber" : "off");
}


static ssize_t pwr_led_store(struct device *dev, struct device_attribute *devattr,
                                const char *buf, size_t count)
{
    unsigned char led_status, data;

    if (sysfs_streq(buf, "off")) {
        led_status = alarm_led_off;
    } else if (sysfs_streq(buf, "green")) {
        led_status = alarm_led_grn;
    } else if (sysfs_streq(buf, "amber")) {
        led_status = alarm_led_amb;
    } else {
        count = -EINVAL;
        return count;
    }
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(LED_ALARM);
    data = data & ~(0x3);
    data = data | led_status;
    outb(data, LED_ALARM);
    mutex_unlock(&cpld_data->cpld_lock);
    return count;
}

static ssize_t psuL_prs_show(struct device *dev, struct device_attribute *devattr,
                             char *buf)
{
    unsigned char data = 0;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PSU_STAT);
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "%d\n", (data >> PSUL_PRS) & 1U);
}

static ssize_t psuR_prs_show(struct device *dev, struct device_attribute *devattr,
                             char *buf)
{
    unsigned char data = 0;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PSU_STAT);
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "%d\n", (data >> PSUR_PRS) & 1U);
}
static DEVICE_ATTR_RO(psuR_prs);

static ssize_t psuL_status_show(struct device *dev, struct device_attribute *devattr,
                                char *buf)
{
    unsigned char data = 0;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PSU_STAT);
    mutex_unlock(&cpld_data->cpld_lock);
    data = ( data >> PSUL_PWOK ) & 0x1;
    return sprintf(buf, "%d\n", data );
}

static ssize_t psuR_status_show(struct device *dev, struct device_attribute *devattr,
                                char *buf)
{
    unsigned char data = 0;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PSU_STAT);
    mutex_unlock(&cpld_data->cpld_lock);
    data = ( data >> PSUR_PWOK ) & 0x1;
    return sprintf(buf, "%d\n", data);
}


static ssize_t psuL_enb_show(struct device *dev, struct device_attribute *devattr,
                            char *buf)
{
    unsigned char data = 0;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PSU_STAT);
    mutex_unlock(&cpld_data->cpld_lock);
    data = ( data >> PSUL_ENB ) & 1U;
    return sprintf(buf, "%d\n", data);
}

static ssize_t psuL_enb_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    long value;
    ssize_t status;
    unsigned char data;

    mutex_lock(&cpld_data->cpld_lock);
    status = kstrtol(buf, 0, &value);
    value = (value & 0x01) << PSUL_ENB;
    if (status == 0) {
        data = inb(PSU_STAT);
        data = data | value;
        outb(data, PSU_STAT);
        status = size;
    }
    mutex_unlock(&cpld_data->cpld_lock);
    return status;
}

static ssize_t psuR_enb_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    long value;
    ssize_t status;
    unsigned char data;

    mutex_lock(&cpld_data->cpld_lock);
    status = kstrtol(buf, 0, &value);
    value = (value & 0x01) << PSUR_ENB;
    if (status == 0) {
        data = inb(PSU_STAT);
        data = data | value;
        outb(data, PSU_STAT);
        status = size;
    }
    mutex_unlock(&cpld_data->cpld_lock);
    return status;
}

static ssize_t psuR_enb_show(struct device *dev, struct device_attribute *devattr,
                            char *buf)
{
    unsigned char data = 0;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PSU_STAT);
    mutex_unlock(&cpld_data->cpld_lock);
    data = ( data >> PSUR_ENB ) & 1U;
    return sprintf(buf, "%d\n", data);
}

static ssize_t sfp_txfault_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct sensor_device_attribute *sa = to_sensor_dev_attr(attr);
    int index = sa->index;
    unsigned char data;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PRT_MDSL_TXFLT);
    data = ((data & 0xFF) >> (index -1)) & 0x1;
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "%d\n", data);
}

static ssize_t sfp_modabs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct sensor_device_attribute *sa = to_sensor_dev_attr(attr);
    int index = sa->index;
    unsigned char data;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PRT_PRSNT_MODABS);
    data = ((data & 0xFF) >> (index -1)) & 0x1; 
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "%d\n", data);
}

static ssize_t sfp_rxlos_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct sensor_device_attribute *sa = to_sensor_dev_attr(attr);
    int index = sa->index;
    unsigned char data;
 
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PRT_INT_RXLOS);
    data = ((data & 0xFF) >> (index -1)) & 0x1; 
    mutex_unlock(&cpld_data->cpld_lock);

    return sprintf(buf, "%d\n", data);
}

static ssize_t sfp_txdis_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct sensor_device_attribute *sa = to_sensor_dev_attr(attr);
    int index = sa->index;
    unsigned char data;
    
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PRT_LPMOD_TXDIS);
    data = ((data & 0xFF) >> (index -1)) & 0x1; 
    mutex_unlock(&cpld_data->cpld_lock);

    return sprintf(buf, "%d\n", data);
}

static ssize_t sfp_txdis_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    struct sensor_device_attribute *sa = to_sensor_dev_attr(attr);
    int index = sa->index;
    long value;
    ssize_t status;
    unsigned char data;

    mutex_lock(&cpld_data->cpld_lock);
    status = kstrtol(buf, 0, &value);
    if (status == 0) {
        if(0 == value){
	    data = inb(PRT_LPMOD_TXDIS);
            data &= (0xFF &(~(0x1 << (index -1))));
            outb(data, PRT_LPMOD_TXDIS);
	    status = size;
	}else if(1 == value){
	    data = inb(PRT_LPMOD_TXDIS);
            data |= (0x1 << (index -1));
            outb(data, PRT_LPMOD_TXDIS);
            status = size;
        }else{
	    status = -EINVAL;
        }
    }

    mutex_unlock(&cpld_data->cpld_lock);
    return status;
}

/*static ssize_t sfp_rs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    unsigned char data;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(SFP_TXCTRL) >> 4;
    data = data & 0x0F;
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "0x%x\n", data);
}

static ssize_t sfp_rs_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    long value;
    ssize_t status;
    unsigned char data;

    mutex_lock(&cpld_data->cpld_lock);
    status = kstrtol(buf, 0, &value);
    value = (value & 0x0F) << 4;
    if (status == 0) {
        data = inb(SFP_TXCTRL);
        data = data & ~(0xF0);
        data = data | value;
        outb(data, SFP_TXCTRL);
        status = size;
    }
    mutex_unlock(&cpld_data->cpld_lock);
    return status;
}*/


/**
 * @brief      Show the avaliable interrupt trigger mode.
 *             "none" means the interrupt is masked.
 *
 * @return     Current trigger mode.
 */
/*static ssize_t txfault_trig_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    unsigned char mode;
    char *mode_str[5] = {"falling", "rising", "both", "low"};

    mutex_lock(&cpld_data->cpld_lock);
    mode = inb(PRT_MDSL_TXFLT) >> TXFAULT_TRIG;
    mode = mode & 0x3;
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "%s\n", mode_str[mode]);
}
*/

/**
 * @brief      Set the trigger mode of each interrupt type.
 *             Only one trigger mode allow in a type.
 *
 * @param      buf   The trigger mode of follwings 
 *                   "falling", "rising", "both"
 */
/*static ssize_t txfault_trig_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    ssize_t status;
    unsigned char data, trig_mode;

    if (sysfs_streq(buf, "falling")) {
        trig_mode = 0;
    } else if (sysfs_streq(buf, "rising")) {
        trig_mode = 1;
    } else if (sysfs_streq(buf, "both")) {
        trig_mode = 2;
    } else if (sysfs_streq(buf, "low")) {
        trig_mode = 3;
    } else {
        status = -EINVAL;
        return status;
    }

    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PRT_MDSL_TXFLT);
    data = data & ~(0x03 << TXFAULT_TRIG);
    data = data | trig_mode << TXFAULT_TRIG;
    outb(data, PRT_MDSL_TXFLT);
    mutex_unlock(&cpld_data->cpld_lock);
    status = size;
    return status;
}
*/

/**
 * @brief      Show the avaliable interrupt trigger mode.
 *             "none" means the interrupt is masked.
 *
 * @return     Current trigger mode.
 */
/*static ssize_t modabs_trig_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    unsigned char mode;
    char *mode_str[5] = {"falling", "rising", "both", "low"};

    mutex_lock(&cpld_data->cpld_lock);
    mode = inb(PRT_PRSNT_MODABS) >> MODABS_TRIG;
    mode = mode & 0x3;
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "%s\n", mode_str[mode]);
}
*/


/**
 * @brief      Set the trigger mode of each interrupt type.
 *             Only one trigger mode allow in a type.
 *
 * @param      buf   The trigger mode of follwings 
 *                   "falling", "rising", "both", "low"            
 */
/*static ssize_t modabs_trig_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    ssize_t status;
    unsigned char data, trig_mode;

    if (sysfs_streq(buf, "falling")) {
        trig_mode = 0;
    } else if (sysfs_streq(buf, "rising")) {
        trig_mode = 1;
    } else if (sysfs_streq(buf, "both")) {
        trig_mode = 2;
    } else if (sysfs_streq(buf, "low")) {
        trig_mode = 3;
    } else {
        status = -EINVAL;
        return status;
    }

    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PRT_PRSNT_MODABS);
    data = data & ~(0x03 << MODABS_TRIG);
    data = data | trig_mode << MODABS_TRIG;
    outb(data, PRT_PRSNT_MODABS);
    mutex_unlock(&cpld_data->cpld_lock);
    status = size;
    return status;
}
*/
/**
 * @brief      Show the avaliable interrupt trigger mode.
 *             "none" means the interrupt is masked.
 *
 * @return     Current trigger mode.
 */
/*static ssize_t rxlos_trig_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    unsigned char mode;
    char *mode_str[5] = {"falling", "rising", "both", "low"};

    mutex_lock(&cpld_data->cpld_lock);
    mode = inb(PRT_INT_RXLOS) >> RXLOS_TRIG;
    mode = mode & 0x3;
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "%s\n", mode_str[mode]);
}
*/

/**
 * @brief      Set the trigger mode of each interrupt type.
 *             Only one trigger mode allow in a type.
 *
 * @param      buf   The trigger mode of follwings 
 *                   "falling", "rising", "both", "low"
 */
/*static ssize_t rxlos_trig_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    ssize_t status;
    unsigned char data, trig_mode;

    if (sysfs_streq(buf, "falling")) {
        trig_mode = 0;
    } else if (sysfs_streq(buf, "rising")) {
        trig_mode = 1;
    } else if (sysfs_streq(buf, "both")) {
        trig_mode = 2;
    } else if (sysfs_streq(buf, "low")) {
        trig_mode = 3;
    } else {
        status = -EINVAL;
        return status;
    }

    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PRT_INT_RXLOS);
    data = data & ~(0x03 << RXLOS_TRIG);
    data = data | trig_mode << RXLOS_TRIG;
    outb(data, PRT_INT_RXLOS);
    mutex_unlock(&cpld_data->cpld_lock);
    status = size;
    return status;
}
*/

/*static ssize_t txfault_int_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    unsigned char data;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PRT_MDSL_TXFLT);
    data = data & 0x0F;
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "0x%x\n", data);
}

static ssize_t modabs_int_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    unsigned char data;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PRT_PRSNT_MODABS);
    data = data & 0x0F;
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "0x%x\n", data);
}

static ssize_t rxlos_int_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    unsigned char data;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(PRT_INT_RXLOS);
    data = data & 0x0F;
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "0x%x\n", data);
}
*/

/*static ssize_t txfault_mask_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    unsigned char data;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(TXFAULT_MSK);
    data = data & 0x0F;
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "0x%x\n", data);
}

static ssize_t txfault_mask_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    long value;
    ssize_t status;

    status = kstrtol(buf, 0, &value);
    value = value & 0x0F;
    if (status == 0) {
        mutex_lock(&cpld_data->cpld_lock);
        outb(value, TXFAULT_MSK);
        mutex_unlock(&cpld_data->cpld_lock);
        status = size;
    }
    return status;
}

static ssize_t modabs_mask_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    unsigned char data;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(MODABS_MSK);
    data = data & 0x0F;
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "0x%x\n", data);
}

static ssize_t modabs_mask_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    long value;
    ssize_t status;

    status = kstrtol(buf, 0, &value);
    value = value & 0x0F;
    if (status == 0) {
        mutex_lock(&cpld_data->cpld_lock);
        outb(value, MODABS_MSK);
        mutex_unlock(&cpld_data->cpld_lock);
        status = size;
    }
    return status;
}

static ssize_t rxlos_mask_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    unsigned char data;
    mutex_lock(&cpld_data->cpld_lock);
    data = inb(RXLOS_MSK);
    data = data & 0x0F;
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "0x%x\n", data);
}

static ssize_t rxlos_mask_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    long value;
    ssize_t status;

    status = kstrtol(buf, 0, &value);
    value = value & 0x0F;
    if (status == 0) {
        mutex_lock(&cpld_data->cpld_lock);
        outb(value, RXLOS_MSK);
        mutex_unlock(&cpld_data->cpld_lock);
        status = size;
    }
    return status;
}
*/

/*static ssize_t fan_led_show(struct device *dev, struct device_attribute *devattr,
                            char *buf)
{
    struct sensor_device_attribute *sa = to_sensor_dev_attr(devattr);
    int index = sa->index;
    unsigned char data = 0;
    char *led_str[5] = {"green", "green-blink", "amber", "amber-blink", "off"};

    mutex_lock(&cpld_data->cpld_lock);
    data = inb(FAN_LED_1 + index);
    data = data & 0x7;
    mutex_unlock(&cpld_data->cpld_lock);
    return sprintf(buf, "%s\n", led_str[data]);
}*/

/*static ssize_t fan_led_store(struct device *dev, struct device_attribute *devattr,
                             const char *buf, size_t count)
{
    struct sensor_device_attribute *sa = to_sensor_dev_attr(devattr);
    int index = sa->index;
    unsigned char led_status = 0;

    if (sysfs_streq(buf, "off")) {
        led_status = fan_led_off;
    } else if (sysfs_streq(buf, "green")) {
        led_status = fan_led_grn;
    } else if (sysfs_streq(buf, "amber")) {
        led_status = fan_led_amb;
    } else if (sysfs_streq(buf, "green-blink")) {
        led_status = fan_led_grn_bnk;
    } else if (sysfs_streq(buf, "amber-blink")) {
        led_status = fan_led_amb_bnk;
    } else {
        count = -EINVAL;
        return count;
    }
    mutex_lock(&cpld_data->cpld_lock);
    outb(led_status, FAN_LED_1 + index);
    mutex_unlock(&cpld_data->cpld_lock);
    return count;
}
*/
#define SFP_1 1
#define SFP_2 2
#define SFP_3 3
#define SFP_4 4
#define SFP_5 5
#define SFP_6 6
#define SFP_7 7
#define SFP_8 8

static DEVICE_ATTR_RO(version);
static DEVICE_ATTR_RW(scratch);
static DEVICE_ATTR_RW(getreg);
static DEVICE_ATTR_WO(setreg);
static DEVICE_ATTR_RW(sys_led);
static DEVICE_ATTR_RW(alarm_led);
static DEVICE_ATTR_RW(pwr_led);
static DEVICE_ATTR_RO(psuL_prs);
static DEVICE_ATTR_RO(psuL_status);
static DEVICE_ATTR_RO(psuR_status);
static DEVICE_ATTR_RW(psuL_enb);
static DEVICE_ATTR_RW(psuR_enb);

#define DECLAIRE_SFP_TXFAULT_SENSOR_DEVICE_ATTR(index) \
	static SENSOR_DEVICE_ATTR(sfp##index##_txfault,S_IRUGO,  sfp_txfault_show, NULL, SFP_##index);

#define DECLAIRE_SFP_MODABS_SENSOR_DEVICE_ATTR(index) \
	static SENSOR_DEVICE_ATTR(sfp##index##_modabs, S_IRUGO,  sfp_modabs_show, NULL, SFP_##index);

#define DECLAIRE_SFP_RXLOS_SENSOR_DEVICE_ATTR(index) \
	static SENSOR_DEVICE_ATTR(sfp##index##_rxlos, S_IRUGO, sfp_rxlos_show, NULL, SFP_##index);

#define DECLAIRE_SFP_TXDIS_SENSOR_DEVICE_ATTR(index) \
	static SENSOR_DEVICE_ATTR(sfp##index##_txdis,  S_IWUSR | S_IRUGO, sfp_txdis_show, sfp_txdis_store, SFP_##index);

//static DEVICE_ATTR_RW(txfault_trig);
//static DEVICE_ATTR_RW(modabs_trig);
//static DEVICE_ATTR_RW(rxlos_trig);
//static DEVICE_ATTR_RO(txfault_int);
//static DEVICE_ATTR_RO(modabs_int);
//static DEVICE_ATTR_RO(rxlos_int);
//static DEVICE_ATTR_RW(txfault_mask);
//static DEVICE_ATTR_RW(modabs_mask);
//static DEVICE_ATTR_RW(rxlos_mask);
DECLAIRE_SFP_TXFAULT_SENSOR_DEVICE_ATTR(1); 
DECLAIRE_SFP_TXFAULT_SENSOR_DEVICE_ATTR(2); 
DECLAIRE_SFP_TXFAULT_SENSOR_DEVICE_ATTR(3); 
DECLAIRE_SFP_TXFAULT_SENSOR_DEVICE_ATTR(4); 
DECLAIRE_SFP_TXFAULT_SENSOR_DEVICE_ATTR(5); 
DECLAIRE_SFP_TXFAULT_SENSOR_DEVICE_ATTR(6); 
DECLAIRE_SFP_TXFAULT_SENSOR_DEVICE_ATTR(7); 
DECLAIRE_SFP_TXFAULT_SENSOR_DEVICE_ATTR(8); 

DECLAIRE_SFP_MODABS_SENSOR_DEVICE_ATTR(1);
DECLAIRE_SFP_MODABS_SENSOR_DEVICE_ATTR(2);
DECLAIRE_SFP_MODABS_SENSOR_DEVICE_ATTR(3);
DECLAIRE_SFP_MODABS_SENSOR_DEVICE_ATTR(4);
DECLAIRE_SFP_MODABS_SENSOR_DEVICE_ATTR(5);
DECLAIRE_SFP_MODABS_SENSOR_DEVICE_ATTR(6);
DECLAIRE_SFP_MODABS_SENSOR_DEVICE_ATTR(7);
DECLAIRE_SFP_MODABS_SENSOR_DEVICE_ATTR(8);

DECLAIRE_SFP_RXLOS_SENSOR_DEVICE_ATTR(1);
DECLAIRE_SFP_RXLOS_SENSOR_DEVICE_ATTR(2);
DECLAIRE_SFP_RXLOS_SENSOR_DEVICE_ATTR(3);
DECLAIRE_SFP_RXLOS_SENSOR_DEVICE_ATTR(4);
DECLAIRE_SFP_RXLOS_SENSOR_DEVICE_ATTR(5);
DECLAIRE_SFP_RXLOS_SENSOR_DEVICE_ATTR(6);
DECLAIRE_SFP_RXLOS_SENSOR_DEVICE_ATTR(7);
DECLAIRE_SFP_RXLOS_SENSOR_DEVICE_ATTR(8);

DECLAIRE_SFP_TXDIS_SENSOR_DEVICE_ATTR(1);
DECLAIRE_SFP_TXDIS_SENSOR_DEVICE_ATTR(2);
DECLAIRE_SFP_TXDIS_SENSOR_DEVICE_ATTR(3);
DECLAIRE_SFP_TXDIS_SENSOR_DEVICE_ATTR(4);
DECLAIRE_SFP_TXDIS_SENSOR_DEVICE_ATTR(5);
DECLAIRE_SFP_TXDIS_SENSOR_DEVICE_ATTR(6);
DECLAIRE_SFP_TXDIS_SENSOR_DEVICE_ATTR(7);
DECLAIRE_SFP_TXDIS_SENSOR_DEVICE_ATTR(8);


#define DECLAIRE_SFP_TXFAULT_ATTR(index)    &sensor_dev_attr_sfp##index##_txfault.dev_attr.attr
#define DECLAIRE_SFP_MODABS_ATTR(index)     &sensor_dev_attr_sfp##index##_modabs.dev_attr.attr
#define DECLAIRE_SFP_RXLOS_ATTR(index) 	    &sensor_dev_attr_sfp##index##_rxlos.dev_attr.attr
#define DECLAIRE_SFP_TXDIS_ATTR(index)	    &sensor_dev_attr_sfp##index##_txdis.dev_attr.attr

static struct attribute *cpld_attrs[] = {
    &dev_attr_version.attr,
    &dev_attr_scratch.attr,
    &dev_attr_getreg.attr,
    &dev_attr_setreg.attr,
    // LEDs
    &dev_attr_sys_led.attr,
    &dev_attr_alarm_led.attr,
    &dev_attr_pwr_led.attr,
    // PSUs
    &dev_attr_psuL_prs.attr,
    &dev_attr_psuR_prs.attr,
    &dev_attr_psuL_status.attr,
    &dev_attr_psuR_status.attr,
    &dev_attr_psuL_enb.attr,
    &dev_attr_psuR_enb.attr,

    NULL,
};

static struct attribute_group cpld_group = {
    .attrs = cpld_attrs,
};

static struct attribute *sfp_attrs[] = {
    // SFP
    DECLAIRE_SFP_TXFAULT_ATTR(1),
    DECLAIRE_SFP_TXFAULT_ATTR(2),
    DECLAIRE_SFP_TXFAULT_ATTR(3),
    DECLAIRE_SFP_TXFAULT_ATTR(4),
    DECLAIRE_SFP_TXFAULT_ATTR(5),
    DECLAIRE_SFP_TXFAULT_ATTR(6),
    DECLAIRE_SFP_TXFAULT_ATTR(7),
    DECLAIRE_SFP_TXFAULT_ATTR(8),
    DECLAIRE_SFP_MODABS_ATTR(1), 
    DECLAIRE_SFP_MODABS_ATTR(2), 
    DECLAIRE_SFP_MODABS_ATTR(3), 
    DECLAIRE_SFP_MODABS_ATTR(4), 
    DECLAIRE_SFP_MODABS_ATTR(5), 
    DECLAIRE_SFP_MODABS_ATTR(6), 
    DECLAIRE_SFP_MODABS_ATTR(7), 
    DECLAIRE_SFP_MODABS_ATTR(8), 
    DECLAIRE_SFP_RXLOS_ATTR(1),
    DECLAIRE_SFP_RXLOS_ATTR(2),
    DECLAIRE_SFP_RXLOS_ATTR(3),
    DECLAIRE_SFP_RXLOS_ATTR(4),
    DECLAIRE_SFP_RXLOS_ATTR(5),
    DECLAIRE_SFP_RXLOS_ATTR(6),
    DECLAIRE_SFP_RXLOS_ATTR(7),
    DECLAIRE_SFP_RXLOS_ATTR(8),
    DECLAIRE_SFP_TXDIS_ATTR(1),
    DECLAIRE_SFP_TXDIS_ATTR(2),
    DECLAIRE_SFP_TXDIS_ATTR(3),
    DECLAIRE_SFP_TXDIS_ATTR(4),
    DECLAIRE_SFP_TXDIS_ATTR(5),
    DECLAIRE_SFP_TXDIS_ATTR(6),
    DECLAIRE_SFP_TXDIS_ATTR(7),
    DECLAIRE_SFP_TXDIS_ATTR(8),
//    &dev_attr_txfault_trig.attr,
//    &dev_attr_modabs_trig.attr,
//    &dev_attr_rxlos_trig.attr,
//    &dev_attr_txfault_int.attr,
//    &dev_attr_modabs_int.attr,
//    &dev_attr_rxlos_int.attr,
//    &dev_attr_txfault_mask.attr,
//    &dev_attr_modabs_mask.attr,
//    &dev_attr_rxlos_mask.attr,
    NULL,
};

ATTRIBUTE_GROUPS(sfp);

static struct resource pddf_cpld_resources[] = {
    {
        .start  = 0x0200,
        .end    = 0x0255,
        .flags  = IORESOURCE_IO,
    },
};

static void pddf_cpld_dev_release( struct device * dev)
{
    return;
}

static struct platform_device pddf_cpld_dev = {
    .name           = DRIVER_NAME,
    .id             = -1,
    .num_resources  = ARRAY_SIZE(pddf_cpld_resources),
    .resource       = pddf_cpld_resources,
    .dev = {
        .release = pddf_cpld_dev_release,
    }
};

static int pddf_cpld_drv_probe(struct platform_device *pdev)
{
    struct resource *res;
    int err;

    cpld_data = devm_kzalloc(&pdev->dev, sizeof(struct pddf_cpld_data),
                             GFP_KERNEL);
    if (!cpld_data)
        return -ENOMEM;

    mutex_init(&cpld_data->cpld_lock);

    cpld_data->read_addr = VERSION;

    res = platform_get_resource(pdev, IORESOURCE_IO, 0);
    if (unlikely(!res)) {
        printk(KERN_ERR "Specified Resource Not Available...\n");
        return -ENODEV;
    }

    err = sysfs_create_group(&pdev->dev.kobj, &cpld_group);
    if (err) {
        printk(KERN_ERR "Cannot create sysfs for SMC.\n");
        return err;
    }

    celplatform = class_create(THIS_MODULE, "celplatform");
    if (IS_ERR(celplatform)) {
        printk(KERN_ERR "Failed to register device class\n");
        sysfs_remove_group(&pdev->dev.kobj, &cpld_group);
        return PTR_ERR(celplatform);
    }

    cpld_data->fpp_node = device_create_with_groups(celplatform, NULL, MKDEV(0, 0), NULL, sfp_groups, "optical_ports");

    if (IS_ERR(cpld_data->fpp_node)) {
        class_destroy(celplatform);
        sysfs_remove_group(&pdev->dev.kobj, &cpld_group);
        return PTR_ERR(cpld_data->fpp_node);
    }

    err = sysfs_create_link(&pdev->dev.kobj, &cpld_data->fpp_node->kobj, "SFP");
    if (err != 0) {
        put_device(cpld_data->fpp_node);
        device_unregister(cpld_data->fpp_node);
        class_destroy(celplatform);
        sysfs_remove_group(&pdev->dev.kobj, &cpld_group);
        return err;
    }

    // Clear all reset signals
    outb(0xFF, SPR_RESET);

    printk(KERN_WARNING "SMC CPLD Driver Probe Successfully.\n");

    return 0;
}

static int pddf_cpld_drv_remove(struct platform_device *pdev)
{
    device_unregister(cpld_data->fpp_node);
    put_device(cpld_data->fpp_node);
    sysfs_remove_group(&pdev->dev.kobj, &cpld_group);
    class_destroy(celplatform);

    printk(KERN_WARNING "SMC CPLD Driver Remove Successfully.\n");
    return 0;
}

static struct platform_driver pddf_cpld_drv = {
    .probe  = pddf_cpld_drv_probe,
    .remove = __exit_p(pddf_cpld_drv_remove),
    .driver = {
        .name   = DRIVER_NAME,
    },
};

int pddf_cpld_init(void)
{
    // Register platform device and platform driver
    platform_device_register(&pddf_cpld_dev);
    platform_driver_register(&pddf_cpld_drv);
    return 0;
}

void pddf_cpld_exit(void)
{
    // Unregister platform device and platform driver
    platform_driver_unregister(&pddf_cpld_drv);
    platform_device_unregister(&pddf_cpld_dev);
}

module_init(pddf_cpld_init);
module_exit(pddf_cpld_exit);


MODULE_AUTHOR("Celestica Inc.");
MODULE_DESCRIPTION("Celestica Belgite CPLD driver");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");
