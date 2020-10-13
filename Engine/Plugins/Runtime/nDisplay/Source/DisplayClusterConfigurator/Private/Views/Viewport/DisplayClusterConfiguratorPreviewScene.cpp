// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorPreviewScene.h"

#include "DisplayClusterConfiguratorToolkit.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "UObject/Package.h"


FDisplayClusterConfiguratorPreviewScene::FDisplayClusterConfiguratorPreviewScene(const ConstructionValues& CVS, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: IDisplayClusterConfiguratorPreviewScene(CVS)
	, ToolkitPtr(InToolkit)
{
	// Reset Floor mesh
	FVector FloorPos(0.f, 0.f, -50.f);
	FloorMeshComponent->SetWorldTransform(FTransform(FRotator(0.f, 0.f, 0.f), FloorPos, FVector(3.0f, 3.0f, 1.0f)));
	FloorMeshComponent->SetVisibility(false);
}


void FDisplayClusterConfiguratorPreviewScene::Tick(float InDeltaTime)
{
	IDisplayClusterConfiguratorPreviewScene::Tick(InDeltaTime);

	if (!GIntraFrameDebuggingGameThread)
	{
		GetWorld()->Tick(LEVELTICK_All, InDeltaTime);
	}
}
