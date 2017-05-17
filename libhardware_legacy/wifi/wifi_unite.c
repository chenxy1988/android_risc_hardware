/*
 * Copyright 2008, The Android Open Source Project
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ctype.h>
#include <poll.h>
#include <sys/stat.h>

#include "hardware_legacy/wifi.h"
#include "libwpa_client/wpa_ctrl.h"

#define LOG_TAG "WifiHW"
#include "cutils/log.h"
#include "cutils/memory.h"
#include "cutils/misc.h"
#include "cutils/properties.h"
#include "private/android_filesystem_config.h"
#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#endif

enum Vendor {
    UNKNOWN = -1,
    ATHEROS = 0x1,
    REALTEK = 0x2,
    BCM     = 0x3
};
enum RealtekDeviceModel {
    UNKNOWN_MODEL = -1,
    RTL8723AS = 0x0,
    RTL8821AS = 0x1
};

enum BroadcomDeviceModel {
    BCM4335_4339 = 0x0
};
enum AthoresDeviceModel {
    ATHEROS_DEFAULT  = 0
};
#ifndef bool
typedef enum { false = 0, true = 1 } bool;
#endif

int WifiVendor = UNKNOWN;
int WifiModel  = UNKNOWN_MODEL;
#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif
static struct wpa_ctrl *ctrl_conn;
static struct wpa_ctrl *monitor_conn;

/* socket pair used to exit from a blocking read */
static int exit_sockets[2];

extern int do_dhcp();
extern int ifc_init();
extern void ifc_close();
extern char *dhcp_lasterror();
extern void get_dhcp_info();
extern int init_module(void *, unsigned long, const char *);
extern int delete_module(const char *, unsigned int);

static char primary_iface[PROPERTY_VALUE_MAX];
// TODO: use new ANDROID_SOCKET mechanism, once support for multiple
// sockets is in

#ifndef WIFI_DRIVER_MODULE_ARG
#define WIFI_DRIVER_MODULE_ARG          ""
#endif
#ifndef WIFI_FIRMWARE_LOADER
#define WIFI_FIRMWARE_LOADER		""
#endif
#define WIFI_TEST_INTERFACE		"sta"

#ifndef WIFI_DRIVER_FW_PATH_STA
#define WIFI_DRIVER_FW_PATH_STA		"sta"
#endif
#ifndef WIFI_DRIVER_FW_PATH_AP
#define WIFI_DRIVER_FW_PATH_AP		"ap"
#endif
#ifndef WIFI_DRIVER_FW_PATH_P2P
#define WIFI_DRIVER_FW_PATH_P2P		"p2p"
#endif

#ifndef WIFI_DRIVER_FW_PATH_PARAM
#define WIFI_DRIVER_FW_PATH_PARAM               "/sys/module/wlan/parameters/fwpath"
#endif

#define WIFI_DRIVER_LOADER_DELAY                1000000


#define WIFI_DRIVER_MODULE_PATH_BCM              "/system/lib/modules/bcmdhd.ko"
#define WIFI_DRIVER_MODULE_NAME_BCM              "bcmdhd"
#define WIFI_DRIVER_MODULE_ARG_BCM               ""
#define WIFI_SDIO_IF_DRIVER_MODULE_PATH_BCM  "/system/lib/modules/cfg80211_realtek.ko"

#define WIFI_DRIVER_MODULE_PATH_ATHEROS          "/system/lib/modules/ath6kl_sdio.ko"
#define WIFI_DRIVER_MODULE_NAME_ATHEROS          "ath6kl_sdio"
#define WIFI_DRIVER_MODULE_ARG_ATHEROS           "suspend_mode=3 wow_mode=2 ar6k_clock=26000000 ath6kl_p2p=1"
#define WIFI_DRIVER_P2P_MODULE_ARG_ATHEROS       "suspend_mode=3 wow_mode=2 ar6k_clock=26000000 ath6kl_p2p=1 debug_mask=0x2413"
#define WIFI_SDIO_IF_DRIVER_MODULE_PATH_ATHEROS  "/system/lib/modules/cfg80211.ko"
#define WIFI_SDIO_IF_DRIVER_MODULE_NAME          "cfg80211"
#define WIFI_SDIO_IF_DRIVER_MODULE_ARG           ""
#define WIFI_COMPAT_MODULE_PATH                  "/system/lib/modules/compat.ko"
#define WIFI_COMPAT_MODULE_NAME                  "compat"
#define WIFI_COMPAT_MODULE_ARG                   ""

#define WIFI_DRIVER_MODULE_PATH_REALTEK_8723AS          "/system/lib/modules/8723as.ko"
#define WIFI_DRIVER_MODULE_PATH_REALTEK_8821AS          "/system/lib/modules/8821as.ko"
#define WIFI_DRIVER_MODULE_NAME_REALTEK_8723AS          "8723as"
#define WIFI_DRIVER_MODULE_NAME_REALTEK_8821AS          "8821as"
#define WIFI_DRIVER_MODULE_ARG_REALTEK_RTL8723AS           "ifname=wlan0 if2name=p2p0"
#define WIFI_DRIVER_MODULE_ARG_REALTEK_RTL8821AS           "ifname=wlan0 if2name=p2p0 rtw_vht_enable=2"
#define WIFI_SDIO_IF_DRIVER_MODULE_PATH_REALTEK  "/system/lib/modules/cfg80211_realtek.ko"

static const char DRIVER_MODULE_NAME_BCM[]          = WIFI_DRIVER_MODULE_NAME_BCM;
static const char DRIVER_MODULE_TAG_BCM[]           = WIFI_DRIVER_MODULE_NAME_BCM " ";
static const char DRIVER_MODULE_PATH_BCM[]          = WIFI_DRIVER_MODULE_PATH_BCM;
static const char DRIVER_MODULE_ARG_BCM[]           = WIFI_DRIVER_MODULE_ARG_BCM;

