// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalDistanceFieldParameters.h"
#include "Renderer/Private/DistanceFieldLightingShared.h"

namespace FNiagaraDistanceFieldHelper
{
	NIAGARASHADER_API void SetGlobalDistanceFieldParameters(const FGlobalDistanceFieldParameterData* OptionalParameterData, FGlobalDistanceFieldParameters2& ShaderParameters);
}
