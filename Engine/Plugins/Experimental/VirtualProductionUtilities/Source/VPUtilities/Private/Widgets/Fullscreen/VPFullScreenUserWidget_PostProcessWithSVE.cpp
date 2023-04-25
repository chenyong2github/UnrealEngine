// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Fullscreen/VPFullScreenUserWidget_PostProcessWithSVE.h"

#include "Widgets/Fullscreen/PostProcessSceneViewExtension.h"

#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneViewExtension.h"

bool FVPFullScreenUserWidget_PostProcessWithSVE::Display(UWorld* World, UUserWidget* Widget, TAttribute<float> InDPIScale)
{
	bool bOk = CreateRenderer(World, Widget, MoveTemp(InDPIScale));
	if (bOk && ensureMsgf(GetPostProcessMaterialInstance(), TEXT("CreateRenderer returned true even though it failed.")))
	{
		SceneViewExtension = FSceneViewExtensions::NewExtension<UE::VirtualProductionUtilities::Private::FPostProcessSceneViewExtension>(
			TAttribute<UMaterialInterface*>::CreateRaw(this, &FVPFullScreenUserWidget_PostProcessWithSVE::GetPostProcessMaterial)
			);
	}
	return bOk;
}

void FVPFullScreenUserWidget_PostProcessWithSVE::Hide(UWorld* World)
{
	SceneViewExtension.Reset();
	FVPFullScreenUserWidget_PostProcessBase::Hide(World);
}

void FVPFullScreenUserWidget_PostProcessWithSVE::Tick(UWorld* World, float DeltaSeconds)
{
	TickRenderer(World, DeltaSeconds);
}

UMaterialInterface* FVPFullScreenUserWidget_PostProcessWithSVE::GetPostProcessMaterial() const
{
	return GetPostProcessMaterialInstance().Get();
}
