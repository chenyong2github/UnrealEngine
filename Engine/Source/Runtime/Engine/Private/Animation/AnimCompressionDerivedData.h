// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
#include "DerivedDataPluginInterface.h"
#endif

#include "Animation/AnimCompressionTypes.h"

struct FAnimCompressContext;

#if WITH_EDITOR

//////////////////////////////////////////////////////////////////////////
// FDerivedDataAnimationCompression
class FDerivedDataAnimationCompression : public FDerivedDataPluginInterface
{
private:
	// The anim data to compress
	TSharedPtr<FCompressibleAnimData> DataToCompressPtr;

	// The Type of anim data to compress (makes up part of DDC key)
	const TCHAR* TypeName;

	// Bulk of asset DDC key
	const FString AssetDDCKey;

	// FAnimCompressContext to use during compression if we don't pull from the DDC
	TSharedPtr<FAnimCompressContext> CompressContext;

	// Size of the previous compressed data (for stat tracking)
	int32 PreviousCompressedSize;

	// Whether we should frame strip (remove every other frame from even frames animations)
	bool bPerformStripping;

	// Track if it is an even framed animation (when stripping odd framed animations will need to be resampled)
	bool bIsEvenFramed;

public:
	FDerivedDataAnimationCompression(const TCHAR* InTypeName, const FString& InAssetDDCKey, TSharedPtr<FAnimCompressContext> InCompressContext, int32 InPreviousCompressedSize);
	virtual ~FDerivedDataAnimationCompression();

	void SetCompressibleData(TSharedRef<FCompressibleAnimData> InCompressibleAnimData)
	{
		DataToCompressPtr = InCompressibleAnimData;
		check(DataToCompressPtr->Skeleton != nullptr);
	}

	virtual const TCHAR* GetPluginName() const override
	{
		return TypeName;
	}

	virtual const TCHAR* GetVersionString() const override;

	virtual FString GetPluginSpecificCacheKeySuffix() const override
	{
		return AssetDDCKey;
	}


	virtual bool IsBuildThreadsafe() const override
	{
		return false;
	}

	virtual bool Build( TArray<uint8>& OutDataArray) override;

	/** Return true if we can build **/
	bool CanBuild()
	{
		return DataToCompressPtr.IsValid();
	}
};

#endif	//WITH_EDITOR
