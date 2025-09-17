#include "RtlBluetoothOps.hpp"
#include "RealtekBluetoothFirmware.hpp"
#include "Log.h"

// Define the metadata for our new class
OSDefineMetaClassAndStructors(RealtekBluetoothFirmware, IOService)

// A structure to hold the device matching information
struct USBDeviceID {
    uint16_t vendorID;
    uint16_t productID;
};

// This is the new home for the list of supported devices.
// It's moved from Info.plist to be more like IntelBluetoothFirmware.
// You can add new devices here.
static const USBDeviceID supportedDevices[] = {
    { 0x0BDA, 0x8761 }, // 34657
    { 0x0BDA, 0x8821 }, // 34849
    { 0x0BDA, 0xB720 }, // 46880
    { 0x0BDA, 0xB723 }, // 46883
    { 0x0BDA, 0xB728 }, // 46888
    { 0x0BDA, 0xB822 }, // 47138
    { 0x0BDA, 0xC821 }, // 51233
    { 0x0BDA, 0xC82C }, // 51244
    { 0x0BDA, 0xD723 }, // 55075
    { 0x0BDA, 0x1724 }  // 5924
    // Add new { VendorID, ProductID } pairs here
};

IOService *RealtekBluetoothFirmware::probe(IOService *provider, SInt32 *score)
{
    // Call the parent's probe method first
    if (super::probe(provider, score) != this) {
        return nullptr;
    }

    // Cast the provider to a USB device
    m_pUSBDevice = OSDynamicCast(IOUSBHostDevice, provider);
    if (!m_pUSBDevice) {
        XYLog("Provider is not an IOUSBHostDevice\n");
        return nullptr;
    }

    // Get the VID and PID from the device
    uint16_t vendorID = m_pUSBDevice->getDeviceDescriptor()->idVendor;
    uint16_t productID = m_pUSBDevice->getDeviceDescriptor()->idProduct;

    // Check if the device is in our supported list
    for (int i = 0; i < sizeof(supportedDevices) / sizeof(USBDeviceID); i++) {
        if (vendorID == supportedDevices[i].vendorID && productID == supportedDevices[i].productID) {
            XYLog("Found a supported Realtek device: VID=0x%04x, PID=0x%04x\n", vendorID, productID);
            
            // Increase the probe score to make sure this driver is chosen
            *score += 2000;
            
            return this;
        }
    }

    // Not a supported device
    return nullptr;
}

bool RealtekBluetoothFirmware::start(IOService *provider)
{
    XYLog("Starting RealtekBluetoothFirmware driver\n");

    if (!super::start(provider)) {
        XYLog("super::start() failed\n");
        return false;
    }

    // The provider should be the same USB device we found in probe()
    m_pUSBDevice = OSDynamicCast(IOUSBHostDevice, provider);
    if (!m_pUSBDevice) {
        XYLog("Provider is not an IOUSBHostDevice in start()\n");
        return false;
    }

    // Now, create the controller object which will handle the actual work.
    // This is where we delegate the task to the old BtRtl class.
    m_pController = new BtRtl();
    if (!m_pController) {
        XYLog("Failed to allocate BtRtl controller\n");
        return false;
    }

    // Initialize the controller with the USB device
    if (!m_pController->initWithDevice(this, m_pUSBDevice)) {
        XYLog("Failed to initialize BtRtl controller\n");
        OSSafeReleaseNULL(m_pController);
        return false;
    }

    XYLog("RealtekBluetoothFirmware driver started successfully\n");
    return true;
}

void RealtekBluetoothFirmware::stop(IOService *provider)
{
    XYLog("Stopping RealtekBluetoothFirmware driver\n");

    // Clean up the controller object
    if (m_pController) {
        m_pController->release();
        m_pController = nullptr;
    }

    super::stop(provider);
}
