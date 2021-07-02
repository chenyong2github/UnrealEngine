// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "VulkanRHIPrivate.h"
#include "VulkanRHIBridge.h"

#include "DynamicRHI.h"

#include "Amf_Common.h"
#include "Amf_EncoderH264.h"
#include "VideoEncoderFactory.h"

#include "Misc/CoreDelegates.h"

class FAMFEncoderModule : public IModuleInterface
{
public:
	void StartupModule()
	{
		using namespace AVEncoder;
		if(FApp::CanEverRender())
		{
			FAmfCommon& AMF = FAmfCommon::Setup();

			if(AMF.GetIsAvailable())
			{
				const TCHAR* DynamicRHIModuleName = GetSelectedDynamicRHIModuleName(false);
				
				if(FString("VulkanRHI") == FString(DynamicRHIModuleName))
				{
					AMF.InitializeContext("Vulkan", NULL);
					amf::AMFContext1Ptr pContext1(AMF.GetContext());

					amf_size NumDeviceExtensions = 0;
					const char** DeviceExtensions = nullptr;
					pContext1->GetVulkanDeviceExtensions(&NumDeviceExtensions, DeviceExtensions);

					AMF.DestroyContext();

					const TArray<const ANSICHAR*> ExtentionsToAdd(DeviceExtensions, NumDeviceExtensions);
					VulkanRHIBridge::AddEnabledDeviceExtensionsAndLayers(ExtentionsToAdd, TArray<const ANSICHAR*>());
				}

				FCoreDelegates::OnPostEngineInit.AddLambda([]() {FVideoEncoderAmf_H264::Register(FVideoEncoderFactory::Get());});
			}
		}
	}
};

IMPLEMENT_MODULE(FAMFEncoderModule, EncoderAMF);
