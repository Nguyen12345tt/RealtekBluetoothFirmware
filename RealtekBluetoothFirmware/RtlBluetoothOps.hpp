//
//  RtlBluetoothOps.h
//  RtlBluetoothFirmware
//
//  Created by GitHub Copilot on 2025/09/16.
//

#ifndef RtlBluetoothOps_h
#define RtlBluetoothOps_h

#include "BtRtl.h"

class RtlBluetoothOps : public BtRtl {
    OSDeclareDefaultStructors(RtlBluetoothOps)
public:
    virtual bool setup() override;
    virtual bool shutdown() override;
    virtual bool getFirmwareName(char *fwname, size_t len) override;
};

#endif /* RtlBluetoothOps_h */
