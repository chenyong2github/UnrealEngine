// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace RuntimeVirtualTexture
{
	/** Returns true if the component describes a runtime virtual texture that has streaming mips. */
	bool HasStreamedMips(class URuntimeVirtualTextureComponent* InComponent);

	/** Build the streaming mips and store in the component's associated URuntimeVirtualTexture object. */
	bool BuildStreamedMips(class URuntimeVirtualTextureComponent* InComponent, enum class ERuntimeVirtualTextureDebugType DebugType);
};
