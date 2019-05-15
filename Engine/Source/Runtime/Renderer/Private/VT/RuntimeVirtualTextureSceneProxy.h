// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Scene proxy for the URuntimeVirtualTextureComponent. Manages a runtime virtual texture in the renderer scene. */
class FRuntimeVirtualTextureSceneProxy
{
public:
	FRuntimeVirtualTextureSceneProxy(URuntimeVirtualTextureComponent* InComponent);
	~FRuntimeVirtualTextureSceneProxy();

	/** Pointer to linked URuntimeVirtualTexture. Not for dereferencing, just for debug! */
	class URuntimeVirtualTexture* VirtualTexture;
};