static const char DRIVER_MODULE_NAME_ATHEROS[]      = WIFI_DRIVER_MODULE_NAME_ATHEROS;
static const char DRIVER_MODULE_TAG_ATHEROS[]       = WIFI_DRIVER_MODULE_NAME_ATHEROS " ";
static const char DRIVER_MODULE_PATH_ATHEROS[]      = WIFI_DRIVER_MODULE_PATH_ATHEROS;
static const char DRIVER_MODULE_ARG_ATHEROS[]       = WIFI_DRIVER_MODULE_ARG_ATHEROS;
static const char DRIVER_P2P_MODULE_ARG_ATHEROS[]   = WIFI_DRIVER_P2P_MODULE_ARG_ATHEROS;
static const char DRIVER_SDIO_IF_MODULE_PATH_ATHEROS[]  = WIFI_SDIO_IF_DRIVER_MODULE_PATH_ATHEROS;
static const char DRIVER_COMPAT_MODULE_NAME[]   = WIFI_COMPAT_MODULE_NAME;
static const char DRIVER_COMPAT_MODULE_PATH[]   = WIFI_COMPAT_MODULE_PATH;
static const char DRIVER_COMPAT_MODULE_ARG[]    = WIFI_COMPAT_MODULE_ARG;

static const char* DRIVER_MODULE_NAME_REALTEK[]      = {WIFI_DRIVER_MODULE_NAME_REALTEK_8723AS,
                                                         WIFI_DRIVER_MODULE_NAME_REALTEK_8821AS};
static const char* DRIVER_MODULE_TAG_REALTEK[]       = {WIFI_DRIVER_MODULE_NAME_REALTEK_8723AS " ",
                                                         WIFI_DRIVER_MODULE_NAME_REALTEK_8821AS " "};
static const char* DRIVER_MODULE_PATH_REALTEK[]      = {WIFI_DRIVER_MODULE_PATH_REALTEK_8723AS,
                                                        WIFI_DRIVER_MODULE_PATH_REALTEK_8821AS};
static const char* DRIVER_MODULE_ARG_REALTEK[]       = {WIFI_DRIVER_MODULE_ARG_REALTEK_RTL8723AS,
                                                       WIFI_DRIVER_MODULE_ARG_REALTEK_RTL8821AS};
static const char DRIVER_SDIO_IF_MODULE_PATH_REALTEK[]  = WIFI_SDIO_IF_DRIVER_MODULE_PATH_REALTEK;

static const char DRIVER_SDIO_IF_MODULE_PATH_BCM[]  = WIFI_SDIO_IF_DRIVER_MODULE_PATH_BCM;

static const char DRIVER_SDIO_IF_MODULE_NAME[]  = WIFI_SDIO_IF_DRIVER_MODULE_NAME;
static const char DRIVER_SDIO_IF_MODULE_ARG[]   = WIFI_SDIO_IF_DRIVER_MODULE_ARG;

static const char IFACE_DIR[]           = "/data/system/wpa_supplicant";

static const char BCM_SUPP_PROP_NAME[]      = "init.svc.wpa_supplicant";
static const char BCM_SUPPLICANT_NAME[]     = "wpa_supplicant";
static const char FIRMWARE_LOADER[]     = WIFI_FIRMWARE_LOADER;
static const char DRIVER_PROP_NAME[]    = "wlan.driver.status";
static const char DRIVER_VENDOR_NAME[]  = "wlan.vendor";
static const char SUPPLICANT_NAME[]     = "wpa_supplicant";
static const char SUPP_PROP_NAME[]      = "init.svc.wpa_supplicant";
static const char P2P_SUPPLICANT_NAME_ATHEROS[] = "p2p_supplicant";
static const char P2P_PROP_NAME_ATHEROS[]       = "init.svc.p2p_supplicant";
static const char *P2P_SUPPLICANT_NAME_REALTEK[] = {"rtw_suppl_con",
                                                    "rtw_supplc_adv"}; /* p2p concurrent */
static const char *P2P_PROP_NAME_REALTEK[]       = {"init.svc.rtw_suppl_con",
                                                    "init.svc.rtw_supplc_adv"};
static const char SUPP_CONFIG_TEMPLATE[]= "/system/etc/wifi/wpa_supplicant.conf";
static const char SUPP_CONFIG_FILE[]    = "/data/misc/wifi/wpa_supplicant.conf";
static const char P2P_CONFIG_FILE[]     = "/data/misc/wifi/p2p_supplicant.conf";
static const char CONTROL_IFACE_PATH[]  = "/data/misc/wifi/sockets";
static const char MODULE_FILE[]         = "/proc/modules";

static const char IFNAME[]              = "IFNAME=";
#define IFNAMELEN			(sizeof(IFNAME) - 1)
static const char WPA_EVENT_IGNORE[]    = "CTRL-EVENT-IGNORE ";

static const char SUPP_ENTROPY_FILE[]   = WIFI_ENTROPY_FILE;
static unsigned char dummy_key[21] = { 0x02, 0x11, 0xbe, 0x33, 0x43, 0x35,
    0x68, 0x47, 0x84, 0x99, 0xa9, 0x2b,
    0x1c, 0xd3, 0xee, 0xff, 0xf1, 0xe2,
    0xf3, 0xf4, 0xf5 };

/* Is either SUPPLICANT_NAME or P2P_SUPPLICANT_NAME */
static char supplicant_name[PROPERTY_VALUE_MAX];
/* Is either SUPP_PROP_NAME or P2P_PROP_NAME */
static char supplicant_prop_name[PROPERTY_KEY_MAX];

