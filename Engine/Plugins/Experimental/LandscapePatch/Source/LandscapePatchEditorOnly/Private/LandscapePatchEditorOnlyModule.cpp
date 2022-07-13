// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapePatchEditorOnlyModule.h"

#include "Editor/UnrealEdEngine.h"
#include "LandscapeTexturePatch.h"
#include "LandscapeTexturePatchBase.h"
#include "LandscapeTexturePatchCustomization.h"
#include "LandscapeTexturePatchVisualizer.h"
#include "PropertyEditorModule.h"
#include "UnrealEdGlobals.h"

#define LOCTEXT_NAMESPACE "FLandscapePatchEditorOnlyModule"

void FLandscapePatchEditorOnlyModule::StartupModule()
{
	if (GUnrealEd)
	{
		TSharedPtr<FLandscapeTexturePatchVisualizer> Visualizer = MakeShared<FLandscapeTexturePatchVisualizer>();
		GUnrealEd->RegisterComponentVisualizer(ULandscapeTexturePatchBase::StaticClass()->GetFName(), Visualizer);
		GUnrealEd->RegisterComponentVisualizer(ULandscapeTexturePatch::StaticClass()->GetFName(), Visualizer);
		// This call should maybe be inside the RegisterComponentVisualizer call above, but since it's not,
		// we'll put it here.
		Visualizer->OnRegister();
		VisualizersToUnregisterOnShutdown.Add(ULandscapeTexturePatchBase::StaticClass()->GetFName());
	}

	// Register detail customization
	ClassesToUnregisterOnShutdown.Reset();
	FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
	if (PropertyEditorModule)
	{
		PropertyEditorModule->RegisterCustomClassLayout(ULandscapeTexturePatchBase::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FLandscapeTexturePatchCustomization::MakeInstance));
		ClassesToUnregisterOnShutdown.Add(ULandscapeTexturePatchBase::StaticClass()->GetFName());
	}
}

void FLandscapePatchEditorOnlyModule::ShutdownModule()
{
	if (GUnrealEd)
	{
		for (const FName& Name : VisualizersToUnregisterOnShutdown)
		{
			GUnrealEd->UnregisterComponentVisualizer(Name);
		}
	}

	// Unregister customizations
	FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
	if (PropertyEditorModule)
	{
		for (const FName& ClassName : ClassesToUnregisterOnShutdown)
		{
			PropertyEditorModule->UnregisterCustomClassLayout(ClassName);
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLandscapePatchEditorOnlyModule, LandscapePatchEditorOnly)