// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaSourceManagerInput.h"

#include "MediaSourceManagerInputMediaSource.generated.h"

UCLASS(BlueprintType)
class MEDIASOURCEMANAGER_API UMediaSourceManagerInputMediaSource : public UMediaSourceManagerInput
{
	GENERATED_BODY()

public:
	virtual FString GetDisplayName() override;
	virtual UMediaSource* GetMediaSource() override;

	/** The media source this input uses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputMediaSource")
	TObjectPtr<UMediaSource> MediaSource = nullptr;
};

