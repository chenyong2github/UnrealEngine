// Copyright Epic Games, Inc. All Rights Reserved.

#include "VulkanTextureTransfer.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#endif

#include "DVPAPI.h"
#include "dvpapi_vulkan.h"

#if PLATFORM_WINDOWS
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif


namespace UE::GPUTextureTransfer::Private
{
	DVPStatus FVulkanTextureTransfer::Init_Impl(const FInitializeDMAArgs& InArgs)
	{
		VulkanDevice = (VkDevice)InArgs.RHIDevice;
		VulkanQueue = (VkQueue)InArgs.RHICommandQueue;
		return dvpInitVkDevice(VulkanDevice, 0, (void*)InArgs.RHIDeviceUUID);
	}

	DVPStatus FVulkanTextureTransfer::GetConstants_Impl(uint32* OutBufferAddrAlignment, uint32* OutBufferGPUStrideAlignment, uint32* OutSemaphoreAddrAlignment, uint32* OutSemaphoreAllocSize, uint32* OutSemaphorePayloadOffset, uint32* OutSemaphorePayloadSize) const
	{
		if (!VulkanDevice)
		{
			return DVP_STATUS_ERROR;
		}

		return dvpGetRequiredConstantsVkDevice(OutBufferAddrAlignment, OutBufferGPUStrideAlignment, OutSemaphoreAddrAlignment, OutSemaphoreAllocSize,
			OutSemaphorePayloadOffset, OutSemaphorePayloadSize, VulkanDevice);
	}

	DVPStatus FVulkanTextureTransfer::CloseDevice_Impl() const
	{
		if (!VulkanDevice)
		{
			return DVP_STATUS_ERROR;
		}

		return dvpCloseVkDevice(VulkanDevice);
	}

	DVPStatus FVulkanTextureTransfer::BindBuffer_Impl(DVPBufferHandle InBufferHandle) const
	{
		if (!VulkanDevice)
		{
			return DVP_STATUS_ERROR;
		}

		return dvpBindToVkDevice(InBufferHandle, VulkanDevice);
	}

	DVPStatus FVulkanTextureTransfer::CreateGPUResource_Impl(void* InTexture, FTextureTransferBase::FTextureInfo* OutTextureInfo) const
	{
		if (!OutTextureInfo || !VulkanDevice)
		{
			return DVP_STATUS_ERROR;
		}

		// todo for Vulkan
		checkNoEntry();

		DVPGpuExternalResourceDesc Desc;
		//Desc.width = (uint32) ResourceDesc.Width;
		//Desc.height = (uint32)ResourceDesc.Height;
		//Desc.size = Desc.width * Desc.height * 4;
		Desc.format = DVP_BGRA;
		Desc.type = DVP_UNSIGNED_BYTE;
		Desc.handleType = DVP_OPAQUE_WIN32;
		Desc.external.handle = OutTextureInfo->External.Handle;

		return dvpCreateGPUExternalResourceVkDevice(VulkanDevice, &Desc, &OutTextureInfo->DVPHandle);
	}

	DVPStatus FVulkanTextureTransfer::UnbindBuffer_Impl(DVPBufferHandle InBufferHandle) const
	{
		if (!VulkanDevice)
		{
			return DVP_STATUS_ERROR;
		}

		return dvpUnbindFromVkDevice(InBufferHandle, VulkanDevice);
	}

	DVPStatus FVulkanTextureTransfer::MapBufferWaitAPI_Impl(DVPBufferHandle InHandle) const
	{
		if (!VulkanQueue)
		{
			return DVP_STATUS_ERROR;
		}

		return (dvpMapBufferWaitVk(InHandle, VulkanQueue));
	}

	DVPStatus FVulkanTextureTransfer::MapBufferEndAPI_Impl(DVPBufferHandle InHandle) const
	{
		if (!VulkanQueue)
		{
			return DVP_STATUS_ERROR;
		}

		return (dvpMapBufferEndVk(InHandle, VulkanQueue));
	}
}
