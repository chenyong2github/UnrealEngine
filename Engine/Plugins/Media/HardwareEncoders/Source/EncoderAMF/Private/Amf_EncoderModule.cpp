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
#if PLATFORM_WINDOWS
				const TCHAR* DynamicRHIModuleName = GetSelectedDynamicRHIModuleName(false);
#elif PLATFORM_LINUX
				const TCHAR* DynamicRHIModuleName = TEXT("VulkanRHI");
#endif
				if(FString("VulkanRHI") == FString(DynamicRHIModuleName))
				{
#if PLATFORM_WINDOWS
					UE_LOG(LogEncoderAMF, Fatal, TEXT("Vulkan Amf support under Windows is currently unstable and has been hard disabled for the time being."));
#endif
					AMF.InitializeContext("Vulkan", NULL);
					amf::AMFContext1Ptr pContext1(AMF.GetContext());

					amf_size NumDeviceExtensions = 0;
					pContext1->GetVulkanDeviceExtensions(&NumDeviceExtensions, nullptr);

					TArray<const ANSICHAR*> ExtentionsToAdd; 
					ExtentionsToAdd.AddUninitialized(5);

					pContext1->GetVulkanDeviceExtensions(&NumDeviceExtensions, ExtentionsToAdd.GetData());

					AMF.DestroyContext();

					VulkanRHIBridge::AddEnabledDeviceExtensionsAndLayers(ExtentionsToAdd, TArray<const ANSICHAR*>());
				}

				FCoreDelegates::OnPostEngineInit.AddLambda([]() {FVideoEncoderAmf_H264::Register(FVideoEncoderFactory::Get());});
				
				AMFStarted = true;
			}
		}
	}

	void ShutdownModule()
	{
		using namespace AVEncoder;
		if(AMFStarted)
		{
			FAmfCommon& AMF = FAmfCommon::Setup();
			AMF.Shutdown();
			AMFStarted = false;
		}
	}

private:
	bool AMFStarted = false;
};

IMPLEMENT_MODULE(FAMFEncoderModule, EncoderAMF);
