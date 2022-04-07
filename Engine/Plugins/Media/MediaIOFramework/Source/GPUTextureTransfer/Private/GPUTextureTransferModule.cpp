// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPUTextureTransferModule.h"

#if PLATFORM_WINDOWS
#include "D3D11TextureTransfer.h"
#include "D3D12TextureTransfer.h"
#include "VulkanTextureTransfer.h"
#include "TextureTransferBase.h"
#endif

#include "CoreMinimal.h"
#include "IVulkanDynamicRHI.h"
#include "GenericPlatform/GenericPlatformDriver.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"

DEFINE_LOG_CATEGORY(LogGPUTextureTransfer);

namespace 
{
	auto ConvertRHI = [](ERHIInterfaceType RHI)
	{
		switch (RHI)
		{
		case ERHIInterfaceType::D3D11: return UE::GPUTextureTransfer::ERHI::D3D11;
		case ERHIInterfaceType::D3D12: return UE::GPUTextureTransfer::ERHI::D3D12;
		case ERHIInterfaceType::Vulkan: return UE::GPUTextureTransfer::ERHI::Vulkan;
		default: return UE::GPUTextureTransfer::ERHI::Invalid;
		}
	};
}

void FGPUTextureTransferModule::StartupModule()
{
	if (FApp::CanEverRender())
	{
		if (LoadGPUDirectBinary())
		{
#if PLATFORM_WINDOWS		
			const TCHAR* DynamicRHIModuleName = GetSelectedDynamicRHIModuleName(false);
#elif PLATFORM_LINUX
			const TCHAR* DynamicRHIModuleName = TEXT("VulkanRHI");
#else
			const TCHAR* DynamicRHIModuleName = TEXT("");
			ensure(false);
#endif

			// We cannot use GDynmicRHI here because it hasn't been assigned yet.
			if (TEXT("VulkanRHI") == FString(DynamicRHIModuleName))
			{
#if PLATFORM_WINDOWS
				const TArray<const ANSICHAR*> ExtentionsToAdd{ VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME, VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
#elif PLATFORM_LINUX
				const TArray<const ANSICHAR*> ExtentionsToAdd{ VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME, VK_KHR_SURFACE_EXTENSION_NAME };
#endif
				IVulkanDynamicRHI::AddEnabledDeviceExtensionsAndLayers(ExtentionsToAdd, TArray<const ANSICHAR*>());
			}

			TransferObjects.AddDefaulted(RHI_MAX);

			// Since this module is started before the RHI is initialized, we have to delay initialization to later. 
			FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(this, &FGPUTextureTransferModule::InitializeTextureTransfer);
			// Same for shutdown, uninitialize ourselves before library is unloaded
			FCoreDelegates::OnEnginePreExit.AddRaw(this, &FGPUTextureTransferModule::UninitializeTextureTransfer);
		}
	}
}

void FGPUTextureTransferModule::ShutdownModule()
{
}

UE::GPUTextureTransfer::TextureTransferPtr FGPUTextureTransferModule::GetTextureTransfer()
{
#if PLATFORM_WINDOWS
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
#endif
	return nullptr;
}

bool FGPUTextureTransferModule::IsAvailable()
{
#if PLATFORM_WINDOWS
	return FModuleManager::Get().IsModuleLoaded("GPUTextureTransfer");
#else
	return false;
#endif
}

FGPUTextureTransferModule& FGPUTextureTransferModule::Get()
{
	return FModuleManager::LoadModuleChecked<FGPUTextureTransferModule>("GPUTextureTransfer");
}

bool FGPUTextureTransferModule::LoadGPUDirectBinary()
{
#if PLATFORM_WINDOWS
	FString GPUDirectPath = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/NVIDIA/GPUDirect"), FPlatformProcess::GetBinariesSubdirectory());
	FPlatformProcess::PushDllDirectory(*GPUDirectPath);

	FString DVPDll;

	DVPDll = TEXT("dvp.dll");

	DVPDll = FPaths::Combine(GPUDirectPath, DVPDll);

	TextureTransferHandle = FPlatformProcess::GetDllHandle(*DVPDll);
	if (TextureTransferHandle == nullptr)
	{
		UE_LOG(LogGPUTextureTransfer, Display, TEXT("Failed to load required library %s. GPU Texture transfer will not be functional."), *DVPDll);
	}

	FPlatformProcess::PopDllDirectory(*GPUDirectPath);

#endif
	return !!TextureTransferHandle;
}


void FGPUTextureTransferModule::InitializeTextureTransfer()
{
#if PLATFORM_WINDOWS
	bIsGPUTextureTransferAvailable = true;
	// This must be called on game thread 
	const FGPUDriverInfo GPUDriverInfo = FPlatformMisc::GetGPUDriverInfo(GRHIAdapterName);
	bIsGPUTextureTransferAvailable = GPUDriverInfo.IsNVIDIA() && !FModuleManager::Get().IsModuleLoaded("RenderDocPlugin");
	bIsGPUTextureTransferAvailable = bIsGPUTextureTransferAvailable && !GPUDriverInfo.DeviceDescription.Contains(TEXT("Tesla"));

	if (!bIsGPUTextureTransferAvailable)
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(InitializeGPUTextureTransfer)(
	[this](FRHICommandListImmediate& RHICmdList) mutable
	{
		if (!GDynamicRHI)
		{
			return;
		}

		UE::GPUTextureTransfer::TextureTransferPtr TextureTransfer;

		UE::GPUTextureTransfer::ERHI RHI = ConvertRHI(RHIGetInterfaceType());

		switch (RHI)
		{
		case UE::GPUTextureTransfer::ERHI::D3D11:
			TextureTransfer = MakeShared<UE::GPUTextureTransfer::Private::FD3D11TextureTransfer>();
			break;
		case UE::GPUTextureTransfer::ERHI::D3D12:
			TextureTransfer = MakeShared<UE::GPUTextureTransfer::Private::FD3D12TextureTransfer>();
			break;
		case UE::GPUTextureTransfer::ERHI::Vulkan:
			TextureTransfer = MakeShared<UE::GPUTextureTransfer::Private::FVulkanTextureTransfer>();
			break;
		default:
			ensureAlways(false);
			break;
		}

		UE::GPUTextureTransfer::FInitializeDMAArgs InitializeArgs;
		InitializeArgs.RHI = RHI;
		InitializeArgs.RHIDevice = GDynamicRHI->RHIGetNativeDevice();
		InitializeArgs.RHICommandQueue = GDynamicRHI->RHIGetNativeGraphicsQueue();
		if (RHI == UE::GPUTextureTransfer::ERHI::Vulkan)
		{
			IVulkanDynamicRHI* DynRHI = GetIVulkanDynamicRHI();
			InitializeArgs.VulkanInstance = DynRHI->RHIGetVkInstance();
			FMemory::Memcpy(InitializeArgs.RHIDeviceUUID, DynRHI->RHIGetVulkanDeviceUUID(), 16);
		}

		const uint8 RHIIndex = static_cast<uint8>(RHI);
		if (TextureTransfer->Initialize(InitializeArgs))
		{
			TransferObjects[RHIIndex] = TextureTransfer;
		}
	});
#endif // PLATFORM_WINDOWS
}

void FGPUTextureTransferModule::UninitializeTextureTransfer()
{
#if PLATFORM_WINDOWS
	ENQUEUE_RENDER_COMMAND(UninitializeGPUTextureTransfer)(
		[this](FRHICommandListImmediate& RHICmdList) mutable
		{
			for (uint8 RhiIt = 1; RhiIt < RHI_MAX; RhiIt++)
			{
				if (const UE::GPUTextureTransfer::TextureTransferPtr& TextureTransfer = TransferObjects[RhiIt])
				{
					TextureTransfer->Uninitialize();
				}
			}
		});
#endif
}

IMPLEMENT_MODULE(FGPUTextureTransferModule, GPUTextureTransfer);
