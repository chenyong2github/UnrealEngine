// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCamera.h"

struct FGLTFCameraUtility
{
	static FGLTFJsonOrthographic ConvertOrthographic(const FMinimalViewInfo& View);
	static FGLTFJsonPerspective ConvertPerspective(const FMinimalViewInfo& View);

	static float ConvertFieldOfView(const FMinimalViewInfo& View);
};
