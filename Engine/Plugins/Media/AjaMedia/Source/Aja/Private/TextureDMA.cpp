// Copyright Epic Games, Inc. All Rights Reserved.

#include "AJALib.h"
#include "GPUTextureTransfer.h"
#include "GPUTextureTransferModule.h"

namespace AJA_Private
{
	// Placed outside of AJA namespace to not use our version of the EPixelFormat, AJA namespace needs to be re-scoped as well.

	UE::GPUTextureTransfer::FRegisterDMATextureArgs ToGPUTextureTransferStruct(const AJA::FRegisterDMATextureArgs& Args)
	{
		UE::GPUTextureTransfer::FRegisterDMATextureArgs GPUArgs;
		GPUArgs.RHITexture = Args.RHITexture;
		GPUArgs.RHIResourceMemory = Args.RHIResourceMemory;

		if (Args.RHITexture)
		{
			GPUArgs.Width = Args.RHITexture->GetDesc().GetSize().X;
			GPUArgs.Height = Args.RHITexture->GetDesc().GetSize().Y;

			if (Args.RHITexture->GetFormat() == EPixelFormat::PF_B8G8R8A8)
			{
				GPUArgs.PixelFormat = UE::GPUTextureTransfer::EPixelFormat::PF_8Bit;
				GPUArgs.Stride = GPUArgs.Width * 4;
			}
			else if (Args.RHITexture->GetFormat() == EPixelFormat::PF_R32G32B32A32_UINT)
			{
				GPUArgs.PixelFormat = UE::GPUTextureTransfer::EPixelFormat::PF_10Bit;
				GPUArgs.Stride = GPUArgs.Width * 16;
			}
			else
			{
				checkf(false, TEXT("Format not supported"));
			}
		}
		return GPUArgs;
	}
}

namespace AJA
{
	UE::GPUTextureTransfer::ITextureTransfer* TextureTransfer = nullptr;

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
			checkNoEntry();
			break;
		}


		GPUArgs.RHIDevice = Args.RHIDevice;
		GPUArgs.RHICommandQueue = Args.RHICommandQueue;

		// Begin Vulkan Only
		GPUArgs.VulkanInstance = Args.VulkanInstance;
		memcpy(GPUArgs.RHIDeviceUUID, Args.RHIDeviceUUID, 16);

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

	bool InitializeDMA(const FInitializeDMAArgs& Args)
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
			TextureTransfer->RegisterTexture(AJA_Private::ToGPUTextureTransferStruct(Args));
			return true;
		}
		return false;
	}

	bool UnregisterDMATexture(FRHITexture* RHITexture)
	{
		if (TextureTransfer)
		{
			TextureTransfer->UnregisterTexture(RHITexture);
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

	bool LockDMATexture(FRHITexture* RHITexture)
	{
		if (TextureTransfer)
		{
			TextureTransfer->LockTexture(RHITexture);
			return true;
		}
		return false;
	}

	bool UnlockDMATexture(FRHITexture* RHITexture)
	{
		if (TextureTransfer)
		{
			TextureTransfer->UnlockTexture(RHITexture);
			return true;
		}
		return false;
	}
}