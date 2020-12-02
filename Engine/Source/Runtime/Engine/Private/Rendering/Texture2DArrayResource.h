// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/*=============================================================================
	Texture2DArrayResource.cpp: Implementation of FTexture2DArrayResource used  by streamable UTexture2DArray.
=============================================================================*/

#include "CoreMinimal.h"
#include "Rendering/StreamableTextureResource.h"
#include "Containers/ResourceArray.h"

class UTexture2DArray;

/** Represents a 2D Texture Array to the renderer. */
class FTexture2DArrayResource : public FStreamableTextureResource
{
public:

	FTexture2DArrayResource(UTexture2DArray* InOwner, const FStreamableRenderResourceState& InState);

	// Dynamic cast methods.
	ENGINE_API virtual FTexture2DArrayResource* GetTexture2DArrayResource() { return this; }
	// Dynamic cast methods (const).
	ENGINE_API virtual const FTexture2DArrayResource* GetTexture2DArrayResource() const { return this; }

	virtual uint64 GetPlatformMipsSize(uint32 NumMips) const override;

protected:

	void CreateTexture() final override;
	void CreatePartiallyResidentTexture() final override;

	/** The initial data for all mips of a single slice. */
	typedef TArray<TArrayView<uint8>, TInlineAllocator<MAX_TEXTURE_MIP_COUNT> > FSingleSliceMipDataView;
	/** All slices initial data. */
	TArray<FSingleSliceMipDataView> SliceMipDataViews;
	/** The single allocation holding initial data. */
	TUniquePtr<uint8[]> InitialMipData;
};
