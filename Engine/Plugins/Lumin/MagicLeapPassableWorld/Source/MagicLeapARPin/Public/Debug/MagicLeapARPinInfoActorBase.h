// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GameFramework/Actor.h"
#include "IMagicLeapARPinFeature.h"
#include "MagicLeapARPinInfoActorBase.generated.h"

UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, Abstract)
class MAGICLEAPARPIN_API AMagicLeapARPinInfoActorBase : public AActor
{
	GENERATED_BODY()

public:
	AMagicLeapARPinInfoActorBase();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ContentPersistence|MagicLeap")
	FGuid PinID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ContentPersistence|MagicLeap")
	bool bVisibilityOverride;

	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "ContentPersistence|MagicLeap")
	void OnUpdateARPinState();
};
