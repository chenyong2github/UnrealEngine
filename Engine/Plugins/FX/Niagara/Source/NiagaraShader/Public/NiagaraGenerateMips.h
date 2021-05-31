// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"

namespace NiagaraGenerateMips
{
	NIAGARASHADER_API void GenerateMips(FRHICommandList& RHICmdList, FRHITexture2D* TextureRHI);
}
