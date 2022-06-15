// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "MediaSourceManagerChannel.generated.h"

class UMediaSource;
class UMediaSourceManagerInput;
class UProxyMediaSource;

/**
* Handles a single channel for the MediaSourceManager.
*/
UCLASS(BlueprintType)
class MEDIASOURCEMANAGER_API UMediaSourceManagerChannel : public UObject
{
	GENERATED_BODY()

public:
	/** The name of this channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel")
	FString Name;

	/** Our input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel")
	TObjectPtr<UMediaSourceManagerInput> Input = nullptr;

	/** Our media sources for our inputs. */
	TObjectPtr<UMediaSource> InMediaSource = nullptr;

	/** Our persistent media source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel")
	TObjectPtr<UProxyMediaSource> OutMediaSource = nullptr;

	//~ Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject interface

};
