// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCamera.h"

struct FGLTFCameraUtility
{
	static FGLTFJsonOrthographic ConvertOrthographic(const FMinimalViewInfo& View, const float ConversionScale);
	static FGLTFJsonPerspective ConvertPerspective(const FMinimalViewInfo& View, const float ConversionScale);

	static float ConvertFieldOfView(const FMinimalViewInfo& View);
};
