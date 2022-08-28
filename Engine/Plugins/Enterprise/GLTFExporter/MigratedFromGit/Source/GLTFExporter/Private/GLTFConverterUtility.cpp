// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFConverterUtility.h"
#include "Engine/Blueprint.h"

bool FGLTFConverterUtility::IsSkySphereBlueprint(const UBlueprint* Blueprint)
{
	return Blueprint != nullptr && Blueprint->GetPathName().Equals(TEXT("/Engine/EngineSky/BP_Sky_Sphere.BP_Sky_Sphere"));
}

bool FGLTFConverterUtility::IsHDRIBackdropBlueprint(const UBlueprint* Blueprint)
{
	return Blueprint != nullptr && Blueprint->GetPathName().Equals(TEXT("/HDRIBackdrop/Blueprints/HDRIBackdrop.HDRIBackdrop"));
}
