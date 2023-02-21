// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

enum ETextureRenderTargetFormat : int;

namespace NiagaraDataInterfaceRenderTargetCommon
{
	extern int32 GReleaseResourceOnRemove;
	extern int32 GIgnoreCookedOut;
	extern float GResolutionMultiplier;

	extern bool GetRenderTargetFormat(bool bOverrideFormat, ETextureRenderTargetFormat OverrideFormat, ETextureRenderTargetFormat& OutRenderTargetFormat);
}
