// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFConverterUtility.h"
#include "Engine/TextureLODSettings.h"

TextureFilter FGLTFConverterUtility::GetDefaultFilter(TextureGroup LODGroup)
{
	// TODO: should this be the running platform?
	const ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	const UTextureLODSettings& TextureLODSettings = RunningPlatform->GetTextureLODSettings();
	const FTextureLODGroup& TextureLODGroup = TextureLODSettings.GetTextureLODGroup(LODGroup);

	switch (TextureLODGroup.Filter)
	{
		case ETextureSamplerFilter::Point:             return TF_Nearest;
		case ETextureSamplerFilter::Bilinear:          return TF_Bilinear;
		case ETextureSamplerFilter::Trilinear:         return TF_Trilinear;
		case ETextureSamplerFilter::AnisotropicPoint:  return TF_Nearest;
		case ETextureSamplerFilter::AnisotropicLinear: return TF_Trilinear;
		default:                                       return TF_Default; // fallback
	}
}
