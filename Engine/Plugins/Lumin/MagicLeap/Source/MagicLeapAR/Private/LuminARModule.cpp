// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminARModule.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"
#include "Features/IModularFeature.h"

#include "LuminARTrackingSystem.h"
#include "LuminARTrackingSystem.h"
#include "Templates/SharedPointer.h"

#define LOCTEXT_NAMESPACE "LuminAR"

IMPLEMENT_MODULE(FLuminARModule, MagicLeapAR)

TWeakPtr<FLuminARImplementation, ESPMode::ThreadSafe> FLuminARModule::LuminARImplmentationPtr;

void FLuminARModule::StartupModule()
{
	ensureMsgf(FModuleManager::Get().LoadModule("AugmentedReality"), TEXT("Lumin AR depends on the AugmentedReality module.") );

	FModuleManager::LoadModulePtr<IModuleInterface>("AugmentedReality");
}

//create for mutual connection (regardless of construction order)
TSharedPtr<IARSystemSupport, ESPMode::ThreadSafe> FLuminARModule::CreateARImplementation()
{
#if WITH_MLSDK
#if !PLATFORM_LUMIN
	bool bIsVDZIEnabled = false;
	GConfig->GetBool(TEXT("/Script/MagicLeap.MagicLeapSettings"), TEXT("bEnableZI"), bIsVDZIEnabled, GEngineIni);
	if (bIsVDZIEnabled)
#endif // !PLATFORM_LUMIN
	{
		LuminARImplementation = MakeShareable(new FLuminARImplementation());
		LuminARImplmentationPtr = LuminARImplementation;
	}
#endif //WITH_MLSDK
	return LuminARImplementation;
}

void FLuminARModule::ConnectARImplementationToXRSystem(FXRTrackingSystemBase* InXRTrackingSystem)
{
	ensure(InXRTrackingSystem);

	LuminARImplementation->SetARSystem(InXRTrackingSystem->GetARCompositionComponent());
	InXRTrackingSystem->GetARCompositionComponent()->InitializeARSystem();
}

void FLuminARModule::InitializeARImplementation()
{}

TSharedPtr<class FLuminARImplementation, ESPMode::ThreadSafe> FLuminARModule::GetLuminARSystem()
{
	return LuminARImplmentationPtr.Pin();
}

#undef LOCTEXT_NAMESPACE
