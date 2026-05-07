#ifndef RealtekBluetoothFirmware_hpp
#define RealtekBluetoothFirmware_hpp

#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/IOService.h>
#include "BtRtl.h"

class RealtekBluetoothFirmware : public IOService {
    OSDeclareDefaultStructors(RealtekBluetoothFirmware)

private:
    /**
     *  The underlying Realtek controller object that handles device-specific operations.
     */
    BtRtl *m_pController;

    /**
     *  The USB device object provided by I/O Kit.
     */
    IOUSBHostDevice *m_pUSBDevice;

public:
    /**
     *  Called by I/O Kit to determine if this driver should attach to the given provider.
     *  We will perform device matching based on VID/PID here.
     */
    virtual IOService *probe(IOService *provider, SInt32 *score) override;

    /**
     *  Called by I/O Kit when the driver is attaching to the provider.
     *  This is where we initialize the controller and start the firmware upload process.
     */
    virtual bool start(IOService *provider) override;

    /**
     *  Called by I/O Kit when the driver is detaching.
     *  We clean up our resources here.
     */
    virtual void stop(IOService *provider) override;
};

#endif /* RealtekBluetoothFirmware_hpp */
