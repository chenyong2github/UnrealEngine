// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSSharedModule.h"

#include "Features/IModularFeatures.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

#include COMPILED_PLATFORM_HEADER(EOSSDKManager.h)

#define LOCTEXT_NAMESPACE "EOS"

IMPLEMENT_MODULE(FEOSSharedModule, EOSShared);

void FEOSSharedModule::StartupModule()
{
#if WITH_EOS_SDK
	SDKManager = MakeUnique<FPlatformEOSSDKManager>();
	check(SDKManager);

	IModularFeatures::Get().RegisterModularFeature(TEXT("EOSSDKManager"), SDKManager.Get());
#endif // WITH_EOS_SDK
}

void FEOSSharedModule::ShutdownModule()
{
#if WITH_EOS_SDK
	if(SDKManager.IsValid())
	{
		IModularFeatures::Get().UnregisterModularFeature(TEXT("EOSSDKManager"), SDKManager.Get());
		SDKManager->Shutdown();
		SDKManager.Reset();
	}
#endif // WITH_EOS_SDK
}

#undef LOCTEXT_NAMESPACE