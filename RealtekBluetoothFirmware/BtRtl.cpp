/** @file
  Copyright (c) 2020 zxystd. All rights reserved.
  SPDX-License-Identifier: GPL-3.0-only
**/

//
//  BtRtl.cpp
//  RtlBluetoothFirmware
//
//  Created by zxystd on 2019/11/17.
//  Copyright © 2019 zxystd. All rights reserved.
//

#include "BtRtl.h"
#include "Log.h"
#include <IOKit/storage/IOStorage.h>
#include <IOKit/IOKitKeys.h>
#include <libkern/OSMalloc.h>

#include "FwData.h"

#define super OSObject
OSDefineMetaClassAndAbstractStructors(BtRtl, OSObject)

enum : uint32_t {
    RTL_PATCH_SNIPPETS = 0x01,
    RTL_PATCH_DUMMY_HEADER = 0x02,
    RTL_PATCH_SECURITY_HEADER = 0x03,
};
static constexpr uint32_t RTL_V2_MAX_PATCH_PARTS = 64;
static constexpr uint32_t RTL_EXTENSION_INSTRUCTION_SIZE = 3;
static constexpr uint32_t RTL_EXTENSION_TAIL_SIZE = RTL_EXTENSION_INSTRUCTION_SIZE + 4;

static int rtlExpectedProjectId(uint16_t lmp_subversion)
{
    switch (lmp_subversion) {
        case 0x1200: return 0;  // 8723A
        case 0x8723: return 1;  // 8723B
        case 0x8821: return 2;  // 8821A
        case 0x8761: return 3;  // 8761A
        case 0x8703: return 7;  // 8703B
        case 0x8b23: return 9;  // 8723D family
        case 0xd723: return 9;  // 8723D family
        case 0x8c10: return 10; // 8821C family
        case 0xa10c: return 10; // 8821C family
        case 0x8822: return 8;  // 8822B
        case 0x8852: return 20; // 8852B/BU (common USB case)
        case 0x8851: return 36; // 8851B
        case 0x8922: return 44; // 8922A
        default: return -1;
    }
}

bool BtRtl::firmwareNameFromLmpSubversion(uint16_t lmp_subversion, const char *&fw_name, bool &requires_epatch_parse)
{
    fw_name = NULL;
    requires_epatch_parse = true;

    switch (lmp_subversion) {
        case 0x1200:
            fw_name = "rtl8723a_fw.bin";
            requires_epatch_parse = false;
            return true;
        case 0x8723:
        case 0x8b23:
            fw_name = "rtl8723b_fw.bin";
            return true;
        case 0x8761:
            fw_name = "rtl8761a_fw.bin";
            return true;
        case 0x8821:
            fw_name = "rtl8821a_fw.bin";
            return true;
        case 0x8c10:
        case 0xa10c:
        case 0xb721:
            fw_name = "rtl8821c_fw.bin";
            return true;
        case 0x8822:
            fw_name = "rtl8822b_fw.bin";
            return true;
        case 0xd723:
            fw_name = "rtl8723d_fw.bin";
            return true;
        case 0x8852:
            fw_name = "rtl8852bu_fw.bin";
            return true;
        default:
            return false;
    }
}

bool BtRtl::
initWithDevice(IOService *client, IOUSBHostDevice *dev)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    if (!super::init()) {
        return false;
    }
    
    m_pUSBDeviceController = new USBDeviceController();
    if (!m_pUSBDeviceController->init(client, dev)) {
        return false;
    }
    if (!m_pUSBDeviceController->initConfiguration()) {
        return false;
    }
    if (!m_pUSBDeviceController->findInterface()) {
        return false;
    }
    if (!m_pUSBDeviceController->findPipes()) {
        return false;
    }
    if (!setup()) {
        XYLog("Failed to setup firmware\n");
        // Depending on the desired behavior, you might want to fail initialization
        // return false; 
    }

    return true;
}

void BtRtl::
free()
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    OSSafeReleaseNULL(m_pUSBDeviceController);
    super::free();
}

