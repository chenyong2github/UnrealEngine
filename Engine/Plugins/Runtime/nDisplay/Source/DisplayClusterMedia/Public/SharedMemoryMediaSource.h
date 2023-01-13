// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MediaSource.h"

#include "SharedMemoryMediaSource.generated.h"

/**
 * Media source for SharedMemory streams.
 */
UCLASS(BlueprintType, hideCategories=(Platforms,Object))
class DISPLAYCLUSTERMEDIA_API USharedMemoryMediaSource : public UMediaSource
{
	GENERATED_BODY()

public:

	/** Shared memory will be found by using this name. Should match the media output setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	FString UniqueName = TEXT("UniqueName");

	/** Zero latency option to wait for the cross gpu texture rendered on the same frame. May adversely affect fps */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bZeroLatency = true;

public:

	//~ Begin UMediaSource interface
	virtual FString GetUrl() const override;
	virtual bool Validate() const override;
	//~ End UMediaSource interface

	//~ Begin IMediaOptions interface
	virtual bool GetMediaOption(const FName& Key, bool DefaultValue) const override;
	virtual int64 GetMediaOption(const FName& Key, int64 DefaultValue) const override;
	virtual FString GetMediaOption(const FName& Key, const FString& DefaultValue) const override;
	virtual bool HasMediaOption(const FName& Key) const override;
	//~ End IMediaOptions interface
};
