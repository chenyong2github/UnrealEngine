// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GameFramework/Actor.h"
#include "IMagicLeapARPinFeature.h"
#include "MagicLeapARPinRenderer.generated.h"

class AMagicLeapARPinInfoActorBase;

UCLASS(ClassGroup = MagicLeap, BlueprintType)
class MAGICLEAPARPIN_API AMagicLeapARPinRenderer : public AActor
{
	GENERATED_BODY()

public:
	AMagicLeapARPinRenderer();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetVisibilityOverride, Category = "ContentPersistence|MagicLeap")
	bool bInfoActorsVisibilityOverride;

private:
	void OnARPinsUpdated(const TArray<FGuid>& Added, const TArray<FGuid>& Updated, const TArray<FGuid>& Deleted);

	UFUNCTION(BlueprintSetter)
	void SetVisibilityOverride(const bool InVisibilityOverride);

	UPROPERTY()
	TMap<FGuid, AMagicLeapARPinInfoActorBase*> AllInfoActors;

	FDelegateHandle DelegateHandle;

	UPROPERTY()
	TSubclassOf<AMagicLeapARPinInfoActorBase> ClassToSpawn;
};