bool BtRtl::
rtlSendHCISync(HciCommandHdr *cmd, void *event, uint32_t eventBufSize, uint32_t *size, int timeout)
{
//    XYLog("%s cmd: 0x%02x len: %d\n", __PRETTY_FUNCTION__, cmd->opcode, cmd->len);
    IOReturn ret;
    if ((ret = m_pUSBDeviceController->sendHCIRequest(cmd, timeout)) != kIOReturnSuccess) {
        XYLog("%s sendHCIRequest failed: %s %d\n", __FUNCTION__, m_pUSBDeviceController->stringFromReturn(ret), ret);
        return false;
    }
    if ((ret = m_pUSBDeviceController->interruptPipeRead(event, eventBufSize, size, timeout)) != kIOReturnSuccess) {
        XYLog("%s interruptPipeRead failed: %s %d\n", __FUNCTION__, m_pUSBDeviceController->stringFromReturn(ret), ret);
        return false;
    }
    return true;
}

bool BtRtl::
rtlSendHCISyncEvent(HciCommandHdr *cmd, void *event, uint32_t eventBufSize, uint32_t *size, uint8_t syncEvent, int timeout)
{
    IOReturn ret;
    if ((ret = m_pUSBDeviceController->sendHCIRequest(cmd, timeout)) != kIOReturnSuccess) {
        XYLog("%s sendHCIRequest failed: %s %d\n", __FUNCTION__, m_pUSBDeviceController->stringFromReturn(ret), ret);
        return false;
    }
    do {
        ret = m_pUSBDeviceController->interruptPipeRead(event, eventBufSize, size, timeout);
        if (ret != kIOReturnSuccess) {
            XYLog("%s interruptPipeRead failed: %s %d\n", __FUNCTION__, m_pUSBDeviceController->stringFromReturn(ret), ret);
            break;
        }
        if (*(uint8_t *)event == syncEvent) {
            return true;
        }
    } while (true);
    return false;
}

bool BtRtl::
rtlBulkHCISync(HciCommandHdr *cmd, void *event, uint32_t eventBufSize, uint32_t *size, int timeout)
{
//    XYLog("%s cmd: 0x%02x len: %d\n", __FUNCTION__, cmd->opcode, cmd->len);
    IOReturn ret;
    if ((ret = m_pUSBDeviceController->bulkWrite(cmd, HCI_COMMAND_HDR_SIZE + cmd->len, timeout)) != kIOReturnSuccess) {
        XYLog("%s bulkWrite failed: %s %d\n", __FUNCTION__, m_pUSBDeviceController->stringFromReturn(ret), ret);
        return false;
    }
    if ((ret = m_pUSBDeviceController->bulkPipeRead(event, eventBufSize, size, timeout)) != kIOReturnSuccess) {
        XYLog("%s bulkPipeRead failed: %s %d\n", __FUNCTION__, m_pUSBDeviceController->stringFromReturn(ret), ret);
        return false;
    }
    return true;
}

bool BtRtl::
securedSend(uint8_t fragmentType, uint32_t len, const uint8_t *fragment)
{
    bool ret = true;
    uint8_t buf[CMD_BUF_MAX_SIZE];
    HciCommandHdr *hciCommand = (HciCommandHdr *)buf;
    
    while (len > 0) {
        uint8_t fragment_len = (len > 252) ? 252 : len;
        
        memset(buf, 0, sizeof(buf));
        hciCommand->opcode = OSSwapHostToLittleInt16(0xfc09); // FIXME: This needs to be changed to Realtek specific
        hciCommand->len = fragment_len + 1;
        hciCommand->data[0] = fragmentType;
        memcpy(hciCommand->data + 1, fragment, fragment_len);
        
        if (!(ret = rtlBulkHCISync(hciCommand, NULL, 0, NULL, HCI_INIT_TIMEOUT))) {
            XYLog("secure send failed\n");
            return ret;
        }
        
        len -= fragment_len;
        fragment += fragment_len;
    }
    
    return ret;
}

