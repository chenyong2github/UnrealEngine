// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TimeSynchronizableMediaSource.h"
#include "Containers/UnrealString.h"
#include "MediaIOCoreDefinitions.h"
#include "MediaIOCoreDeinterlacer.h"
#include "HAL/Platform.h"
#include "UObject/NameTypes.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "CaptureCardMediaSource.generated.h"

namespace UE::CaptureCardMediaSource
{
	static FName Deinterlacer("Deinterlacer");
	static FName InterlaceFieldOrder("InterlaceFieldOrder");
}

/**
 * Base class for media sources that are coming from a capture card.
 */
UCLASS(Abstract)
class MEDIAIOCORE_API UCaptureCardMediaSource : public UTimeSynchronizableMediaSource
{
	GENERATED_BODY()
	
public:
	
	UCaptureCardMediaSource()
	{
		Deinterlacer = CreateDefaultSubobject<UBobDeinterlacer>("Deinterlacer");
	}
	
	/**
	 * How interlaced video should be treated.
	 */
	UPROPERTY(BlueprintReadOnly, Instanced, EditAnywhere, Category = "Video")
	TObjectPtr<UVideoDeinterlacer> Deinterlacer;

	/**
	 * The order in which interlace fields should be copied.
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Video")
	EMediaIOInterlaceFieldOrder InterlaceFieldOrder = EMediaIOInterlaceFieldOrder::TopFieldFirst;

public:
	//~ IMediaOptions interface
	using Super::GetMediaOption;
	virtual FString GetMediaOption(const FName& Key, const FString& DefaultValue) const override;
	virtual int64 GetMediaOption(const FName& Key, int64 DefaultValue) const override;
	virtual bool HasMediaOption(const FName& Key) const override;
};
