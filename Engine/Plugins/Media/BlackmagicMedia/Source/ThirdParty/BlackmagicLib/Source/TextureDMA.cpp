// Copyright Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"

#if _WINDOWS
#include "GPUTextureTransferLog.h"
#include "GPUTextureTransfer.h"
#endif

#if _WINDOWS
namespace BlackmagicDesign
{
	GPUTextureTransfer::FInitializeDMAArgs ToGPUTextureTransferStruct(const FInitializeDMAArgs& Args)
	{
		GPUTextureTransfer::FInitializeDMAArgs GPUArgs;
		GPUArgs.RHICommandQueue = Args.RHICommandQueue;

		switch (Args.RHI)
		{
		case ERHI::Cuda:
			GPUArgs.RHI = GPUTextureTransfer::ERHI::Cuda;
			break;
		case ERHI::D3D11:
			GPUArgs.RHI = GPUTextureTransfer::ERHI::D3D11;
			break;
		case ERHI::D3D12:
			GPUArgs.RHI = GPUTextureTransfer::ERHI::D3D12;
			break;
		case ERHI::Vulkan:
			GPUArgs.RHI = GPUTextureTransfer::ERHI::Vulkan;
			break;
		default:
			GPUArgs.RHI = GPUTextureTransfer::ERHI::Invalid;
			break;
		}


		GPUArgs.RHIDevice = Args.RHIDevice;
		GPUArgs.RHICommandQueue = Args.RHICommandQueue;

		// Begin Vulkan Only
		GPUArgs.VulkanInstance = Args.VulkanInstance;
		memcpy(GPUArgs.RHIDeviceUUID, Args.RHIDeviceUUID, 16);

		return GPUArgs;
	}

	GPUTextureTransfer::FRegisterDMATextureArgs ToGPUTextureTransferStruct(const FRegisterDMATextureArgs& Args)
	{
		GPUTextureTransfer::FRegisterDMATextureArgs GPUArgs;
		GPUArgs.RHITexture = Args.RHITexture;
		GPUArgs.RHIResourceMemory = Args.RHIResourceMemory;
		return GPUArgs;
	}

	GPUTextureTransfer::FRegisterDMABufferArgs ToGPUTextureTransferStruct(const FRegisterDMABufferArgs& Args)
	{
		GPUTextureTransfer::FRegisterDMABufferArgs GPUArgs;
		GPUArgs.Buffer = Args.Buffer;
		GPUArgs.Stride = Args.Stride;
		GPUArgs.Width = Args.Width;
		GPUArgs.Height = Args.Height;
		return GPUArgs;
	}

	bool InitializeDMA(const BlackmagicDesign::FInitializeDMAArgs& Args)
	{
		if (TextureTransfer)
		{
			return true;
		}

		GPUTextureTransfer::FInitializeDMAArgs GpuArgs = ToGPUTextureTransferStruct(Args);

		TextureTransfer = GPUTextureTransfer::GetTextureTransfer(GpuArgs);
		return TextureTransfer != nullptr;
	}

	void UninitializeDMA()
	{
		if (TextureTransfer)
		{
			GPUTextureTransfer::CleanupTextureTransfer(TextureTransfer);
			TextureTransfer = nullptr;
		}
	}

	bool RegisterDMATexture(const FRegisterDMATextureArgs& Args)
	{
		if (TextureTransfer)
		{
			TextureTransfer->RegisterTexture(ToGPUTextureTransferStruct(Args));
			return true;
		}
		return false;
	}

	bool UnregisterDMATexture(void* InRHITexture)
	{
		if (TextureTransfer)
		{
			TextureTransfer->UnregisterTexture(InRHITexture);
			return true;
		}
		return false;
	}

	bool RegisterDMABuffer(const FRegisterDMABufferArgs& Args)
	{
		if (TextureTransfer)
		{
			TextureTransfer->RegisterBuffer(ToGPUTextureTransferStruct(Args));
			return true;
		}
		return false;
	}

	bool UnregisterDMABuffer(void* InBuffer)
	{
		if (TextureTransfer)
		{
			TextureTransfer->UnregisterBuffer(InBuffer);
			return true;
		}
		return false;
	}

	bool LockDMATexture(void* InRHITexture)
	{
		if (TextureTransfer)
		{
			TextureTransfer->LockTexture(InRHITexture);
			return true;
		}
		return false;
	}

	bool UnlockDMATexture(void* InRHITexture)
	{
		if (TextureTransfer)
		{
			TextureTransfer->UnlockTexture(InRHITexture);
			return true;
		}
		return false;
	}
}
#else
namespace BlackmagicDesign
{
	bool InitializeDMA(const BlackmagicDesign::FInitializeDMAArgs& Args)
	{
		LOG_ERROR(TEXT("GPU Texture transfer is not available on linux."));
		return false;
	}

	void UninitializeDMA()
	{
	}

	bool RegisterDMATexture(const FRegisterDMATextureArgs& Args)
	{
		return false;
	}

	bool UnregisterDMATexture(void* InRHITexture)
	{
		return false;
	}

	bool RegisterDMABuffer(const FRegisterDMABufferArgs& Args)
	{
		return false;
	}

	bool UnregisterDMABuffer(void* InBuffer)
	{
		return false;
	}

	bool LockDMATexture(void* InRHITexture)
	{
		return false;
	}

	bool UnlockDMATexture(void* InRHITexture)
	{
		return false;
	}
}
#endif
