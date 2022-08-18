// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "MediaSourceManagerChannel.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "MediaSourceManager.generated.h"

/**
* Manager to handle media sources and their connections.
*/
UCLASS(BlueprintType)
class MEDIASOURCEMANAGER_API UMediaSourceManager : public UObject
{
	GENERATED_BODY()

public:
	/** Our channels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channels")
	TArray<TObjectPtr<UMediaSourceManagerChannel>> Channels;

	/**
	 * Call this to make sure everything is set up.
	 */
	void Validate();

};
