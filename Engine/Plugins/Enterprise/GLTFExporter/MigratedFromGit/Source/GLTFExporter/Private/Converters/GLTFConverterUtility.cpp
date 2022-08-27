// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFConverterUtility.h"
#include "Engine/Blueprint.h"

bool FGLTFConverterUtility::IsSkySphereBlueprint(const UBlueprint* Blueprint)
{
	return Blueprint != nullptr && Blueprint->GetPathName().Equals(TEXT("/Engine/EngineSky/BP_Sky_Sphere.BP_Sky_Sphere"));
}

bool FGLTFConverterUtility::IsHDRIBackdropBlueprint(const UBlueprint* Blueprint)
{
	return Blueprint != nullptr && Blueprint->GetPathName().Equals(TEXT("/HDRIBackdrop/Blueprints/HDRIBackdrop.HDRIBackdrop"));
}

bool FGLTFConverterUtility::IsSelected(const UActorComponent* ActorComponent)
{
	if (ActorComponent == nullptr)
	{
		return false;
	}

	const AActor* Owner = ActorComponent->GetOwner();
	return Owner != nullptr && Owner->IsSelected();
}
