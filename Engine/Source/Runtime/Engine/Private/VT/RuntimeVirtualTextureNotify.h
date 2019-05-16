// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace RuntimeVirtualTexture
{
#if WITH_EDITOR
	/**
	 * Find any URuntimeVirtualTextureComponent that reference this virtual texture and mark them dirty.
	 * We need to do this after editing the URuntimeVirtualTexture settings.
	 */
	void NotifyComponents(URuntimeVirtualTexture const* VirtualTexture);

	/**
	 * Find any primitive components that render to this virtual texture and mark them dirty.
	 * We need to do this after editing the URuntimeVirtualTexture settings.
	 */
	void NotifyPrimitives(URuntimeVirtualTexture const* VirtualTexture);
#endif

	/**
	 * Find materials referencing this virtual texture and re-cache the uniforms.
	 * We need to do this after any operation that reallocates the virtual texture since the material caches info about the VT allocation in it's uniform buffer.
	 */
	void NotifyMaterials(URuntimeVirtualTexture const* VirtualTexture);
}