bool BtRtl::
rtlVersionInfo(RtlVersion *ver)
{
    const char *variant;
    
    // FIXME: This entire function needs to be adapted for Realtek devices.
    // The platform and variant checks are Intel-specific.
    
    /* The hardware platform number has a fixed value of 0x37 and
     * for now only accept this single value.
     */
    if (ver->hw_platform != 0x37) {
        XYLog("Unsupported Realtek hardware platform (%u)\n",
              ver->hw_platform);
        return false;
    }
    
    /* Check for supported iBT hardware variants of this firmware
     * loading method.
     *
     * This check has been put in place to ensure correct forward
     * compatibility options when newer hardware variants come along.
     */
    switch (ver->hw_variant) {
        case 0x0b:      /* SfP */
        case 0x0c:      /* WsP */
        case 0x11:      /* JfP */
        case 0x12:      /* ThP */
        case 0x13:      /* HrP */
        case 0x14:      /* CcP */
            break;
        default:
            XYLog("Unsupported Realtek hardware variant (%u)\n",
                  ver->hw_variant);
            return false;
    }
    
    switch (ver->fw_variant) {
        case 0x06:
            variant = "Bootloader";
            break;
        case 0x23:
            variant = "Firmware";
            break;
        default:
            XYLog("Unsupported firmware variant(%02x)\n", ver->fw_variant);
            return false;
    }
    
    XYLog("%s revision %u.%u build %u week %u %u\n",
          variant, ver->fw_revision >> 4, ver->fw_revision & 0x0f,
          ver->fw_build_num, ver->fw_build_ww,
          2000 + ver->fw_build_yy);
    
    return true;
}

bool BtRtl::
rtlBoot(uint32_t bootAddr)
{
    uint8_t buf[CMD_BUF_MAX_SIZE];
    uint32_t actLen = 0;
    HciResponse *resp = (HciResponse *)buf;
    
    if (!sendRtlReset(bootAddr)) {
        XYLog("Realtek Soft Reset failed\n");
        resetToBootloader();
        return false;
    }
    
    // FIXME: The boot process for Realtek is likely different.
    /* The bootloader will not indicate when the device is ready. This
     * is done by the operational firmware sending bootup notification.
     *
     * Booting into operational firmware should not take longer than
     * 1 second. However if that happens, then just fail the setup
     * since something went wrong.
     */
    IOReturn ret = m_pUSBDeviceController->interruptPipeRead(buf, sizeof(buf), &actLen, 1000);
    if (ret != kIOReturnSuccess || actLen <= 0) {
        XYLog("Realtek boot failed\n");
        if (ret == kIOReturnTimeout) {
            XYLog("Reset to bootloader\n");
            resetToBootloader();
        }
        return false;
    }
    if (resp->evt.evt == 0xff && resp->numCommands == 0x02) {
        XYLog("Notify: Device reboot done\n");
        return true;
    }
    return false;
}

bool BtRtl::
loadDDCConfig(const char *ddcFileName)
{
    const uint8_t *fw_ptr;
    uint8_t buf[CMD_BUF_MAX_SIZE];
    HciCommandHdr *cmd = (HciCommandHdr *)buf;
    
    OSData *fwData = requestFirmwareData(ddcFileName);
    
    if (fwData == NULL) {
        XYLog("DDC file not found: %s\n", ddcFileName);
        return false;
    }
    
    XYLog("Load DDC config: %s %d\n", ddcFileName, fwData->getLength());
    
    fw_ptr = (uint8_t *)fwData->getBytesNoCopy();
    
    /* DDC file contains one or more DDC structure which has
     * Length (1 byte), DDC ID (2 bytes), and DDC value (Length - 2).
     */
    while (fwData->getLength() > fw_ptr - (uint8_t *)fwData->getBytesNoCopy()) {
        uint8_t cmd_plen = fw_ptr[0] + sizeof(uint8_t);

        cmd->opcode = OSSwapHostToLittleInt16(0xfc8b); // FIXME: This needs to be changed to Realtek specific
        cmd->len = cmd_plen;
        memcpy(cmd->data, fw_ptr, cmd->len);
        if (!rtlSendHCISync(cmd, NULL, 0, NULL, HCI_INIT_TIMEOUT)) {
            XYLog("Failed to send Realtek_Write_DDC\n");
            return false;
        }

        fw_ptr += cmd_plen;
    }
    OSSafeReleaseNULL(fwData);
    
    XYLog("Load DDC config done\n");
    return true;
}

