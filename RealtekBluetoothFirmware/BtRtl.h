/** @file
 Copyright (c) 2020 zxystd. All rights reserved.
 SPDX-License-Identifier: GPL-3.0-only
 **/

//
//  BtRtl.h
//  RtlBluetoothFirmware
//
//  Created by zxystd on 2019/11/17.
//  Copyright © 2019 zxystd. All rights reserved.
//

#ifndef BtRtl_h
#define BtRtl_h

#include <libkern/c++/OSObject.h>
#include <libkern/libkern.h>

#include "USBDeviceController.hpp"
#include "Hci.h"

typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t hw_platform;
    uint8_t hw_variant;
    uint8_t hw_revision;
    uint8_t fw_variant;
    uint8_t fw_revision;
    uint8_t fw_build_num;
    uint8_t fw_build_ww;
    uint8_t fw_build_yy;
    uint8_t fw_patch_num;
} RtlVersion;

typedef struct __attribute__((packed)) {
    uint8_t     result;
    uint16_t   opcode;
    uint8_t     status;
} RtlSecureSendResult;

typedef struct __attribute__((packed)) {
    uint8_t     reset_type;
    uint8_t     patch_enable;
    uint8_t     ddc_reload;
    uint8_t     boot_option;
    uint32_t   boot_param;
} RtlReset;

typedef struct __attribute__((packed)) {
    uint8_t b[6];
} bdaddr_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t len;
    uint8_t val[];
} RtlTLV;

typedef struct __attribute__((packed)) {
    uint32_t    cnvi_top;
    uint32_t    cnvr_top;
    uint32_t    cnvi_bt;
    uint32_t    cnvr_bt;
    uint16_t    dev_rev_id;
    uint8_t    img_type;
    uint16_t    timestamp;
    uint8_t    build_type;
    uint32_t    build_num;
    uint8_t    secure_boot;
    uint8_t    otp_lock;
    uint8_t    api_lock;
    uint8_t    debug_lock;
    uint8_t    min_fw_build_nn;
    uint8_t    min_fw_build_cw;
    uint8_t    min_fw_build_yy;
    uint8_t    limited_cce;
    uint8_t    sbe_type;
    uint32_t   git_sha1;
    bdaddr_t otp_bd_addr;
} RtlVersionTLV;

typedef struct __attribute__((packed)) {
    uint8_t     zero;
    uint8_t     num_cmds;
    uint8_t     source;
    uint8_t     reset_type;
    uint8_t     reset_reason;
    uint8_t     ddc_status;
} RtlBootUp;

typedef struct __attribute__((packed)) {
    uint8_t     status;
    uint8_t     otp_format;
    uint8_t     otp_content;
    uint8_t     otp_patch;
    uint16_t   dev_revid;
    uint8_t     secure_boot;
    uint8_t     key_from_hdr;
    uint8_t     key_type;
    uint8_t     otp_lock;
    uint8_t     api_lock;
    uint8_t     debug_lock;
    bdaddr_t   otp_bdaddr;
    uint8_t     min_fw_build_nn;
    uint8_t     min_fw_build_cw;
    uint8_t     min_fw_build_yy;
    uint8_t     limited_cce;
    uint8_t     unlocked_state;
} RtlBootParams;

typedef struct __attribute__((packed)) {
    uint8_t    page1[16];
} RtlDebugFeatures;

typedef struct __attribute__((packed))
{
    uint16_t    opcode;    /* OCF & OGF */
    uint8_t     len;
} FWCommandHdr;

#define HCI_OP_READ_LOCAL_VERSION 0x1001
#define HCI_OP_RTL_READ_ROM_VERSION 0xfc6d
#define HCI_OP_RTL_DOWNLOAD_FW 0xfc20
#define HCI_OP_RTL_READ_REG 0xfc61
#define HCI_OP_RTL_COREDUMP 0xfcff

#define RTL_FRAG_LEN 252

// These structs are from linux/drivers/bluetooth/btrtl.h
struct rtl_rom_version_evt {
	__u8 status;
	__u8 version;
} __packed;

struct rtl_download_cmd {
	__u8 index;
	__u8 data[RTL_FRAG_LEN];
} __packed;

struct rtl_download_response {
	__u8 status;
	__u8 index;
} __packed;

#define BDADDR_RTL        (&(bdaddr_t){{0x00, 0x8b, 0x9e, 0x19, 0x03, 0x00}}) // FIXME: This needs to be changed to Realtek specific
#define RSA_HEADER_LEN        644
#define CSS_HEADER_OFFSET    8
#define ECDSA_OFFSET        644
#define ECDSA_HEADER_LEN    320

#define CMD_BUF_MAX_SIZE    256

#define RTL_EPATCH_SIGNATURE "Realtech"
#define RTL_EPATCH_SIGNATURE_V2 "RTBTCore"

struct rtl_epatch_header {
	__u8 signature[8];
	__le32 fw_version;
	__le16 num_patches;
} __packed;

struct rtl_epatch_header_v2 {
	__u8   signature[8];
	__u8   fw_version[8];
	__le32 num_sections;
} __packed;

struct rtl_section {
	__le32 opcode;
	__le32 len;
	u8     data[];
} __packed;

class BtRtl : public OSObject {
    OSDeclareAbstractStructors(BtRtl)
public:
    
    virtual bool initWithDevice(IOService *client, IOUSBHostDevice *dev);
    
    virtual void free() override;
    
    virtual bool setup() = 0;
    
    virtual bool shutdown() = 0;
    
    virtual bool getFirmwareName(char *fwname, size_t len) = 0;
    
    bool securedSend(uint8_t fragmentType, uint32_t len, const uint8_t *fragment);
    
    bool enterMfg();
    
    bool exitMfg(bool reset, bool patched);
    
    bool setEventMask(bool debug);
    
    bool setEventMaskMfg(bool debug);
    
    bool readVersion(RtlVersion *version);
    
    bool readRomVersion(uint8_t *version);

    bool sendRtlReset(uint32_t bootParam);
    
    bool readBootParams(RtlBootParams *params);
    
    bool resetToBootloader();
    
    bool rtlVersionInfo(RtlVersion *ver);
    
    bool rtlBoot(uint32_t bootAddr);
    
    bool readDebugFeatures(RtlDebugFeatures *features);
    
    bool setDebugFeatures(RtlDebugFeatures *features);
    
    bool loadDDCConfig(const char *ddcFileName);
    
    OSData *firmwareConvertion(OSData *originalFirmware);
    
    OSData *requestFirmwareData(const char *fwName, bool noWarn = false);
    
    OSData *parseFirmware(OSData *firmware, uint8_t rom_version, int project_id);
    
    bool downloadFirmware(OSData *firmwarePatch);
    bool setupFirmware();

private:
    static OSData *loadFirmwareFromFile(const char *fileName);

protected:
    
    bool rtlSendHCISync(HciCommandHdr *cmd, void *event, uint32_t eventBufSize, uint32_t *size, int timeout);
    
    bool rtlSendHCISyncEvent(HciCommandHdr *cmd, void *event, uint32_t eventBufSize, uint32_t *size, uint8_t syncEvent, int timeout);
    
    bool rtlBulkHCISync(HciCommandHdr *cmd, void *event, uint32_t eventBufSize, uint32_t *size, int timeout);
    
protected:
    USBDeviceController *m_pUSBDeviceController;
};

#endif /* BtRtl_h */