#ifdef WIFI_UNITE_USE_KERNEL_MODULE
int get_wifi_vendor_info(int* model)
{
#define SYS_SDIO_BUS_BASE_PATH    "/sys/bus/sdio/devices/"
#define SYS_SDIO_BUS_MMC_PATH(num)    SYS_SDIO_BUS_BASE_PATH"mmc"#num":0001:1/vendor"
#define SYS_SDIO_BUS_MMC_PATH_DEVICE(num)    SYS_SDIO_BUS_BASE_PATH"mmc"#num":0001:1/device"
#define MAX_SDIO_NUM 4
    const char* sdio_path[] = {
        SYS_SDIO_BUS_MMC_PATH(0),
        SYS_SDIO_BUS_MMC_PATH(1),
        SYS_SDIO_BUS_MMC_PATH(2),
        SYS_SDIO_BUS_MMC_PATH(3),
    };
    const char* sdio_device_path[] = {
        SYS_SDIO_BUS_MMC_PATH_DEVICE(0),
        SYS_SDIO_BUS_MMC_PATH_DEVICE(1),
        SYS_SDIO_BUS_MMC_PATH_DEVICE(2),
        SYS_SDIO_BUS_MMC_PATH_DEVICE(3),
    };
    char linebuf[1024];
    long value;
    int i;
    int vendor = -1;
    FILE *f = NULL;
    for (i = 0; i < MAX_SDIO_NUM; i++)
    {
        f = fopen(sdio_path[i], "r");
        if (f)
        {
            ALOGE("read a device on sdio bus: %s\n", sdio_path[i]);
            break;
        } else{
            ALOGE("unable to read device on sdio bus: %s\n", sdio_path[i]);
        }
    }
    if (f == NULL)
    {
        ALOGD("Nothing detected but we consider to detect the BCM card");
        property_set(DRIVER_VENDOR_NAME, "broadcom");
        vendor = BCM;
    } else if (fgets(linebuf, sizeof(linebuf) -1, f))
    {
        ALOGD("read vendor info: %s", linebuf);
        value = strtol(linebuf, NULL, 16);
        if (value == 0x024c)
        {
            ALOGD("detect a realtek card");
            property_set(DRIVER_VENDOR_NAME, "realtek");
            vendor = REALTEK;
        } else if (value == 0x0271) {
            ALOGD("detect an atheros card");
            property_set(DRIVER_VENDOR_NAME, "atheros");
            *model = ATHEROS_DEFAULT;
            fclose(f);
            return ATHEROS;
        }
        else {
            ALOGD(" Nothing detected but we consider to detect the BCM card");
            property_set(DRIVER_VENDOR_NAME, "broadcom");
            vendor = BCM;
        }
    }
    if (f)
        fclose(f);
    f = NULL;
    f = fopen(sdio_device_path[i], "r");
    if (f)
    {
        if (fgets(linebuf, sizeof(linebuf) -1, f))
        {
            ALOGD("read device info:%s",linebuf);
            value = strtol(linebuf, NULL, 16);
            if (vendor == REALTEK) {
                 if (value == 0x8723)
                 {
                     ALOGD("realtek RTL8723AS detected!");
                     *model = RTL8723AS;
                 }
                 else if (value == 0x8821)
                 {
                     ALOGD("realtek RTL8821AS detected!");
                     *model = RTL8821AS;
                 }
                 else
                 {
                     ALOGE("realtek unkown device: %s ", linebuf);
                     *model = UNKNOWN_MODEL;
                 }
            } else if (vendor == BCM) {
                    ALOGD("bcm 4335/4339 detected!");
                    *model = BCM4335_4339;
            }
        }
        fclose(f);
    } else {
        ALOGD("Nothing detected but we consider to detect the BCM4339 card");
        *model = BCM4335_4339;
    }
    return vendor;
}
#endif

int get_wifi_ifname_from_prop(char *ifname)
{
    ifname[0] = '\0';
    if (property_get("wifi.interface", ifname, WIFI_TEST_INTERFACE)
            && strcmp(ifname, WIFI_TEST_INTERFACE) != 0)
        return 0;

    ALOGE("Can't get wifi ifname from property \"wifi.interface\"");
    return -1;
}
int check_wifi_ifname_from_proc(char *buf, char *target)
{
#define PROC_NET_DEV_PATH "/proc/net/dev"
#define MAX_WIFI_IFACE_NUM 20

    char linebuf[1024];
    unsigned char wifi_ifcount = 0;
    char wifi_ifaces[MAX_WIFI_IFACE_NUM][IFNAMSIZ+1];
    int i, ret = -1;
    int match = -1; /* if matched, this means the index*/
    FILE *f = fopen(PROC_NET_DEV_PATH, "r");

    if(buf)
        buf[0] = '\0';

    if (!f) {
        ALOGE("Unable to read %s: %s\n", PROC_NET_DEV_PATH, strerror(errno));
        goto exit;
    }

    /* Get wifi interfaces form PROC_NET_DEV_PATH*/
    memset(wifi_ifaces, 0, MAX_WIFI_IFACE_NUM * (IFNAMSIZ+1));
    while(fgets(linebuf, sizeof(linebuf)-1, f)) {

        if (strchr(linebuf, ':')) {
            char *dest = wifi_ifaces[wifi_ifcount];
            char *p = linebuf;

            while(*p && isspace(*p))
                ++p;
            while (*p && *p != ':') {
                *dest++ = *p++;
            }
            *dest = '\0';

            ALOGD("%s: find %s\n", __func__, wifi_ifaces[wifi_ifcount]);
            wifi_ifcount++;
            if (wifi_ifcount>=MAX_WIFI_IFACE_NUM) {
                ALOGD("%s: wifi_ifcount >= MAX_WIFI_IFACE_NUM(%u)\n", __func__,
                        MAX_WIFI_IFACE_NUM);
                break;
            }
        }
    }
    fclose(f);

    if (target) {
        /* Try to find match */
        for (i = 0;i < wifi_ifcount;i++) {
            if (strcmp(target, wifi_ifaces[i]) == 0) {
                match = i;
                break;
            }
        }
    } else {
        /* No target, use the first wifi_iface as target*/
        match = 0;
    }

    if(buf && match >= 0)
        strncpy(buf, wifi_ifaces[match], IFNAMSIZ);

    if (match >= 0)
        ret = 0;
exit:
    return ret;
}

int get_wifi_ifname_from_proc(char *ifname)
{
    return check_wifi_ifname_from_proc(ifname, NULL);
}

#define PRIMARY     0
#define SECONDARY   1

char *wifi_ifname(int index)
{
#define WIFI_P2P_INTERFACE "p2p0"

    char primary_if[IFNAMSIZ+1];
    char second_if[IFNAMSIZ+1];

    if (index == PRIMARY) {
        primary_iface[0] = '\0';
        if (get_wifi_ifname_from_prop(primary_if) == 0 &&
                check_wifi_ifname_from_proc(primary_iface, primary_if) == 0) {
            return primary_iface;
        }
    } else if (index == SECONDARY) {
        if (check_wifi_ifname_from_proc(NULL, WIFI_P2P_INTERFACE) == 0)
            return WIFI_P2P_INTERFACE;
    }
    return NULL;
}