bool BtRtl::
readRomVersion(uint8_t *version)
{
    uint8_t buf[CMD_BUF_MAX_SIZE];
    HciCommandHdr cmd;
    uint32_t size = 0;

    XYLog("%s\n", __PRETTY_FUNCTION__);

    cmd.opcode = OSSwapHostToLittleInt16(HCI_OP_RTL_READ_ROM_VERSION);
    cmd.len = 0;

    if (!rtlSendHCISync((HciCommandHdr *)&cmd, buf, sizeof(buf), &size, HCI_INIT_TIMEOUT)) {
        XYLog("Failed to read ROM version\n");
        return false;
    }

    // The response is a full HCI command complete event:
    // [evt_code(1), param_len(1), num_cmds(1), opcode(2), status(1), version(1)]
    // Return parameters start after the HciResponse header (sizeof(HciResponse)).
    if (size < sizeof(HciResponse) + sizeof(rtl_rom_version_evt)) {
        XYLog("ROM version event too short (%d bytes)\n", size);
        return false;
    }

    HciResponse *resp = (HciResponse *)buf;
    if (resp->evt.evt != HCI_EV_CMD_COMPLETE) {
        XYLog("Unexpected event 0x%02x waiting for ROM version\n", resp->evt.evt);
        return false;
    }

    rtl_rom_version_evt *evt = (rtl_rom_version_evt *)(buf + sizeof(HciResponse));
    if (evt->status != 0) {
        XYLog("Failed to read ROM version, status: 0x%02x\n", evt->status);
        return false;
    }

    *version = evt->version;
    XYLog("Realtek ROM version: 0x%02x\n", *version);

    return true;
}

bool BtRtl::
readLocalVersion(uint16_t *lmp_subversion)
{
    uint8_t buf[CMD_BUF_MAX_SIZE];
    HciCommandHdr cmd;
    uint32_t size = 0;

    XYLog("%s\n", __PRETTY_FUNCTION__);

    cmd.opcode = OSSwapHostToLittleInt16(HCI_OP_READ_LOCAL_VERSION);
    cmd.len = 0;

    if (!rtlSendHCISync(&cmd, buf, sizeof(buf), &size, HCI_INIT_TIMEOUT)) {
        XYLog("Failed to read local version\n");
        return false;
    }

    // Response: [evt_code(1), param_len(1), num_cmds(1), opcode(2), <HciLocalVersion>]
    if (size < sizeof(HciResponse) + sizeof(HciLocalVersion)) {
        XYLog("Local version event too short (%d bytes)\n", size);
        return false;
    }

    HciResponse *resp = (HciResponse *)buf;
    if (resp->evt.evt != HCI_EV_CMD_COMPLETE) {
        XYLog("Unexpected event 0x%02x waiting for local version\n", resp->evt.evt);
        return false;
    }

    HciLocalVersion *ver = (HciLocalVersion *)(buf + sizeof(HciResponse));
    if (ver->status != 0) {
        XYLog("Read local version failed, status: 0x%02x\n", ver->status);
        return false;
    }

    *lmp_subversion = OSSwapLittleToHostInt16(ver->lmp_subver);
    XYLog("Realtek LMP subversion: 0x%04x\n", *lmp_subversion);

    return true;
}

OSData *BtRtl::
requestFirmwareData(const char *fwName, bool noWarn)
{
    OSData *data = getFWDescByName(fwName);
    if (!data && !noWarn) {
        XYLog("Firmware not found: %s\n", fwName);
    }
    return data;
}

