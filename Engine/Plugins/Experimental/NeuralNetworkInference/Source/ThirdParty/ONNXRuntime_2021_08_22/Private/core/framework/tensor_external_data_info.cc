// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "tensor_external_data_info.h"
#include "core/common/common.h"
#include "core/platform/path_lib.h"

#ifdef _WIN32
#include "ThirdPartyWarningDisabler.h" // WITH_UE
NNI_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#include <Windows.h>
NNI_THIRD_PARTY_INCLUDES_END // WITH_UE
#endif
using ::google::protobuf::RepeatedPtrField;
using ::ONNX_NAMESPACE::StringStringEntryProto;

namespace onnxruntime {
Status ExternalDataInfo::Create(const RepeatedPtrField<StringStringEntryProto>& input,
                                std::unique_ptr<ExternalDataInfo>& out) {
  out = std::make_unique<ExternalDataInfo>();
  const int input_size = input.size();
  for (int i = 0; i != input_size; ++i) {
    StringStringEntryProto stringmap = input[i];
    if (!stringmap.has_key())
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "model format error! Need a key for the external data info");
    if (!stringmap.has_value())
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "model format error! Need a value for the external data info");
    if (stringmap.key() == "location" && !stringmap.value().empty()) {
      out->rel_path_ = ToWideString(stringmap.value());
    } else if (stringmap.key() == "offset" && !stringmap.value().empty()) {
      char* end;
#ifdef _WIN32
      out->offset_ = _strtoi64(stringmap.value().c_str(), &end, 10);
#else
      out->offset_ = OrtStrToPtrDiff(stringmap.value().c_str(), &end);
#endif
      if (end != stringmap.value().c_str() + stringmap.value().length())
        return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "parsing ", stringmap.value(), " failed");
    } else if (stringmap.key() == "length" && !stringmap.value().empty()) {
      char* end;
      out->length_ = static_cast<size_t>(OrtStrToPtrDiff(stringmap.value().c_str(), &end));
      if (end != stringmap.value().c_str() + stringmap.value().length())
        return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "parsing ", stringmap.value(), " failed");
    } else if (stringmap.key() == "checksum" && !stringmap.value().empty()) {
      out->checksum_ = stringmap.value();
    } else {
      return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "model format error!");
    }
  }
  if (out->rel_path_.empty()) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "model format error! Missing 'location'");
  }
  return Status::OK();
}
}  // namespace onnxruntime