static int insmod(const char *filename, const char *args)
{
    void *module;
    unsigned int size;
    int ret;

    module = load_file(filename, &size);
    if (!module)
    {   ALOGE("insmod:load_file %s  error", filename);
        return -1;
    }

    ret = init_module(module, size, args);

    free(module);

    return ret;
}

static int rmmod(const char *modname)
{
    int ret = -1;
    int maxtry = 10;

    while (maxtry-- > 0) {
        ret = delete_module(modname, O_NONBLOCK | O_EXCL);
        if (ret < 0 && errno == EAGAIN)
            usleep(500000);
        else
            break;
    }

    if (ret != 0)
        ALOGD("Unable to unload driver module \"%s\": %s\n",
                modname, strerror(errno));
    return ret;
}

int do_dhcp_request(int *ipaddr, int *gateway, int *mask,
        int *dns1, int *dns2, int *server, int *lease) {
    /* For test driver, always report success */
    if (strcmp(primary_iface, WIFI_TEST_INTERFACE) == 0)
        return 0;

    if (ifc_init() < 0)
        return -1;

    if (do_dhcp(primary_iface) < 0) {
        ifc_close();
        return -1;
    }
    ifc_close();
    get_dhcp_info(ipaddr, gateway, mask, dns1, dns2, server, lease);
    return 0;
}

const char *get_dhcp_error_string() {
    return dhcp_lasterror();
}

int check_wifi_driver_loaded_strict(const char *driver_module_name)
{
    char line[sizeof(driver_module_name) + 10];
    FILE *proc;
    if ((proc = fopen(MODULE_FILE, "r")) == NULL) {
        ALOGW("Could not open %s: %s", MODULE_FILE, strerror(errno));
        property_set(DRIVER_PROP_NAME, "unloaded");
        property_set(DRIVER_VENDOR_NAME, "");
        return 0;
    }
    while ((fgets(line, sizeof(line), proc)) != NULL) {
        if (strncmp(line, driver_module_name, strlen(driver_module_name)) == 0) {
            fclose(proc);
            return 1;
        }
    }
    fclose(proc);
    property_set(DRIVER_VENDOR_NAME, "");
    property_set(DRIVER_PROP_NAME, "unloaded");
    return 0;
}

int is_wifi_driver_loaded() {

#ifdef WIFI_UNITE_USE_KERNEL_MODULE

    char driver_status[PROPERTY_VALUE_MAX];
    if (!property_get(DRIVER_PROP_NAME, driver_status, NULL)
            || strcmp(driver_status, "ok") != 0) {
        return 0;  /* driver not loaded */
    }

    if (WifiVendor == ATHEROS)
    {
        return check_wifi_driver_loaded_strict(DRIVER_MODULE_TAG_ATHEROS);
    } else if (WifiVendor == REALTEK) {
        return check_wifi_driver_loaded_strict(DRIVER_MODULE_TAG_REALTEK[WifiModel]);
    } else if (WifiVendor == BCM) {
        return check_wifi_driver_loaded_strict(DRIVER_MODULE_NAME_BCM);
    } else
        return 0;//Unkown driver
#else

    return 0;

#endif
}

#ifdef WIFI_UNITE_USE_KERNEL_MODULE
int wifi_insmod_driver_realtek()
{
    if (insmod(DRIVER_SDIO_IF_MODULE_PATH_REALTEK, DRIVER_SDIO_IF_MODULE_ARG) < 0)
    {
        ALOGE("%s: error via loading %s",__func__, DRIVER_SDIO_IF_MODULE_PATH_REALTEK);
        return -1;
    }
    if (insmod(DRIVER_MODULE_PATH_REALTEK[WifiModel], DRIVER_MODULE_ARG_REALTEK[WifiModel]) < 0)
    {
        ALOGE("%s: error via loading %s",__func__, DRIVER_MODULE_PATH_REALTEK[WifiModel]);
        return -1;
    }
    return 0;
}
#endif

#ifdef WIFI_UNITE_USE_KERNEL_MODULE
int wifi_insmod_driver_bcm()
{
    if (insmod(DRIVER_SDIO_IF_MODULE_PATH_BCM, DRIVER_SDIO_IF_MODULE_ARG) < 0)
        return -1;

    if (insmod(DRIVER_MODULE_PATH_BCM, DRIVER_MODULE_ARG_BCM) < 0)
        return -1;
    return 0;
}
#endif

#ifdef WIFI_UNITE_USE_KERNEL_MODULE
int wifi_insmod_driver_atheros()
{
    if (insmod(DRIVER_COMPAT_MODULE_PATH, DRIVER_COMPAT_MODULE_ARG) < 0)
        return -1;
    if (insmod(DRIVER_SDIO_IF_MODULE_PATH_ATHEROS, DRIVER_SDIO_IF_MODULE_ARG) < 0)
        return -1;

    if (insmod(DRIVER_MODULE_PATH_ATHEROS, DRIVER_MODULE_ARG_ATHEROS) < 0)
        return -1;
    return 0;
}
#endif 

