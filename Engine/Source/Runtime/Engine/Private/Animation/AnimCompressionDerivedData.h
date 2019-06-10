// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
#include "DerivedDataPluginInterface.h"
#endif

#include "Animation/AnimCompressionTypes.h"

class UAnimSequence;
struct FAnimCompressContext;

#if WITH_EDITOR

//////////////////////////////////////////////////////////////////////////
// FDerivedDataAnimationCompression
class FDerivedDataAnimationCompression : public FDerivedDataPluginInterface
{
private:
	FCompressibleAnimData DataToCompress;

	// FAnimCompressContext to use during compression if we don't pull from the DDC
	TSharedPtr<FAnimCompressContext> CompressContext;

	// Size of the previous compressed data (for stat tracking)
	int32 PreviousCompressedSize;

	// Whether we should frame strip (remove every other frame from even frames animations)
	bool bPerformStripping;

	// Track if it is an even framed animation (when stripping odd framed animations will need to be resampled)
	bool bIsEvenFramed;

public:
	FDerivedDataAnimationCompression(const FCompressibleAnimData& InDataToCompress, TSharedPtr<FAnimCompressContext> InCompressContext, int32 InPreviousCompressedSize, bool bInTryFrameStripping, bool bTryStrippingOnOddFramedAnims);
	virtual ~FDerivedDataAnimationCompression();

	virtual const TCHAR* GetPluginName() const override
	{
		return *DataToCompress.TypeName;
	}

	virtual const TCHAR* GetVersionString() const override
	{
		// This is a version string that mimics the old versioning scheme. If you
		// want to bump this version, generate a new guid using VS->Tools->Create GUID and
		// return it here. Ex.
		return TEXT("1F1656B9E10142729AB16650D9821B1F");
	}

	virtual FString GetPluginSpecificCacheKeySuffix() const override;


	virtual bool IsBuildThreadsafe() const override
	{
		return false;
	}

	virtual bool Build( TArray<uint8>& OutDataArray) override;

	/** Return true if we can build **/
	bool CanBuild()
	{
		return true;
	}
};

#endif	//WITH_EDITOR
