// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "VPSplineComponentVisualizer.h"
#include "VPSplineComponent.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"

#define LOCTEXT_NAMESPACE "VPSplineEditor"

class FVPSplineEditorModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		VPSplineComponentName = UVPSplineComponent::StaticClass()->GetFName();

		if (GUnrealEd)
		{
			TSharedPtr<FVPSplineComponentVisualizer> Visualizer = MakeShared<FVPSplineComponentVisualizer>();
			GUnrealEd->RegisterComponentVisualizer(VPSplineComponentName, Visualizer);
			Visualizer->OnRegister();
		}
	}

	virtual void ShutdownModule() override
	{
		if (GUnrealEd)
		{
			GUnrealEd->UnregisterComponentVisualizer(VPSplineComponentName);
		}
	}

private:
	FName VPSplineComponentName;
};


#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVPSplineEditorModule, VPSplineEditor)
