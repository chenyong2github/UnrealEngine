// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#pragma warning(push)
#pragma warning(disable : 4201) // nonstandard extension used: nameless struct/union
#include <d3d12.h>
#pragma warning(pop)

#ifdef __cplusplus
  #include <DirectML.h>
#else
  struct IDMLDevice;
  typedef struct IDMLDevice IDMLDevice;
#endif

// Windows pollutes the macro space, causing a build break in constants.h.
#undef OPTIONAL

#include "onnxruntime_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WITH_UE
/**
 * OrtDMLGPUResourceAllocator allows wrapping of a ID312Resource to DML allocation
 */
typedef struct OrtDMLGPUResourceAllocator {
	uint32_t version;  // Initialize to ORT_API_VERSION
	const struct OrtMemoryInfo* (ORT_API_CALL* GetProviderMemoryInfo)(const struct OrtDMLGPUResourceAllocator* this_);
	void* (ORT_API_CALL* GPUAllocationFromD3DResource)(struct OrtDMLGPUResourceAllocator* this_, void* resource);
	void (ORT_API_CALL* FreeGPUAllocation)(struct OrtDMLGPUResourceAllocator* this_, void* allocation);
} OrtDMLGPUResourceAllocator;

/**
 * Options for the DML provider that are passed to SessionOptionsAppendExecutionProviderWithOptions_DML
 */
typedef struct OrtDMLProviderOptions {
	// Input
	IDMLDevice* dml_device;
	ID3D12CommandQueue* cmd_queue;

	// Output
	OrtDMLGPUResourceAllocator** resource_allocator;
} OrtDMLProviderOptions;
#endif //WITH_UE

/**
 * Creates a DirectML Execution Provider which executes on the hardware adapter with the given device_id, also known as
 * the adapter index. The device ID corresponds to the enumeration order of hardware adapters as given by 
 * IDXGIFactory::EnumAdapters. A device_id of 0 always corresponds to the default adapter, which is typically the 
 * primary display GPU installed on the system. A negative device_id is invalid.
*/
ORT_API_STATUS(OrtSessionOptionsAppendExecutionProvider_DML, _In_ OrtSessionOptions* options, int device_id);

/**
 * Creates a DirectML Execution Provider using the given DirectML device, and which executes work on the supplied D3D12
 * command queue. The DirectML device and D3D12 command queue must have the same parent ID3D12Device, or an error will
 * be returned. The D3D12 command queue must be of type DIRECT or COMPUTE (see D3D12_COMMAND_LIST_TYPE). If this 
 * function succeeds, the inference session maintains a strong reference on both the dml_device and the command_queue 
 * objects.
 * See also: DMLCreateDevice
 * See also: ID3D12Device::CreateCommandQueue
 */
ORT_API_STATUS(OrtSessionOptionsAppendExecutionProviderEx_DML, _In_ OrtSessionOptions* options,
               _In_ IDMLDevice* dml_device, _In_ ID3D12CommandQueue* cmd_queue);

#ifdef WITH_UE
/**
 * Create DirectML Execution Provider with specified options
 */
ORT_EXPORT
ORT_API_STATUS(OrtSessionOptionsAppendExecutionProviderWithOptions_DML, _In_ OrtSessionOptions* options,
	_In_ OrtDMLProviderOptions* provider_options);
#endif //WITH_UE

#ifdef __cplusplus

#ifdef WITH_UE
namespace Ort
{
/**
  * DMLGPUResourceAllocator allows wrapping of a ID312Resource to DML allocation
  */
class DMLGPUResourceAllocator
{
public:

    DMLGPUResourceAllocator(OrtDMLGPUResourceAllocator* allocator = nullptr) : allocator_(allocator)
    {
    }

    const OrtMemoryInfo* GetProviderMemoryInfo() const
    {
        if (allocator_)
            return allocator_->GetProviderMemoryInfo(allocator_);

        return nullptr;
    }

    void* GPUAllocationFromD3DResource(void* resource)
    {
        if (allocator_)
            return allocator_->GPUAllocationFromD3DResource(allocator_, resource);

        return nullptr;
    }

    void FreeGPUAllocation(void* allocation)
    {
        if (allocator_)
            return allocator_->FreeGPUAllocation(allocator_, allocation);
    }

    void SetAllocator(OrtDMLGPUResourceAllocator* allocator)
    {
        allocator_ = allocator;
    }

	OrtDMLGPUResourceAllocator** GetAllocatorAddressOf()
	{
		return &allocator_;
	}

	bool IsValid() const
	{
		return allocator_ != nullptr;
	}

private:

    OrtDMLGPUResourceAllocator* allocator_;
};

} // namspace Ort
#endif // WITH_UE
}
#endif
