// Copyright Epic Games, Inc. All Rights Reserved.

#include "Common.h"
#include "CoreMinimal.h"

#if PLATFORM_WINDOWS
#include "GPUTextureTransfer.h"
#endif

#if PLATFORM_WINDOWS
namespace BlackmagicDesign
{
	UE::GPUTextureTransfer::FInitializeDMAArgs ToGPUTextureTransferStruct(const FInitializeDMAArgs& Args)
	{
		UE::GPUTextureTransfer::FInitializeDMAArgs GPUArgs;
		GPUArgs.RHICommandQueue = Args.RHICommandQueue;

		switch (Args.RHI)
		{
		case ERHI::D3D11:
			GPUArgs.RHI = UE::GPUTextureTransfer::ERHI::D3D11;
			break;
		case ERHI::D3D12:
			GPUArgs.RHI = UE::GPUTextureTransfer::ERHI::D3D12;
			break;
		case ERHI::Vulkan:
			GPUArgs.RHI = UE::GPUTextureTransfer::ERHI::Vulkan;
			break;
		default:
			GPUArgs.RHI = UE::GPUTextureTransfer::ERHI::Invalid;
			break;
		}

		GPUArgs.RHIDevice = Args.RHIDevice;
		GPUArgs.RHICommandQueue = Args.RHICommandQueue;

		// Begin Vulkan Only
		GPUArgs.VulkanInstance = Args.VulkanInstance;
		memcpy(GPUArgs.RHIDeviceUUID, Args.RHIDeviceUUID, 16);

		return GPUArgs;
	}

	UE::GPUTextureTransfer::FRegisterDMATextureArgs ToGPUTextureTransferStruct(const FRegisterDMATextureArgs& Args)
	{
		UE::GPUTextureTransfer::FRegisterDMATextureArgs GPUArgs;
		GPUArgs.RHITexture = Args.RHITexture;
		GPUArgs.RHIResourceMemory = Args.RHIResourceMemory;
		return GPUArgs;
	}

	UE::GPUTextureTransfer::FRegisterDMABufferArgs ToGPUTextureTransferStruct(const FRegisterDMABufferArgs& Args)
	{
		UE::GPUTextureTransfer::FRegisterDMABufferArgs GPUArgs;
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

		UE::GPUTextureTransfer::FInitializeDMAArgs GpuArgs = ToGPUTextureTransferStruct(Args);

		TextureTransfer = UE::GPUTextureTransfer::GetTextureTransfer(GpuArgs);
		return TextureTransfer != nullptr;
	}

	void UninitializeDMA()
	{
		if (TextureTransfer)
		{
			UE::GPUTextureTransfer::CleanupTextureTransfer(TextureTransfer);
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

	bool UnregisterDMATexture(FRHITexture* InRHITexture)
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

	bool LockDMATexture(FRHITexture* InRHITexture)
	{
		if (TextureTransfer)
		{
			TextureTransfer->LockTexture(InRHITexture);
			return true;
		}
		return false;
	}

	bool UnlockDMATexture(FRHITexture* InRHITexture)
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
		UE_LOG(LogBlackmagicCore, Error,TEXT("GPU Texture transfer is not available on linux."));
		return false;
	}

	void UninitializeDMA()
	{
	}

	bool RegisterDMATexture(const FRegisterDMATextureArgs& Args)
	{
		return false;
	}

	bool UnregisterDMATexture(FRHITexture* InRHITexture)
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

	bool LockDMATexture(FRHITexture* InRHITexture)
	{
		return false;
	}

	bool UnlockDMATexture(FRHITexture* InRHITexture)
	{
		return false;
	}
}
#endif
