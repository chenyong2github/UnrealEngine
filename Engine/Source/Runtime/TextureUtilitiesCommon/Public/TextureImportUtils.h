// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

struct FImage;

namespace UE::TextureUtilitiesCommon
{
	/**
		* Detect the existence of gray scale image in some formats and convert those to a gray scale equivalent image
		* 
		* @return true if the image was converted
		*/
	TEXTUREUTILITIESCOMMON_API bool AutoDetectAndChangeGrayScale(FImage& Image);
}