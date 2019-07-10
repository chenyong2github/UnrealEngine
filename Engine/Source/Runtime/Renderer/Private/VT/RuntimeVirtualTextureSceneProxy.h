// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class URuntimeVirtualTexture;
class URuntimeVirtualTextureComponent;

/** Scene proxy for the URuntimeVirtualTextureComponent. Manages a runtime virtual texture in the renderer scene. */
class FRuntimeVirtualTextureSceneProxy
{
public:
	/** Constructor initializes resources for the URuntimeVirtualTexture associated with the provided component. */
	FRuntimeVirtualTextureSceneProxy(URuntimeVirtualTextureComponent* InComponent);
	~FRuntimeVirtualTextureSceneProxy();

	/**
	 * Release the object and it's associated runtime virtual texture resources. 
	 * Call this on the main thread before deferring deletion to happen on the render thread.
	 */
	void Release();

	/** Index in FScene::RuntimeVirtualTextures. */
	int32 SceneIndex;
	/** Unique ID for producer that this Proxy created. Used for finding this object and it's SceneIndex from the producer.  */
	int32 ProducerId;

	/** Pointer to linked URuntimeVirtualTexture. Not for dereferencing, just for pointer comparison. */
	URuntimeVirtualTexture* VirtualTexture;

private:
	static int32 ProducerIdGenerator;
};
