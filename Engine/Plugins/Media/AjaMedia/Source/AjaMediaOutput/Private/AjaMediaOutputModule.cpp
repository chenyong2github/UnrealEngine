// Copyright Epic Games, Inc. All Rights Reserved.

#include "AjaMediaOutputModule.h"

#include "AjaMediaFrameGrabberProtocol.h"
#include "AjaLib.h"
#include "DynamicRHI.h"
#include "RHI.h"

#define LOCTEXT_NAMESPACE "AjaMediaOutput"

DEFINE_LOG_CATEGORY(LogAjaMediaOutput);

void FAjaMediaOutputModule::StartupModule()
{
	ENQUEUE_RENDER_COMMAND(AjaMediaCaptureInitialize)(
		[this](FRHICommandListImmediate& RHICmdList) mutable
		{
			if (!GDynamicRHI)
			{
				return;
			}

			auto GetRHI = []()
			{
				FString RHIName = GDynamicRHI->GetName();
				if (RHIName == TEXT("D3D11"))
				{
					return AJA::ERHI::D3D11;
				}
				else if (RHIName == TEXT("D3D12"))
				{
					return AJA::ERHI::D3D12;
				}
				else if (RHIName == TEXT("Vulkan"))
				{
					return AJA::ERHI::Vulkan;
				}

				return AJA::ERHI::Invalid;

			};

			AJA::FInitializeDMAArgs Args;
			AJA::ERHI RHI = GetRHI();
			Args.RHI = RHI;
			/* Re-enable when adding vulkan support
			if (RHI == AJA::ERHI::Vulkan)
			{
				FVulkanDynamicRHI* vkDynamicRHI = static_cast<FVulkanDynamicRHI*>(GDynamicRHI);
				Args.InVulkanInstance = vkDynamicRHI->GetInstance();

				FMemory::Memcpy(Args.InRHIDeviceUUID, vkDynamicRHI->GetDevice()->GetDeviceIdProperties().deviceUUID, 16);
			}
			*/
			Args.RHIDevice = GDynamicRHI->RHIGetNativeDevice();
			Args.RHICommandQueue = GDynamicRHI->RHIGetNativeGraphicsQueue();

			bIsGPUTextureTransferAvailable = AJA::InitializeDMA(Args);
		});
}

void FAjaMediaOutputModule::ShutdownModule()
{
	ENQUEUE_RENDER_COMMAND(AjaMediaCaptureUninitialize)(
		[bIsAvailable = bIsGPUTextureTransferAvailable](FRHICommandListImmediate& RHICmdList) mutable
		{
			if (bIsAvailable)
			{
				AJA::UninitializeDMA();
			}
		});
}

bool FAjaMediaOutputModule::IsGPUTextureTransferAvailable() const
{
	return bIsGPUTextureTransferAvailable;
}

IMPLEMENT_MODULE(FAjaMediaOutputModule, AjaMediaOutput )

#undef LOCTEXT_NAMESPACE