int wifi_load_driver()
{
#ifdef WIFI_UNITE_USE_KERNEL_MODULE
    char driver_status[PROPERTY_VALUE_MAX];
    int count = 100; /* wait at most 20 seconds for completion */
    int ret;
#ifdef SABRESD_7D
    /* Sabresd_7d use BCM4339 by force */
    WifiVendor = BCM;
    WifiModel = BCM4335_4339;
    property_set(DRIVER_VENDOR_NAME, "broadcom");
#else
    WifiVendor = get_wifi_vendor_info(&WifiModel);
#endif
    if (is_wifi_driver_loaded()) {
        return 0;
    }
    if (WifiVendor == ATHEROS) {
        ALOGD("load driver atheros");
        ret =  wifi_insmod_driver_atheros();
    } else if (WifiVendor == REALTEK) {
        ALOGD("load driver realtek");
        if (WifiModel == UNKNOWN_MODEL)
            return -1;
        ret =  wifi_insmod_driver_realtek();
    } else if (WifiVendor == BCM) {
        ALOGD("load driver bcm");
        if (WifiModel == BCM4335_4339)
            ret = wifi_insmod_driver_bcm();
        else
            ret = -1;
    } else
        return -1;
    if (ret < 0)
        return -1;
    if (strcmp(FIRMWARE_LOADER,"") == 0) {
        while (wifi_ifname(PRIMARY) == NULL && count-- > 0) {
            usleep(100000);
        }
        if (wifi_ifname(PRIMARY) == NULL) {
            ALOGE("%s: get wifi_ifname(PRIMARY) fail\n", __func__);
            goto timeout;
        }
        ALOGD("set driver prop OK");
        property_set(DRIVER_PROP_NAME, "ok");
    }

    sched_yield();
    while (count-- > 0) {
        if (property_get(DRIVER_PROP_NAME, driver_status, NULL)) {
            if (strcmp(driver_status, "ok") == 0)
                return 0;
            else if (strcmp(DRIVER_PROP_NAME, "failed") == 0) {
                wifi_unload_driver();
                return -1;
            }
        }
        usleep(200000);
    }
timeout:
    property_set(DRIVER_PROP_NAME, "timeout");
    wifi_unload_driver();
    return -1;
#else
    WifiVendor = BCM;
    WifiModel = BCM4335_4339;
    property_set(DRIVER_VENDOR_NAME, "broadcom");
    return 0;
#endif
}

#ifdef WIFI_UNITE_USE_KERNEL_MODULE
static int __wifi_unload_driver(const char* mod_name, bool supportCompat)
{
    if (rmmod(mod_name) == 0) {
        int count = 100; /* wait at most 10 seconds for completion */
        while (count-- > 0) {
            if (!is_wifi_driver_loaded())
                break;
            usleep(100000);
        }
        if (count) {
            /* unload cfg80211 kernel module */
            if (rmmod(DRIVER_SDIO_IF_MODULE_NAME) != 0) {
                return -1;
            }
            if (supportCompat) {
                /* unload compat kernel module */
                if (rmmod(DRIVER_COMPAT_MODULE_NAME) != 0) {
                    return -1;
                }
            }
        }
        if (!is_wifi_driver_loaded()) {
            return 0;
        }
        return -1;
    } else
        return -1;
}
#endif

int wifi_unload_driver()
{
#ifdef WIFI_UNITE_USE_KERNEL_MODULE
    usleep(200000); /* allow to finish interface down */
    if (WifiVendor == REALTEK)
        return __wifi_unload_driver(DRIVER_MODULE_NAME_REALTEK[WifiModel], false);
    else if (WifiVendor == ATHEROS)
        return __wifi_unload_driver(DRIVER_MODULE_NAME_ATHEROS, true);
    else if (WifiVendor == BCM) {
        return __wifi_unload_driver(DRIVER_MODULE_NAME_BCM, false);
    }
        return -1;
#else
	return 0;
#endif
}

int ensure_entropy_file_exists()
{
    int ret;
    int destfd;

    ret = access(SUPP_ENTROPY_FILE, R_OK|W_OK);
    if ((ret == 0) || (errno == EACCES)) {
        if ((ret != 0) &&
                (chmod(SUPP_ENTROPY_FILE, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) != 0)) {
            ALOGE("Cannot set RW to \"%s\": %s", SUPP_ENTROPY_FILE, strerror(errno));
            return -1;
        }
        return 0;
    }
    destfd = TEMP_FAILURE_RETRY(open(SUPP_ENTROPY_FILE, O_CREAT|O_RDWR, 0660));
    if (destfd < 0) {
        ALOGE("Cannot create \"%s\": %s", SUPP_ENTROPY_FILE, strerror(errno));
        return -1;
    }

    if (TEMP_FAILURE_RETRY(write(destfd, dummy_key, sizeof(dummy_key))) != sizeof(dummy_key)) {
        ALOGE("Error writing \"%s\": %s", SUPP_ENTROPY_FILE, strerror(errno));
        close(destfd);
        return -1;
    }
    close(destfd);

    /* chmod is needed because open() didn't set permisions properly */
    if (chmod(SUPP_ENTROPY_FILE, 0660) < 0) {
        ALOGE("Error changing permissions of %s to 0660: %s",
                SUPP_ENTROPY_FILE, strerror(errno));
        unlink(SUPP_ENTROPY_FILE);
        return -1;
    }

    if (chown(SUPP_ENTROPY_FILE, AID_SYSTEM, AID_WIFI) < 0) {
        ALOGE("Error changing group ownership of %s to %d: %s",
                SUPP_ENTROPY_FILE, AID_WIFI, strerror(errno));
        unlink(SUPP_ENTROPY_FILE);
        return -1;
    }
    return 0;
}

