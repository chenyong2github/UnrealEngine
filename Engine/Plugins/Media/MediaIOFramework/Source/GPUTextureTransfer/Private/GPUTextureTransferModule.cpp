// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPUTextureTransferModule.h"

#include "D3D11TextureTransfer.h"
#include "D3D12TextureTransfer.h"
#include "RHI.h"
#include "DynamicRHI.h"
#include "GenericPlatform/GenericPlatformDriver.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "TextureTransferBase.h"

DEFINE_LOG_CATEGORY(LogGPUTextureTransfer);

namespace 
{
	auto ConvertRHI = [](ERHIInterfaceType RHI)
	{
		switch (RHI)
		{
		case ERHIInterfaceType::D3D11: return UE::GPUTextureTransfer::ERHI::D3D11;
		case ERHIInterfaceType::D3D12: return UE::GPUTextureTransfer::ERHI::D3D12;
		default: return UE::GPUTextureTransfer::ERHI::Invalid;
		}
	};
}

void FGPUTextureTransferModule::StartupModule()
{
	if (LoadGPUDirectBinary())
	{
		bIsGPUTextureTransferAvailable = true;

		const FGPUDriverInfo GPUDriverInfo = FPlatformMisc::GetGPUDriverInfo(GRHIAdapterName);
		bIsGPUTextureTransferAvailable = GPUDriverInfo.IsNVIDIA() && !FModuleManager::Get().IsModuleLoaded("RenderDocPlugin");
		bIsGPUTextureTransferAvailable &= !GPUDriverInfo.DeviceDescription.Contains(TEXT("Tesla"));

		TransferObjects.AddDefaulted(RHI_MAX);

		if (bIsGPUTextureTransferAvailable)
		{
			// todo: Remove this after testing packaged, probably not needed.
			FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(this, &FGPUTextureTransferModule::InitializeTextureTransfer);
			//Same for shutdown, uninitialize ourselves before library is unloaded
			FCoreDelegates::OnEnginePreExit.AddRaw(this, &FGPUTextureTransferModule::UninitializeTextureTransfer);
		}

	}
}

void FGPUTextureTransferModule::ShutdownModule()
{
}

FGPUTextureTransferModule::TextureTransferPtr FGPUTextureTransferModule::GetTextureTransfer()
{
	UE::GPUTextureTransfer::ERHI SupportedRHI = ConvertRHI(RHIGetInterfaceType());
	if (SupportedRHI == UE::GPUTextureTransfer::ERHI::Invalid) 
	{
		UE_LOG(LogGPUTextureTransfer, Error, TEXT("The current RHI is not supported with GPU Texture Transfer."));
		return nullptr;
	}
	
	const uint8 RHIIndex = static_cast<uint8>(SupportedRHI);
	if (TransferObjects[RHIIndex])
	{
		return TransferObjects[RHIIndex];
	}

	return nullptr;
}

bool FGPUTextureTransferModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("GPUTextureTransfer");
}

FGPUTextureTransferModule& FGPUTextureTransferModule::Get()
{
	return FModuleManager::LoadModuleChecked<FGPUTextureTransferModule>("GPUTextureTransfer");
}

bool FGPUTextureTransferModule::LoadGPUDirectBinary()
{
	FString GPUDirectPath = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/NVIDIA/GPUDirect"), FPlatformProcess::GetBinariesSubdirectory());
	FPlatformProcess::PushDllDirectory(*GPUDirectPath);

	FString DVPDll;

#if PLATFORM_WINDOWS
	DVPDll = TEXT("dvp.dll");
#endif

	DVPDll = FPaths::Combine(GPUDirectPath, DVPDll);

	TextureTransferHandle = FPlatformProcess::GetDllHandle(*DVPDll);
	if (TextureTransferHandle == nullptr)
	{
		UE_LOG(LogGPUTextureTransfer, Display, TEXT("Failed to load required library %s. GPU Texture transfer will not be functional."), *DVPDll);
	}

	FPlatformProcess::PopDllDirectory(*GPUDirectPath);

	return !!TextureTransferHandle;
}


void FGPUTextureTransferModule::InitializeTextureTransfer()
{
	ENQUEUE_RENDER_COMMAND(InitializeGPUTextureTransfer)(
	[this](FRHICommandListImmediate& RHICmdList) mutable
	{
		if (!GDynamicRHI)
		{
			return;
		}

		TextureTransferPtr TextureTransfer;

		UE::GPUTextureTransfer::ERHI RHI = ConvertRHI(RHIGetInterfaceType());

		switch (RHI)
		{
		case UE::GPUTextureTransfer::ERHI::D3D11:
			TextureTransfer = MakeShared<UE::GPUTextureTransfer::Private::FD3D11TextureTransfer>();
			break;
		case UE::GPUTextureTransfer::ERHI::D3D12:
			TextureTransfer = MakeShared<UE::GPUTextureTransfer::Private::FD3D12TextureTransfer>();
			break;
		default:
			ensureAlways(false);
			break;
		}

		UE::GPUTextureTransfer::FInitializeDMAArgs InitializeArgs;
		InitializeArgs.RHI = RHI;
		InitializeArgs.RHIDevice = GDynamicRHI->RHIGetNativeDevice();
		InitializeArgs.RHICommandQueue = GDynamicRHI->RHIGetNativeGraphicsQueue();
		/* Re-enable when adding vulkan support
		if (RHI == AJA::ERHI::Vulkan)
		{
			FVulkanDynamicRHI* vkDynamicRHI = GetDynamicRHI<FVulkanDynamicRHI>();
			InitializeArgs.VulkanInstance = vkDynamicRHI->GetInstance();

			FMemory::Memcpy(InitializeArgs.RHIDeviceUUID, vkDynamicRHI->GetDevice()->GetDeviceIdProperties().deviceUUID, 16);
		}
		*/

		const uint8 RHIIndex = static_cast<uint8>(RHI);
		if (TextureTransfer->Initialize(InitializeArgs))
		{
			TransferObjects[RHIIndex] = TextureTransfer;
		}
	});
}

void FGPUTextureTransferModule::UninitializeTextureTransfer()
{
	ENQUEUE_RENDER_COMMAND(UninitializeGPUTextureTransfer)(
		[this](FRHICommandListImmediate& RHICmdList) mutable
		{
			for (uint8 RhiIt = 1; RhiIt < RHI_MAX; RhiIt++)
			{
				if (const TextureTransferPtr& TextureTransfer = TransferObjects[RhiIt])
				{
					TextureTransfer->Uninitialize();
				}
			}
		});
}

IMPLEMENT_MODULE(FGPUTextureTransferModule, GPUTextureTransfer);
