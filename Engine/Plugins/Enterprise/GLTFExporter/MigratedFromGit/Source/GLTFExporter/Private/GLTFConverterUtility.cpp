// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFConverterUtility.h"

bool FGLTFConverterUtility::IsSkySphereBlueprint(const UBlueprint* Blueprint)
{
	static const UBlueprint* SkySphere = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EngineSky/BP_Sky_Sphere.BP_Sky_Sphere"), nullptr, LOAD_None, nullptr);
	return Blueprint != nullptr && Blueprint == SkySphere;
}

bool FGLTFConverterUtility::IsHDRIBackdropBlueprint(const UBlueprint* Blueprint)
{
	static const UBlueprint* HDRIBackdrop = LoadObject<UBlueprint>(nullptr, TEXT("/HDRIBackdrop/Blueprints/HDRIBackdrop.HDRIBackdrop"), nullptr, LOAD_None, nullptr);
	return Blueprint != nullptr && Blueprint == HDRIBackdrop;
}
