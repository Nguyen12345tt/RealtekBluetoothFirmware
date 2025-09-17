/** @file
  Copyright (c) 2020 zxystd. All rights reserved.
  SPDX-License-Identifier: GPL-3.0-only
**/

#include "FwData.h"
#include "Log.h"
#include "BtRtl.h"
// ================================================================================
//
//  QUAN TRỌNG: Hướng dẫn nhúng Firmware
//
//  1. Tìm các tệp firmware (.bin) từ driver Linux cho thiết bị Realtek của bạn.
//
//  2. Dùng lệnh 'xxd' trong Terminal để chuyển đổi mỗi tệp .bin thành một tệp .h:
//     xxd -i rtl8723b_fw.bin > rtl8723b_fw.h
//     xxd -i rtl8821c_fw.bin > rtl8821c_fw.h
//
//  3. Sao chép các tệp .h đã tạo vào thư mục project này.
//
//  4. Include các tệp .h đó vào bên dưới.
//
//  5. Thêm một mục mới vào 'fwList' cho mỗi firmware.
//
// ================================================================================


// BƯỚC 4: Include các tệp header firmware của bạn tại đây
// Ví dụ:
#include "rtl8723b_fw.h"
// #include "rtl8821c_fw.h"


// BƯỚC 5: Định nghĩa danh sách firmware của bạn
const struct FwDesc fwList[] = {
    // Ví dụ:
    { RTL_FW("rtl8723b_fw.bin", rtl8723b_fw_bin, rtl8723b_fw_bin_len) },
    // { RTL_FW("rtl8821c_fw.bin", rtl8821c_fw_bin, rtl8821c_fw_bin_len) },
};

// Tự động tính toán tổng số firmware trong danh sách
const int fwNumber = sizeof(fwList) / sizeof(fwList[0]);