int update_ctrl_interface(const char *config_file) {

    int srcfd, destfd;
    int nread;
    char ifc[PROPERTY_VALUE_MAX];
    char *pbuf;
    char *sptr;
    struct stat sb;
    int ret;

    if (stat(config_file, &sb) != 0)
        return -1;

    pbuf = malloc(sb.st_size + PROPERTY_VALUE_MAX);
    if (!pbuf)
        return 0;
    memset(pbuf, 0, sb.st_size + PROPERTY_VALUE_MAX);
    srcfd = TEMP_FAILURE_RETRY(open(config_file, O_RDONLY));
    if (srcfd < 0) {
        ALOGE("Cannot open \"%s\": %s", config_file, strerror(errno));
        free(pbuf);
        return 0;
    }
    nread = TEMP_FAILURE_RETRY(read(srcfd, pbuf, sb.st_size));
    close(srcfd);
    if (nread < 0) {
        ALOGE("Cannot read \"%s\": %s", config_file, strerror(errno));
        free(pbuf);
        return 0;
    }

    if (!strcmp(config_file, SUPP_CONFIG_FILE)) {
        property_get("wifi.interface", ifc, WIFI_TEST_INTERFACE);
        if (wifi_ifname(PRIMARY) == NULL) {
            ALOGE("%s: get wifi_ifname(PRIMARY) fail\n", __func__);
            free(pbuf);
            return -1;
        }
	if (strlen(wifi_ifname(PRIMARY)) < PROPERTY_VALUE_MAX)
	    strcpy(ifc, wifi_ifname(PRIMARY));
	else {
	    ALOGE("too long wifi_ifname.");
            return -1;
	}
    } else {
        strcpy(ifc, CONTROL_IFACE_PATH);
    }
    /* Assume file is invalid to begin with */
    ret = -1;
    /*
     * if there is a "ctrl_interface=<value>" entry, re-write it ONLY if it is
     * NOT a directory.  The non-directory value option is an Android add-on
     * that allows the control interface to be exchanged through an environment
     * variable (initialized by the "init" program when it starts a service
     * with a "socket" option).
     *
     * The <value> is deemed to be a directory if the "DIR=" form is used or
     * the value begins with "/".
     */
    if ((sptr = strstr(pbuf, "ctrl_interface="))) {
        ret = 0;
        if ((!strstr(pbuf, "ctrl_interface=DIR=")) &&
                (!strstr(pbuf, "ctrl_interface=/"))) {
            char *iptr = sptr + strlen("ctrl_interface=");
            int ilen = 0;
            int mlen = strlen(ifc);
            int nwrite;
            if (strncmp(ifc, iptr, mlen) != 0) {
                ALOGE("ctrl_interface != %s", ifc);
                while (((ilen + (iptr - pbuf)) < nread) && (iptr[ilen] != '\n'))
                    ilen++;
                mlen = ((ilen >= mlen) ? ilen : mlen) + 1;
                memmove(iptr + mlen, iptr + ilen + 1, nread - (iptr + ilen + 1 - pbuf));
                memset(iptr, '\n', mlen);
                memcpy(iptr, ifc, strlen(ifc));
                destfd = TEMP_FAILURE_RETRY(open(config_file, O_RDWR, 0660));
                if (destfd < 0) {
                    ALOGE("Cannot update \"%s\": %s", config_file, strerror(errno));
                    free(pbuf);
                    return -1;
                }
                TEMP_FAILURE_RETRY(write(destfd, pbuf, nread + mlen - ilen -1));
                close(destfd);
            }
        }
    }
    free(pbuf);
    return ret;
}

int ensure_config_file_exists(const char *config_file)
{
    char buf[2048];
    int srcfd, destfd;
    struct stat sb;
    int nread;
    int ret;

    ret = access(config_file, R_OK|W_OK);
    if ((ret == 0) || (errno == EACCES)) {
        if ((ret != 0) &&
                (chmod(config_file, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) != 0)) {
            ALOGE("Cannot set RW to \"%s\": %s", config_file, strerror(errno));
            return -1;
        }
        /* return if we were able to update control interface properly */
        if (update_ctrl_interface(config_file) >=0) {
            return 0;
        } else {
            /* This handles the scenario where the file had bad data
             * for some reason. We continue and recreate the file.
             */
        }
    } else if (errno != ENOENT) {
        ALOGE("Cannot access \"%s\": %s", config_file, strerror(errno));
        return -1;
    }

    srcfd = TEMP_FAILURE_RETRY(open(SUPP_CONFIG_TEMPLATE, O_RDONLY));
    if (srcfd < 0) {
        ALOGE("Cannot open \"%s\": %s", SUPP_CONFIG_TEMPLATE, strerror(errno));
        return -1;
    }

    destfd = TEMP_FAILURE_RETRY(open(config_file, O_CREAT|O_RDWR, 0660));
    if (destfd < 0) {
        close(srcfd);
        ALOGE("Cannot create \"%s\": %s", config_file, strerror(errno));
        return -1;
    }

    while ((nread = TEMP_FAILURE_RETRY(read(srcfd, buf, sizeof(buf)))) != 0) {
        if (nread < 0) {
            ALOGE("Error reading \"%s\": %s", SUPP_CONFIG_TEMPLATE, strerror(errno));
            close(srcfd);
            close(destfd);
            unlink(config_file);
            return -1;
        }
        TEMP_FAILURE_RETRY(write(destfd, buf, nread));
    }

    close(destfd);
    close(srcfd);

    /* chmod is needed because open() didn't set permisions properly */
    if (chmod(config_file, 0660) < 0) {
        ALOGE("Error changing permissions of %s to 0660: %s",
                config_file, strerror(errno));
        unlink(config_file);
        return -1;
    }

    if (chown(config_file, AID_SYSTEM, AID_WIFI) < 0) {
        ALOGE("Error changing group ownership of %s to %d: %s",
                config_file, AID_WIFI, strerror(errno));
        unlink(config_file);
        return -1;
    }
    return update_ctrl_interface(config_file);
}

