// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TimeSynchronizableMediaSource.h"

#include "MediaIOCoreDefinitions.h"

#include "RivermaxMediaSource.generated.h"

/**
 * Native data format.
 */
UENUM()
enum class ERivermaxMediaSourceColorFormat : uint8
{
	YUV2_8bit UMETA(DisplayName = "8bit YUV"),
	//Todo Add support for 10bit YUV and 8/10 bit RGB
};

/**
 * Media source for Rivermax streams.
 */
UCLASS(BlueprintType, hideCategories=(Platforms,Object), meta=(MediaIOCustomLayout="Rivermax"))
class RIVERMAXMEDIA_API URivermaxMediaSource : public UTimeSynchronizableMediaSource
{
	GENERATED_BODY()

public:

	//todo proper device configuration like Aja and Blackmagic

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Format")
	FIntPoint Resolution = {1920, 1080};
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Format")
	FFrameRate FrameRate = {24,1};
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Format")
	ERivermaxMediaSourceColorFormat PixelFormat = ERivermaxMediaSourceColorFormat::YUV2_8bit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Format")
	FString SourceAddress;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Format")
	FString DestinationAddress;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Format")
	int32 Port = 50000;

public:
	//~ Begin IMediaOptions interface
	virtual bool GetMediaOption(const FName& Key, bool DefaultValue) const override;
	virtual int64 GetMediaOption(const FName& Key, int64 DefaultValue) const override;
	virtual FString GetMediaOption(const FName& Key, const FString& DefaultValue) const override;
	virtual bool HasMediaOption(const FName& Key) const override;
	//~ End IMediaOptions interface

public:
	//~ Begin UMediaSource interface
	virtual FString GetUrl() const override;
	virtual bool Validate() const override;
	//~ End UMediaSource interface
};