OSData *BtRtl::
parseFirmware(OSData *firmware, uint8_t rom_version, int project_id)
{
    const uint8_t *fw_ptr = (const uint8_t *)firmware->getBytesNoCopy();
    uint32_t fw_len = firmware->getLength();
    const uint8_t extension_sig[] = { 0x51, 0x04, 0xfd, 0x77 };
    
    XYLog("%s\n", __PRETTY_FUNCTION__);

    if (fw_len <= 8) {
        XYLog("Firmware file is too short\n");
        return NULL;
    }

    bool is_v1 = (memcmp(fw_ptr, RTL_EPATCH_SIGNATURE, sizeof(RTL_EPATCH_SIGNATURE) - 1) == 0);
    bool is_v2 = (memcmp(fw_ptr, RTL_EPATCH_SIGNATURE_V2, sizeof(RTL_EPATCH_SIGNATURE_V2) - 1) == 0);
    if (!is_v1 && !is_v2) {
        XYLog("Unknown firmware signature\n");
        return NULL;
    }

    size_t min_size = is_v1
        ? (sizeof(rtl_epatch_header) + sizeof(extension_sig) + RTL_EXTENSION_INSTRUCTION_SIZE)
        : (sizeof(rtl_epatch_header_v2) + sizeof(extension_sig) + RTL_EXTENSION_INSTRUCTION_SIZE);
    if (fw_len < min_size) {
        XYLog("Firmware file is too short for epatch metadata\n");
        return NULL;
    }

    // Parse extension instructions from tail to discover project_id.
    const uint8_t *tail = fw_ptr + fw_len - sizeof(extension_sig);
    if (memcmp(tail, extension_sig, sizeof(extension_sig)) != 0) {
        XYLog("Extension section signature mismatch\n");
        return NULL;
    }
    int found_project_id = -1;
    const uint8_t *scan = tail;
    const uint8_t *min_scan = fw_ptr + (is_v1 ? sizeof(rtl_epatch_header) : sizeof(rtl_epatch_header_v2));
    while (scan >= min_scan + RTL_EXTENSION_INSTRUCTION_SIZE) {
        const uint8_t *instr = scan - RTL_EXTENSION_INSTRUCTION_SIZE;
        if (instr < min_scan) {
            XYLog("Corrupted extension instructions (underflow)\n");
            return NULL;
        }
        uint8_t data = instr[0];
        uint8_t length = instr[1];
        uint8_t opcode = instr[2];
        scan = instr;

        if (opcode == 0xff) { // EOF
            break;
        }
        if (length == 0) {
            XYLog("Invalid extension instruction length 0\n");
            return NULL;
        }
        if (opcode == 0x00 && length == 1) {
            found_project_id = data;
            break;
        }
        if ((size_t)(scan - fw_ptr) < length) {
            XYLog("Corrupted extension instructions\n");
            return NULL;
        }
        scan -= length;
    }

    if (found_project_id < 0) {
        XYLog("Failed to find project id in firmware extension\n");
        return NULL;
    }
    if (project_id >= 0 && found_project_id != project_id) {
        XYLog("Project id mismatch: expected %d got %d\n", project_id, found_project_id);
        return NULL;
    }

    // Version 1 Firmware Format
    if (is_v1) {
        XYLog("Found V1 firmware signature\n");
        
        const rtl_epatch_header *header = (const rtl_epatch_header *)fw_ptr;
        uint16_t num_patches = OSSwapLittleToHostInt16(header->num_patches);
        
        XYLog("FW version: 0x%08x, patches: %d\n", OSSwapLittleToHostInt32(header->fw_version), num_patches);

        // Logic to find the correct patch based on rom_version
        // This is a simplified version of the logic in rtlbt_parse_firmware
        const uint8_t *chip_id_base = fw_ptr + sizeof(rtl_epatch_header);
        const uint8_t *patch_length_base = chip_id_base + (sizeof(uint16_t) * num_patches);
        const uint8_t *patch_offset_base = patch_length_base + (sizeof(uint16_t) * num_patches);
        
        uint32_t patch_offset = 0;
        uint16_t patch_length = 0;

        for (int i = 0; i < num_patches; i++) {
            uint16_t chip_id = OSReadLittleInt16(chip_id_base, i * sizeof(uint16_t));
            if (chip_id == rom_version + 1) {
                patch_length = OSReadLittleInt16(patch_length_base, i * sizeof(uint16_t));
                patch_offset = OSReadLittleInt32(patch_offset_base, i * sizeof(uint32_t));
                break;
            }
        }

        if (patch_offset == 0) {
            XYLog("Failed to find patch for ROM version 0x%02x\n", rom_version);
            return NULL;
        }

        XYLog("Found patch for ROM version 0x%02x at offset 0x%x with length %d\n", rom_version, patch_offset, patch_length);

        if (fw_len < patch_offset + patch_length) {
            XYLog("Firmware file is too short for the patch\n");
            return NULL;
        }

        // Linux behavior: copy patch and replace last 4 bytes with fw_version.
        if (patch_length < 4) {
            XYLog("Patch length too short\n");
            return NULL;
        }
        OSData *patch = OSData::withCapacity(patch_length);
        if (!patch) {
            return NULL;
        }
        patch->appendBytes(fw_ptr + patch_offset, patch_length - sizeof(header->fw_version));
        patch->appendBytes(&header->fw_version, sizeof(header->fw_version));
        return patch;

    } 
    // Version 2 Firmware Format
    else if (is_v2) {
        XYLog("Found V2 firmware signature\n");
        const rtl_epatch_header_v2 *hdr = (const rtl_epatch_header_v2 *)fw_ptr;
        uint32_t num_sections = OSSwapLittleToHostInt32(hdr->num_sections);
        XYLog("V2 sections: %u\n", num_sections);

        struct PatchPart {
            const uint8_t *data;
            uint32_t len;
            uint8_t prio;
        };
        PatchPart parts[RTL_V2_MAX_PATCH_PARTS];
        uint32_t part_count = 0;
        uint32_t total_len = 0;

        const uint8_t *cursor = fw_ptr + sizeof(rtl_epatch_header_v2);
        const uint8_t *fw_end = fw_ptr + fw_len;
        const uint8_t *section_limit = fw_ptr + fw_len - RTL_EXTENSION_TAIL_SIZE; // Skip extension tail.

        for (uint32_t s = 0; s < num_sections; s++) {
            if (cursor + sizeof(rtl_section) > fw_end) {
                XYLog("Invalid section header in v2\n");
                return NULL;
            }

            const rtl_section *section = (const rtl_section *)cursor;
            uint32_t opcode = OSSwapLittleToHostInt32(section->opcode);
            uint32_t section_len = OSSwapLittleToHostInt32(section->len);
            cursor += sizeof(rtl_section);

            if (cursor + section_len > section_limit) {
                XYLog("Invalid section length in v2\n");
                return NULL;
            }

            if (opcode == RTL_PATCH_SNIPPETS || opcode == RTL_PATCH_DUMMY_HEADER || opcode == RTL_PATCH_SECURITY_HEADER) {
                const uint8_t *sp = cursor;
                const uint8_t *slimit = cursor + section_len;
                if (sp + sizeof(rtl_section_hdr) > slimit) {
                    XYLog("Invalid subsection header in v2\n");
                    return NULL;
                }
                const rtl_section_hdr *sh = (const rtl_section_hdr *)sp;
                uint16_t sub_count = OSSwapLittleToHostInt16(sh->num);
                sp += sizeof(rtl_section_hdr);

                for (uint16_t i = 0; i < sub_count; i++) {
                    uint8_t eco = 0;
                    uint8_t prio = 0;
                    uint32_t sub_len = 0;
                    const uint8_t *sub_data = NULL;

                    if (opcode == RTL_PATCH_SECURITY_HEADER) {
                        if (sp + sizeof(rtl_sec_hdr) > slimit) {
                            XYLog("V2 section parse stopped: rtl_sec_hdr out of bounds\n");
                            break;
                        }
                        const rtl_sec_hdr *sec = (const rtl_sec_hdr *)sp;
                        eco = sec->eco;
                        prio = sec->prio;
                        sub_len = OSSwapLittleToHostInt32(sec->len);
                        sp += sizeof(rtl_sec_hdr);
                    } else {
                        if (sp + sizeof(rtl_common_subsec) > slimit) {
                            XYLog("V2 section parse stopped: rtl_common_subsec out of bounds\n");
                            break;
                        }
                        const rtl_common_subsec *common = (const rtl_common_subsec *)sp;
                        eco = common->eco;
                        prio = common->prio;
                        sub_len = OSSwapLittleToHostInt32(common->len);
                        sp += sizeof(rtl_common_subsec);
                    }

                    if (sp + sub_len > slimit) {
                        XYLog("V2 section parse stopped: subsection data out of bounds\n");
                        break;
                    }
                    sub_data = sp;
                    sp += sub_len;

                    if (eco != (uint8_t)(rom_version + 1)) {
                        continue;
                    }
                    if (part_count >= RTL_V2_MAX_PATCH_PARTS) {
                        XYLog("Too many v2 patch parts (max %u)\n", RTL_V2_MAX_PATCH_PARTS);
                        return NULL;
                    }

                    uint32_t insert = part_count;
                    while (insert > 0 && parts[insert - 1].prio > prio) {
                        parts[insert] = parts[insert - 1];
                        insert--;
                    }
                    parts[insert] = { sub_data, sub_len, prio };
                    part_count++;
                    total_len += sub_len;
                }
            }
            cursor += section_len;
        }

        if (part_count == 0 || total_len == 0) {
            XYLog("No matching v2 patch parts for ROM version 0x%02x\n", rom_version);
            return NULL;
        }

        OSData *patch = OSData::withCapacity(total_len);
        if (!patch) {
            return NULL;
        }
        for (uint32_t i = 0; i < part_count; i++) {
            patch->appendBytes(parts[i].data, parts[i].len);
        }
        XYLog("Built V2 patch with %u parts (%u bytes)\n", part_count, total_len);
        return patch;
    } 
    else {
        XYLog("Unknown firmware signature\n");
        return NULL;
    }
}

