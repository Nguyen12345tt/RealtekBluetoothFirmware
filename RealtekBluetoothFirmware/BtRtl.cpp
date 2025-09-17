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

#include "FwData.h"

#define super OSObject
OSDefineMetaClassAndAbstractStructors(BtRtl, OSObject)

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
    if (!setupFirmware()) {
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
    rtl_rom_version_evt *evt = (rtl_rom_version_evt *)buf;
    uint32_t size = 0;

    XYLog("%s\n", __PRETTY_FUNCTION__);

    cmd.opcode = OSSwapHostToLittleInt16(HCI_OP_RTL_READ_ROM_VERSION);
    cmd.len = 0;

    if (!rtlSendHCISync((HciCommandHdr *)&cmd, buf, sizeof(buf), &size, HCI_INIT_TIMEOUT)) {
        XYLog("Failed to read ROM version\n");
        return false;
    }

    if (size != sizeof(rtl_rom_version_evt)) {
        XYLog("ROM version event length mismatch\n");
        return false;
    }

    if (evt->status != 0) {
        XYLog("Failed to read ROM version, status: 0x%02x\n", evt->status);
        return false;
    }

    *version = evt->version;
    XYLog("Realtek ROM version: 0x%02x\n", *version);

    return true;
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

    // Version 1 Firmware Format
    if (memcmp(fw_ptr, RTL_EPATCH_SIGNATURE, sizeof(RTL_EPATCH_SIGNATURE) - 1) == 0) {
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

        // Create a new OSData object with the patch data
        return OSData::withBytes(fw_ptr + patch_offset, patch_length);

    } 
    // Version 2 Firmware Format
    else if (memcmp(fw_ptr, RTL_EPATCH_SIGNATURE_V2, sizeof(RTL_EPATCH_SIGNATURE_V2) - 1) == 0) {
        XYLog("Found V2 firmware signature. Parsing not yet implemented.\n");
        // TODO: Implement V2 parsing logic from rtlbt_parse_firmware_v2
        return NULL;
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
    bool last_fragment = false;

    XYLog("%s: patch_len %d\n", __PRETTY_FUNCTION__, patch_len);

    uint32_t remaining = patch_len;
    for (uint32_t offset = 0; offset < patch_len; offset += 252) {
        rtl_download_cmd cmd;
        uint32_t frag_len = min(remaining, (uint32_t)252);

        if (frag_len == remaining) {
            last_fragment = true;
            frag_index |= 0x80; // Set the final fragment flag
        }
        
        cmd.index = frag_index;
        memcpy(cmd.data, patch_data + offset, frag_len);

        IOReturn ret = rtlSendHCISync(HCI_OP_RTL_DOWNLOAD_FW, &cmd, sizeof(cmd.index) + frag_len);
        if (ret != kIOReturnSuccess) {
            XYLog("Failed to send firmware fragment index %d\n", frag_index & 0x7f);
            return false;
        }

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
    int project_id = 0;
    const char *fw_name = NULL;
    OSData *fw_data = NULL;
    OSData *fw_patch = NULL;

    XYLog("%s\n", __PRETTY_FUNCTION__);

    // 1. Read ROM Version
    if (readRomVersion(&rom_version, &lmp_subversion) != kIOReturnSuccess) {
        XYLog("Failed to read ROM version\n");
        return false;
    }

    // 2. Determine firmware filename based on chip info
    // We use lmp_subversion to identify the chip and select the correct firmware file.
    switch (lmp_subversion) {
        case 0x8723: // RTL8723B, RTL8723D
            fw_name = "rtl8723b_fw.bin";
            // Note: RTL8723D might need "rtl8723d_fw.bin", add if available and needed.
            break;
        case 0x8821: // RTL8821C
            fw_name = "rtw8821c_fw.bin"; // Assuming Wi-Fi and BT firmware have similar names
            break;
        case 0x8703: // RTL8723A
             fw_name = "rtl8723aufw_A.bin";
             break;
        case 0x8192: // RTL8192E
            fw_name = "rtl8192eu_nic.bin";
            break;
        // FIXME: Add more cases for other chips based on their lmp_subversion
        // and the available .bin files in rtlwm/Airportrtlwm/firmware/
        // Example:
        // case 0x8822: // RTL8822B
        //     fw_name = "rtl8822b_fw.bin";
        //     break;
        default:
            XYLog("Unsupported lmp_subversion 0x%04x\n", lmp_subversion);
            return false;
    }

    if (!fw_name) {
        XYLog("Could not determine firmware file name for lmp_subversion 0x%04x\n", lmp_subversion);
        return false;
    }

    XYLog("Chip lmp_subversion 0x%04x, selected firmware: %s\n", lmp_subversion, fw_name);

    // 3. Load firmware file from embedded data
    fw_data = getFWDescByName(fw_name);
    if (!fw_data) {
        XYLog("Failed to load embedded firmware data for %s\n", fw_name);
        return false;
    }

    // 4. Parse firmware to get the patch
    fw_patch = parseFirmware(fw_data, rom_version, project_id);
    fw_data->release(); // Release the full firmware data, we only need the patch now.

    if (!fw_patch) {
        XYLog("Failed to parse firmware and get patch\n");
        return false;
    }

    // 5. Download the patch to the device
    if (!downloadFirmware(fw_patch)) {
        XYLog("Failed to download firmware patch\n");
        fw_patch->release();
        return false;
    }

    fw_patch->release();
    XYLog("Firmware setup completed successfully!\n");
    return true;
}

