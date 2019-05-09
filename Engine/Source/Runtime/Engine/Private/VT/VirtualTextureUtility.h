// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IAllocatedVirtualTexture;

/** Writes 2x FUintVector4 */
void VTGetPackedPageTableUniform(FUintVector4* Uniform, const IAllocatedVirtualTexture* AllocatedVT);

/** Writes 1x FUintVector4 */
void VTGetPackedUniform(FUintVector4* Uniform, const IAllocatedVirtualTexture* AllocatedVT, uint32 LayerIndex);

