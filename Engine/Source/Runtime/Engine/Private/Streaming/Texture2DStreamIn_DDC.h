// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DStreamIn_DDC.h: Stream in helper for 2D textures loading DDC files.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Texture2DStreamIn.h"

#if WITH_EDITORONLY_DATA

extern int32 GStreamingUseAsyncRequestsForDDC;

class FTexture2DStreamIn_DDC : public FTexture2DStreamIn
{
public:

	FTexture2DStreamIn_DDC(UTexture2D* InTexture, int32 InRequestedMips);
	~FTexture2DStreamIn_DDC();

	/** Returns whether DDC of this texture needs to be regenerated.  */
	bool DDCIsInvalid() const override { return bDDCIsInvalid; }

protected:

	// StreamIn_Default : Locked mips of the intermediate textures, used as disk load destination.
	TArray<uint32, TInlineAllocator<MAX_TEXTURE_MIP_COUNT> > DDCHandles;

	// Whether the DDC data was compatible or not.
	bool bDDCIsInvalid;

	// ****************************
	// ********* Helpers **********
	// ****************************

	// Create DDC load requests (into DDCHandles)
	void DoCreateAsyncDDCRequests(const FContext& Context);

	// Create DDC load requests (into DDCHandles)
	bool DoPoolDDCRequests(const FContext& Context);

	// Load from DDC into MipData
	void DoLoadNewMipsFromDDC(const FContext& Context);
};

/**
* This class provides a helper to release DDC handles that haven't been waited for.
* This is to get around limitations of FDerivedDataCacheInterface.
*/

class FAbandonedDDCHandleManager
{
public:
	void Add(uint32 InHandle);
	void Purge();
private:
	TArray<uint32> Handles;	
	FCriticalSection CS;
	uint32 TotalAdd = 0;
};

extern FAbandonedDDCHandleManager GAbandonedDDCHandleManager;

#endif // WITH_EDITORONLY_DATA
