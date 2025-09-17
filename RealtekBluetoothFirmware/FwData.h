/** @file
  Copyright (c) 2020 zxystd. All rights reserved.
  SPDX-License-Identifier: GPL-3.0-only
**/

//
//  FwData.h
//  IntelBluetoothFirmware
//
//  Created by zxystd on 2019/12/22.
//  Copyright © 2019 zxystd. All rights reserved.
//

#ifndef FwData_h
#define FwData_h
#include <string.h>
#include <libkern/c++/OSData.h>
#include <libkern/zlib.h>
#include <zutil.h>

struct FwDesc {
    const char *name;
    const unsigned char *var;
    const long int size; // Kích thước của dữ liệu đã nén
    const bool compressed;
    const long int uncompressed_size; // Kích thước của dữ liệu gốc
};

#define IBT_FW(fw_name, fw_var, fw_size, fw_compressed, fw_uncompressed_size) \
    .name = fw_name, .var = fw_var, .size = fw_size, .compressed = fw_compressed, .uncompressed_size = fw_uncompressed_size


extern const struct FwDesc fwList[];
extern const int fwNumber;

// Khai báo trước hàm uncompressFirmware để getFWDescByName có thể sử dụng
static inline bool uncompressFirmware(unsigned char *dest, uint *destLen, unsigned char *source, uint sourceLen);

static inline OSData *getFWDescByName(const char* name) {
    for (int i = 0; i < fwNumber; i++) {
        if (strcmp(fwList[i].name, name) == 0) {
            FwDesc desc = fwList[i];
            
            if (desc.compressed) {
                // Nếu firmware được nén, hãy giải nén nó
                uint destLen = (uint)desc.uncompressed_size;
                // Cấp phát bộ đệm cho dữ liệu đã giải nén
                unsigned char* uncompressed_data = (unsigned char*)IOMalloc(destLen);
                if (!uncompressed_data) {
                    // Xử lý lỗi không cấp phát được bộ nhớ
                    return NULL;
                }
                
                // Gọi hàm giải nén
                if (uncompressFirmware(uncompressed_data, &destLen, (unsigned char*)desc.var, (uint)desc.size)) {
                    // Tạo đối tượng OSData từ dữ liệu đã giải nén
                    OSData* data = OSData::withBytes(uncompressed_data, destLen);
                    // Giải phóng bộ đệm đã cấp phát
                    IOFree(uncompressed_data, desc.uncompressed_size);
                    return data;
                } else {
                    // Xử lý lỗi giải nén
                    IOFree(uncompressed_data, desc.uncompressed_size);
                    return NULL;
                }
            } else {
                // Nếu không được nén, trả về trực tiếp
                return OSData::withBytes(desc.var, (unsigned int)desc.size);
            }
        }
    }
    return NULL;
}

static inline bool uncompressFirmware(unsigned char *dest, uint *destLen, unsigned char *source, uint sourceLen)
{
    z_stream stream;
    int err;
    
    stream.next_in = source;
    stream.avail_in = sourceLen;
    stream.next_out = dest;
    stream.avail_out = *destLen;
    stream.zalloc = zcalloc;
    stream.zfree = zcfree;
    err = inflateInit(&stream);
    if (err != Z_OK) {
        return false;
    }
    err = inflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END) {
        inflateEnd(&stream);
        return false;
    }
    *destLen = (uint)stream.total_out;

    err = inflateEnd(&stream);
    return err == Z_OK;
}

#endif /* FwData_h */