bool BtRtl::
downloadFirmware(OSData *firmwarePatch)
{
    const uint8_t *patch_data = (const uint8_t *)firmwarePatch->getBytesNoCopy();
    uint32_t patch_len = firmwarePatch->getLength();
    uint8_t frag_index = 0;

    XYLog("%s: patch_len %d\n", __PRETTY_FUNCTION__, patch_len);

    uint32_t remaining = patch_len;
    for (uint32_t offset = 0; offset < patch_len; offset += RTL_FRAG_LEN) {
        uint8_t buf[CMD_BUF_MAX_SIZE];
        HciCommandHdr *hci_cmd = (HciCommandHdr *)buf;
        rtl_download_cmd *dl_cmd = (rtl_download_cmd *)(hci_cmd->data);

        uint32_t frag_len = (remaining < RTL_FRAG_LEN) ? remaining : RTL_FRAG_LEN;
        bool last_fragment = (frag_len == remaining);

        uint8_t send_index = frag_index;
        if (last_fragment) {
            send_index |= 0x80; // Set the final fragment flag
        }

        hci_cmd->opcode = OSSwapHostToLittleInt16(HCI_OP_RTL_DOWNLOAD_FW);
        hci_cmd->len = 1 + frag_len; // index byte + firmware data
        dl_cmd->index = send_index;
        memcpy(dl_cmd->data, patch_data + offset, frag_len);

        uint8_t resp_buf[CMD_BUF_MAX_SIZE];
        uint32_t resp_size = 0;
        if (!rtlSendHCISync(hci_cmd, resp_buf, sizeof(resp_buf), &resp_size, HCI_INIT_TIMEOUT)) {
            XYLog("Failed to send firmware fragment index %d\n", frag_index & 0x7f);
            return false;
        }

        XYLog("Downloaded fragment index %d (%d bytes)\n", frag_index & 0x7f, frag_len);

        if (last_fragment) {
            XYLog("Firmware download complete.\n");
            break;
        }

        frag_index++;
        remaining -= frag_len;
    }

    return true;
}