int wifi_start_supplicant(int p2p_supported)
{
    char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
    int count = 200; /* wait at most 20 seconds for completion */
#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
    const prop_info *pi;
    unsigned serial = 0, i;
#endif
    ALOGE("wifi_start_supplicant p2p=%d\n",p2p_supported);
    if (p2p_supported) {
        if (WifiVendor == ATHEROS) {
            strcpy(supplicant_name, P2P_SUPPLICANT_NAME_ATHEROS);
            strcpy(supplicant_prop_name, P2P_PROP_NAME_ATHEROS);
        } else if (WifiVendor == REALTEK) {
            strcpy(supplicant_name, P2P_SUPPLICANT_NAME_REALTEK[WifiModel]);
            strcpy(supplicant_prop_name, P2P_PROP_NAME_REALTEK[WifiModel]);
        } else if (WifiVendor == BCM)
        {
            strcpy(supplicant_name, BCM_SUPPLICANT_NAME);
            strcpy(supplicant_prop_name, BCM_SUPP_PROP_NAME);
        }

        /* Ensure p2p config file is created */
        if (ensure_config_file_exists(P2P_CONFIG_FILE) < 0) {
            ALOGE("Failed to create a p2p config file");
            return -1;
        }

    } else {
        strcpy(supplicant_name, SUPPLICANT_NAME);
        strcpy(supplicant_prop_name, SUPP_PROP_NAME);
    }

    /* Check whether already running */
    if (property_get(supplicant_name, supp_status, NULL)
            && strcmp(supp_status, "running") == 0) {
        return 0;
    }
    ALOGE("Wi-Fi will ensure config file exist\n");
    /* Before starting the daemon, make sure its config file exists */
    if (ensure_config_file_exists(SUPP_CONFIG_FILE) < 0) {
        ALOGE("Wi-Fi will not be enabled");
        return -1;
    }

    if (ensure_entropy_file_exists() < 0) {
        ALOGE("Wi-Fi entropy file was not created");
    }

    /* Clear out any stale socket files that might be left over. */
    wpa_ctrl_cleanup();

    /* Reset sockets used for exiting from hung state */
    exit_sockets[0] = exit_sockets[1] = -1;

#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
    /*
     * Get a reference to the status property, so we can distinguish
     * the case where it goes stopped => running => stopped (i.e.,
     * it start up, but fails right away) from the case in which
     * it starts in the stopped state and never manages to start
     * running at all.
     */
    pi = __system_property_find(supplicant_prop_name);
    if (pi != NULL) {
        serial = __system_property_serial(pi);
    }
#endif
    property_get("wifi.interface", primary_iface, WIFI_TEST_INTERFACE);
    ALOGD("start supplicant cmd=%s iface=%s",supplicant_name,primary_iface);
    /* Check the interface exist*/
    if (p2p_supported) {
        if (WifiVendor != BCM) {
            int count = 10; /* wait at most 1 seconds for completion */
            while (wifi_ifname(SECONDARY) == NULL && count-- > 0) {
                usleep(100000);
            }
            if (wifi_ifname(SECONDARY) == NULL) {
                ALOGE("%s get wifi_ifname(SECONDARY) fail", __func__);
                return -1;
            }
        }
    }
    if(wifi_ifname(PRIMARY) == NULL) {
        ALOGE("%s get wifi_ifname(PRIMARY) fail", __func__);
        return -1;
    }
    property_set("ctl.start", supplicant_name);
    sched_yield();

    while (count-- > 0) {
#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
        if (pi == NULL) {
            pi = __system_property_find(supplicant_prop_name);
        }
        if (pi != NULL) {
            __system_property_read(pi, NULL, supp_status);
            if (strcmp(supp_status, "running") == 0) {
                return 0;
            } else if (__system_property_serial(pi) != serial &&
                    strcmp(supp_status, "stopped") == 0) {
                return -1;
            }
        }
#else
        if (property_get(supplicant_prop_name, supp_status, NULL)) {
            ALOGE("supp_status=%s\n",supp_status);
            if (strcmp(supp_status, "running") == 0)
                return 0;
        }
#endif
        usleep(100000);
    }
    return -1;
}

int wifi_stop_supplicant(int p2p_supported)
{
    char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
    int count = 50; /* wait at most 5 seconds for completion */

    if (p2p_supported) {
        if (WifiVendor == ATHEROS)
        {
            strcpy(supplicant_name, P2P_SUPPLICANT_NAME_ATHEROS);
            strcpy(supplicant_prop_name, P2P_PROP_NAME_ATHEROS);
        } else if (WifiVendor == REALTEK)
        {
            strcpy(supplicant_name, P2P_SUPPLICANT_NAME_REALTEK[WifiModel]);
            strcpy(supplicant_prop_name, P2P_PROP_NAME_REALTEK[WifiModel]);
        }
        else if( WifiVendor == BCM)
        {
            strcpy(supplicant_name, BCM_SUPPLICANT_NAME);
            strcpy(supplicant_prop_name, BCM_SUPP_PROP_NAME);
        }
    } else {
        strcpy(supplicant_name, SUPPLICANT_NAME);
        strcpy(supplicant_prop_name, SUPP_PROP_NAME);
    }

    /* Check whether supplicant already stopped */
    if (property_get(supplicant_prop_name, supp_status, NULL)
            && strcmp(supp_status, "stopped") == 0) {
        return 0;
    }

    property_set("ctl.stop", supplicant_name);
    sched_yield();

    while (count-- > 0) {
        if (property_get(supplicant_prop_name, supp_status, NULL)) {
            if (strcmp(supp_status, "stopped") == 0)
                return 0;
        }
        usleep(100000);
    }
    ALOGE("Failed to stop supplicant");
    return -1;
}

int wifi_connect_on_socket_path(const char *path)
{
    char supp_status[PROPERTY_VALUE_MAX] = {'\0'};

    /* Make sure supplicant is running */
    if (!property_get(supplicant_prop_name, supp_status, NULL)
            || strcmp(supp_status, "running") != 0) {
        ALOGE("Supplicant not running, cannot connect");
        return -1;
    }

    ctrl_conn = wpa_ctrl_open(path);
    if (ctrl_conn == NULL) {
        ALOGE("Unable to open connection to supplicant on \"%s\": %s",
                path, strerror(errno));
        return -1;
    }
    monitor_conn = wpa_ctrl_open(path);
    if (monitor_conn == NULL) {
        wpa_ctrl_close(ctrl_conn);
        ctrl_conn = NULL;
        return -1;
    }
    if (wpa_ctrl_attach(monitor_conn) != 0) {
        wpa_ctrl_close(monitor_conn);
        wpa_ctrl_close(ctrl_conn);
        ctrl_conn = monitor_conn = NULL;
        return -1;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, exit_sockets) == -1) {
        wpa_ctrl_close(monitor_conn);
        wpa_ctrl_close(ctrl_conn);
        ctrl_conn = monitor_conn = NULL;
        return -1;
    }

    return 0;
}

/* Establishes the control and monitor socket connections on the interface */
int wifi_connect_to_supplicant()
{
    static char path[PATH_MAX];

    if (access(IFACE_DIR, F_OK) == 0) {
        snprintf(path, sizeof(path), "%s/%s", IFACE_DIR, primary_iface);
    } else {
        ALOGD("wifi will connect android wpa %s\n", primary_iface);
        snprintf(path, sizeof(path), "@android:wpa_%s", primary_iface);
    }
    return wifi_connect_on_socket_path(path);

}

int wifi_send_command(const char *cmd, char *reply, size_t *reply_len)
{
    int ret;
    if (ctrl_conn == NULL) {
        ALOGV("Not connected to wpa_supplicant - \"%s\" command dropped.\n", cmd);
        return -1;
    }
    ret = wpa_ctrl_request(ctrl_conn, cmd, strlen(cmd), reply, reply_len, NULL);
    if (ret == -2) {
        ALOGD("'%s' command timed out.\n", cmd);
        /* unblocks the monitor receive socket for termination */
        TEMP_FAILURE_RETRY(write(exit_sockets[0], "T", 1));
        return -2;
    } else if (ret < 0 || strncmp(reply, "FAIL", 4) == 0) {
        return -1;
    }
    if (strncmp(cmd, "PING", 4) == 0) {
        reply[*reply_len] = '\0';
    }
    return 0;

}

