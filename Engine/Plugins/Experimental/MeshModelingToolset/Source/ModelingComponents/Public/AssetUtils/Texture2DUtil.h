// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "Image/ImageDimensions.h"
#include "Image/ImageBuilder.h"
#include "Engine/Classes/Engine/Texture2D.h"



namespace UE
{
namespace AssetUtils
{

	MODELINGCOMPONENTS_API bool ReadTexture(
		UTexture2D* TextureMap,
		FImageDimensions& DimensionsOut,
		TImageBuilder<FVector4f>& DestImageOut,
		bool bPreferPlatformData = false);

}
}

