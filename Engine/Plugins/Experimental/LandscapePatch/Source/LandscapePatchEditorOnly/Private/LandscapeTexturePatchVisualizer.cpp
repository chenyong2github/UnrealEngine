// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeTexturePatchVisualizer.h"

#include "Landscape.h"
#include "LandscapeTexturePatchBase.h"

#include "SceneManagement.h" // FPrimitiveDrawInterface

void FLandscapeTexturePatchVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const ULandscapeTexturePatchBase* Patch = Cast<ULandscapeTexturePatchBase>(Component);
	if (!ensure(Patch))
	{
		return;
	}
	
	FTransform PatchToWorld = Patch->GetPatchToWorldTransform();
	
	FColor Color = FColor::Red;
	float Thickness = 3;
	float DepthBias = 1;
	bool bScreenSpace = 1;

	DrawRectangle(PDI, PatchToWorld.GetTranslation(), PatchToWorld.GetUnitAxis(EAxis::X), PatchToWorld.GetUnitAxis(EAxis::Y),
		Color, Patch->GetUnscaledCoverage().X * PatchToWorld.GetScale3D().X, Patch->GetUnscaledCoverage().Y * PatchToWorld.GetScale3D().Y, SDPG_Foreground,
		Thickness, DepthBias, bScreenSpace);
}