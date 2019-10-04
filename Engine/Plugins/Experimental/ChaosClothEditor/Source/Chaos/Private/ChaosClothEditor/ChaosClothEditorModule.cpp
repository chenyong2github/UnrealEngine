// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ChaosClothEditor/ChaosClothEditorModule.h"

#include "ChaosClothEditor/ChaosClothEditorPrivate.h"

#include "ClothingSystemEditorInterfaceModule.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FChaosClothEditorModule, ChaosClothEditor);
DEFINE_LOG_CATEGORY(LogChaosClothEditor);

void FChaosClothEditorModule::StartupModule()
{
#if WITH_CHAOS
	IModularFeatures::Get().RegisterModularFeature(FClothingSystemEditorInterfaceModule::ExtenderFeatureName, &ChaosEditorExtender);
#endif
}

void FChaosClothEditorModule::ShutdownModule()
{
#if WITH_CHAOS
	IModularFeatures::Get().RegisterModularFeature(FClothingSystemEditorInterfaceModule::ExtenderFeatureName, &ChaosEditorExtender);
#endif
}
