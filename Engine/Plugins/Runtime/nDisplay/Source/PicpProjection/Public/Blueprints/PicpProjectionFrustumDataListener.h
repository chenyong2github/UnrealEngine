// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprints/PicpProjectionFrustumData.h"
#include "UObject/Interface.h"
#include "PicpProjectionFrustumDataListener.generated.h"


UINTERFACE()
class UPicpProjectionFrustumDataListener
	: public UInterface
{
	GENERATED_BODY()

public:
	UPicpProjectionFrustumDataListener(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
	}
};


/**
 * Interface for cluster event listeners
 */
class IPicpProjectionFrustumDataListener
{
	GENERATED_BODY()

public:
	// React on incoming cluster events
	UFUNCTION(BlueprintImplementableEvent, Category = "PICP")
	void OnProjectionDataUpdate(const FPicpProjectionFrustumData& ProjectionData);
};
