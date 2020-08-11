// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class URuntimeVirtualTextureComponent;

namespace RuntimeVirtualTexture
{
	/** Returns true if the component describes a runtime virtual texture that has a MinMax height texture. */
	bool HasMinMaxHeightTexture(URuntimeVirtualTextureComponent* InComponent);

	/** Build the min/max height texture. */
	bool BuildMinMaxHeightTexture(URuntimeVirtualTextureComponent* InComponent);
};
