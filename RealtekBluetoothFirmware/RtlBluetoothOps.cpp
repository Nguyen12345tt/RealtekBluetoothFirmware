//
//  RtlBluetoothOps.cpp
//  RtlBluetoothFirmware
//
//  Created by GitHub Copilot on 2025/09/16.
//

#include "RtlBluetoothOps.hpp"
#include "Log.h"

OSDefineMetaClassAndStructors(RtlBluetoothOps, BtRtl)

bool RtlBluetoothOps::setup() {
    XYLog("RtlBluetoothOps: setup firmware start\n");
    return setupFirmware();
}

bool RtlBluetoothOps::shutdown() {
    XYLog("RtlBluetoothOps: shutdown -> HCI reset\n");
    return resetToBootloader();
}

bool RtlBluetoothOps::getFirmwareName(char *fwname, size_t len) {
    uint16_t lmp_subversion = 0;
    if (!readLocalVersion(&lmp_subversion)) {
        XYLog("RtlBluetoothOps: failed to read local version for firmware name\n");
        return false;
    }

    const char *name = NULL;
    bool requires_epatch_parse = true;
    if (!firmwareNameFromLmpSubversion(lmp_subversion, name, requires_epatch_parse)) {
        XYLog("RtlBluetoothOps: unsupported lmp_subversion 0x%04x\n", lmp_subversion);
        return false;
    }

    strlcpy(fwname, name, len);
    XYLog("RtlBluetoothOps: selected firmware %s for chip 0x%04x\n", fwname, lmp_subversion);
    return true;
}
