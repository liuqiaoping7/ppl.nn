// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "ppl/nn/engines/cuda/data_converter.h"

#include <memory>
#include <functional>

#include "ppl/common/cuda/cuda_types.h"
#include "ppl/nn/common/types.h"
#include "ppl/nn/common/logger.h"
#include "ppl/nn/engines/cuda/cuda_device.h"
#include "cudakernel/reformat/reformat.h"

using namespace std;
using namespace ppl::common;

namespace ppl { namespace nn { namespace cuda {

RetCode CudaDataConverter::ConvertToHost(void* dst, const TensorShape& dst_desc, const BufferDesc& src,
                                         const TensorShape& src_desc) const {
    if (src_desc.GetBytesExcludingPadding() == 0) {
        // TODO release dst
        return RC_SUCCESS;
    }

    BufferDesc tmp_buffer_desc;
    auto status = device_->Realloc(dst_desc, &tmp_buffer_desc);
    if (status != RC_SUCCESS) {
        LOG(ERROR) << "alloc tmp buffer for dst tensor failed";
        return status;
    }
    BufferDescGuard __tmp_buffer_guard__(&tmp_buffer_desc, [this](BufferDesc* buffer) {
        device_->Free(buffer);
    });

    status = Convert(&tmp_buffer_desc, dst_desc, src, src_desc);
    if (status != RC_SUCCESS) {
        LOG(ERROR) << "convert in device failed during ConvertToHost: " << GetRetCodeStr(status);
        return status;
    }

    status = device_->CopyToHost(dst, tmp_buffer_desc, dst_desc);
    if (status != RC_SUCCESS) {
        LOG(ERROR) << "copy dst data to Host failed: " << GetRetCodeStr(status);
        return status;
    }

    return RC_SUCCESS;
}

RetCode CudaDataConverter::ConvertFromHost(BufferDesc* dst, const TensorShape& dst_desc, const void* src,
                                           const TensorShape& src_desc) const {
    if (src_desc.GetBytesExcludingPadding() == 0) {
        device_->Free(dst);
        return RC_SUCCESS;
    }

    BufferDesc tmp_buffer_desc;
    auto status = device_->Realloc(src_desc, &tmp_buffer_desc);
    if (status != RC_SUCCESS) {
        LOG(ERROR) << "alloc tmp buffer for dst tensor failed";
        return status;
    }
    BufferDescGuard __tmp_buffer_guard__(&tmp_buffer_desc, [this](BufferDesc* buffer) {
        device_->Free(buffer);
    });

    status = device_->CopyFromHost(&tmp_buffer_desc, src, src_desc);
    if (status != RC_SUCCESS) {
        LOG(ERROR) << "copy src data from host failed: " << GetRetCodeStr(status);
        return status;
    }

    status = Convert(dst, dst_desc, tmp_buffer_desc, src_desc);
    if (status != RC_SUCCESS) {
        LOG(ERROR) << "convert in device failed during ConvertFromHost: " << GetRetCodeStr(status);
        return status;
    }

    return RC_SUCCESS;
}

RetCode CudaDataConverter::Convert(BufferDesc* dst, const TensorShape& dst_desc, const BufferDesc& src,
                                   const TensorShape& src_desc) const {
    if (src_desc.GetBytesExcludingPadding() == 0) {
        device_->Free(dst);
        return RC_SUCCESS;
    }

    ReFormatParam param;
    auto status = SetReLayoutParam(&param, src_desc, dst_desc);
    if (status != RC_SUCCESS) {
        LOG(ERROR) << "illegal convert layout condition.";
        return status;
    }

    auto src_align_size = ppl::common::cuda::GetDataFormatChannelAlignment(src_desc.GetDataFormat());
    auto dst_align_size = ppl::common::cuda::GetDataFormatChannelAlignment(dst_desc.GetDataFormat());
    if (src_desc.GetDimCount() == 2 && dst_desc.GetDimCount() == 2) {
        if (src_desc.GetDim(1) % src_align_size == 0 && dst_desc.GetDim(1) % dst_align_size == 0) {
            param.in_format = param.out_format;
            param.mix_format = 0;
        }
    }

    if (param.in_format == param.out_format && param.in_type == param.out_type) {
        device_->Copy(dst, src, dst_desc);
    } else if (param.in_format == param.out_format || param.in_type == param.out_type) {
        PPLCUDADataConvert(device_->GetStream(), src.addr, dst->addr, nullptr, param);
    } else {
        auto shape_size = src_desc.GetElementsIncludingPadding() * GetSizeOfDataType(dst_desc.GetDataType());

        BufferDesc tmp_buffer_desc;
        status = device_->Realloc(shape_size, &tmp_buffer_desc);
        if (status != RC_SUCCESS) {
            LOG(ERROR) << "alloc tmp buffer for dst tensor failed";
            return status;
        }
        BufferDescGuard __tmp_buffer_guard__(&tmp_buffer_desc, [this](BufferDesc* buffer) {
            device_->Free(buffer);
        });

        PPLCUDADataConvert(device_->GetStream(), src.addr, dst->addr, tmp_buffer_desc.addr, param);
    }

    return RC_SUCCESS;
}

}}} // namespace ppl::nn::cuda