bool BtRtl::setupFirmware()
{
    uint8_t rom_version = 0;
    uint16_t lmp_subversion = 0;
    int project_id = -1;
    const char *fw_name = NULL;
    OSData *fw_data = NULL;
    OSData *fw_patch = NULL;
    bool requires_epatch_parse = true;

    XYLog("%s\n", __PRETTY_FUNCTION__);

    // 1. Read LMP subversion to identify the Realtek chip.
    //    lmp_subver from HCI_Read_Local_Version_Information encodes the chip model.
    if (!readLocalVersion(&lmp_subversion)) {
        XYLog("Failed to read local version\n");
        return false;
    }

    // 2. Select firmware file based on chip model.
    //    Firmware files come from linux-firmware.git/rtl_bt/
    if (!firmwareNameFromLmpSubversion(lmp_subversion, fw_name, requires_epatch_parse)) {
        XYLog("Unsupported Realtek chip lmp_subversion 0x%04x\n", lmp_subversion);
        return false;
    }

    // 3. Read ROM version only for epatch-based firmware.
    if (requires_epatch_parse && !readRomVersion(&rom_version)) {
        XYLog("Failed to read ROM version\n");
        return false;
    }

    project_id = rtlExpectedProjectId(lmp_subversion);
    XYLog("Chip 0x%04x, ROM 0x%02x, firmware: %s\n", lmp_subversion, rom_version, fw_name);
    XYLog("Expected project id: %d\n", project_id);

    // 4. Load firmware from embedded data.
    fw_data = getFWDescByName(fw_name);
    if (!fw_data) {
        XYLog("Firmware '%s' not embedded. Run scripts/generate_fw_data.py first.\n", fw_name);
        return false;
    }

    // 5. Parse firmware to extract the patch for this ROM version.
    if (requires_epatch_parse) {
        fw_patch = parseFirmware(fw_data, rom_version, project_id);
    } else {
        fw_patch = OSData::withBytes(fw_data->getBytesNoCopy(), fw_data->getLength());
    }
    fw_data->release();

    if (!fw_patch) {
        XYLog("Failed to parse firmware\n");
        return false;
    }

    // 6. Download the patch to the device.
    if (!downloadFirmware(fw_patch)) {
        XYLog("Failed to download firmware patch\n");
        fw_patch->release();
        return false;
    }

    fw_patch->release();
    XYLog("Firmware setup completed successfully!\n");
    return true;
}
