# RealtekBluetoothFirmware
A macOS Bluetooth firmware loader for Realtek USB controllers.

## Goal
- Detect supported Realtek USB Bluetooth adapters (VID/PID).
- Read chip information (LMP subversion + ROM version).
- Select the correct embedded `rtl_bt/*.bin` firmware.
- Parse Realtek epatch firmware (v1 and v2) and download it over HCI.

## Firmware source
You already downloaded firmware `.bin` files from:
- https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/rtl_bt

Put the required files in:
- `./RealtekBluetoothFirmware/fw`

No runtime download is needed; firmware is embedded into the kext source.

## Generate embedded firmware table
From repo root:

```bash
cd scripts
python3 generate_fw_data.py
```

This regenerates:
- `./RealtekBluetoothFirmware/FwRtl.cpp`

## Notes
- The script only includes known Realtek Bluetooth `rtl*_fw.bin` / config files.
- If firmware is missing in `FwRtl.cpp`, setup will log:
  - `Firmware '<name>' not embedded. Run scripts/generate_fw_data.py first.`

## Build and test on macOS
- This repository currently does not include an `.xcodeproj` / `.xcworkspace`.
- It contains kext source code, so it is not directly runnable from Xcode like a normal app project.

Recommended workflow:
1. Prepare firmware data (required before build):
   ```bash
   cd scripts
   python3 generate_fw_data.py
   ```
2. Use a compatible kext Xcode project (from an upstream source) or create your own kext target in Xcode.
3. Add this source tree to the kext target and configure:
   - bundle metadata in `RealtekBluetoothFirmware/Info.plist`
   - required dependencies/frameworks for your kext environment
4. Build the target to produce a `.kext` bundle.
5. Load and test via your OpenCore setup (not by pressing Run as a macOS app).