void wifi_close_sockets()
{
    if (ctrl_conn != NULL) {
        wpa_ctrl_close(ctrl_conn);
        ctrl_conn = NULL;
    }

    if (monitor_conn != NULL) {
        wpa_ctrl_close(monitor_conn);
        monitor_conn = NULL;
    }

    if (exit_sockets[0] >= 0) {
        close(exit_sockets[0]);
        exit_sockets[0] = -1;
    }

    if (exit_sockets[1] >= 0) {
        close(exit_sockets[1]);
        exit_sockets[1] = -1;
    }

}

int wifi_ctrl_recv(char *reply, size_t *reply_len)
{
    int res;
    int ctrlfd = wpa_ctrl_get_fd(monitor_conn);
    struct pollfd rfds[2];

    memset(rfds, 0, 2 * sizeof(struct pollfd));
    rfds[0].fd = ctrlfd;
    rfds[0].events |= POLLIN;
    rfds[1].fd = exit_sockets[1];
    rfds[1].events |= POLLIN;
    res = TEMP_FAILURE_RETRY(poll(rfds, 2, -1));
    if (res < 0) {
        ALOGE("Error poll = %d", res);
        return res;
    }
    if (rfds[0].revents & POLLIN) {
        return wpa_ctrl_recv(monitor_conn, reply, reply_len);
    }

    /* it is not rfds[0], then it must be rfts[1] (i.e. the exit socket)
     * or we timed out. In either case, this call has failed ..
     */
    return -2;

}

int wifi_wait_on_socket(char *buf, size_t buflen)
{
    size_t nread = buflen - 1;
    int result;
    char *match, *match2;

    if (monitor_conn == NULL) {
        return snprintf(buf, buflen, WPA_EVENT_TERMINATING " - connection closed");
    }

    result = wifi_ctrl_recv(buf, &nread);

    /* Terminate reception on exit socket */
    if (result == -2) {
        return snprintf(buf, buflen, WPA_EVENT_TERMINATING " - connection closed");
    }

    if (result < 0) {
        ALOGD("wifi_ctrl_recv failed: %s\n", strerror(errno));
        return snprintf(buf, buflen, WPA_EVENT_TERMINATING " - recv error");
    }
    buf[nread] = '\0';
    /* Check for EOF on the socket */
    if (result == 0 && nread == 0) {
        /* Fabricate an event to pass up */
        ALOGD("Received EOF on supplicant socket\n");
        return snprintf(buf, buflen, WPA_EVENT_TERMINATING " - signal 0 received");
    }
    /*
     * Events strings are in the format
     *
     *     IFNAME=iface <N>CTRL-EVENT-XXX 
     *        or
     *     <N>CTRL-EVENT-XXX 
     *
     * where N is the message level in numerical form (0=VERBOSE, 1=DEBUG,
     * etc.) and XXX is the event name. The level information is not useful
     * to us, so strip it off.
     */

    if (strncmp(buf, IFNAME, IFNAMELEN) == 0) {
        match = strchr(buf, ' ');
        if (match != NULL) {
            if (match[1] == '<') {
                match2 = strchr(match + 2, '>');
                if (match2 != NULL) {
                    nread -= (match2 - match);
                    memmove(match + 1, match2 + 1, nread - (match - buf) + 1);
                }
            }
        } else {
            return snprintf(buf, buflen, "%s", WPA_EVENT_IGNORE);
        }
    } else if (buf[0] == '<') {
        match = strchr(buf, '>');
        if (match != NULL) {
            nread -= (match + 1 - buf);
            memmove(buf, match + 1, nread + 1);
            ALOGV("supplicant generated event without interface - %s\n", buf);
        }
    } else {
        /* let the event go as is! */
        ALOGW("supplicant generated event without interface and without message level - %s\n", buf);
    }

    return nread;
}

int wifi_wait_for_event(char *buf, size_t buflen)
{
    return wifi_wait_on_socket(buf, buflen);
}



void wifi_close_supplicant_connection()
{
    char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
    int count = 50; /* wait at most 5 seconds to ensure init has stopped stupplicant */

    wifi_close_sockets();

    while (count-- > 0) {
        if (property_get(supplicant_prop_name, supp_status, NULL)) {
            if (strcmp(supp_status, "stopped") == 0)
                return;
        }
        usleep(100000);
    }
}

int wifi_command(const char *command, char *reply, size_t *reply_len)
{
    return wifi_send_command(command, reply, reply_len);
}

const char *wifi_get_fw_path(int fw_type)
{
    switch (fw_type) {
        case WIFI_GET_FW_PATH_STA:
            return WIFI_DRIVER_FW_PATH_STA;
        case WIFI_GET_FW_PATH_AP:
            return WIFI_DRIVER_FW_PATH_AP;
        case WIFI_GET_FW_PATH_P2P:
            return WIFI_DRIVER_FW_PATH_P2P;
    }
    return NULL;
}

int wifi_change_fw_path(const char *fwpath)
{
    int len;
    int fd;
    int ret = 0;
    char vendor_name[255];
    property_get(DRIVER_VENDOR_NAME, vendor_name, "");
    if (strcmp(vendor_name, "broadcom"))
	return ret;
    if (!fwpath)
        return ret;
    ALOGD("Set wifi firmware path:%s", fwpath);
    fd = TEMP_FAILURE_RETRY(open(WIFI_DRIVER_FW_PATH_PARAM, O_WRONLY));
    if (fd < 0) {
        ALOGE("Failed to open wlan fw path param (%s)", strerror(errno));
        return -1;
    }
    len = strlen(fwpath) + 1;
    if (TEMP_FAILURE_RETRY(write(fd, fwpath, len)) != len) {
        ALOGE("Failed to write wlan fw path param (%s)", strerror(errno));
        ret = -1;
    }

    close(fd);
    return ret;
}
