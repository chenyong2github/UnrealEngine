// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "ContextualAnimAsset.h"
#include "ContextualAnimComponent.generated.h"

class UAnimInstance;
class UAnimMontage;
class AActor;

USTRUCT(BlueprintType)
struct FContextualAnimDebugParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	TWeakObjectPtr<AActor> TestActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (UIMin = 0, ClampMin = 0))
	float DrawAlignmentTransformAtTime = 0.f;
};

UCLASS(meta = (BlueprintSpawnableComponent))
class CONTEXTUALANIMATION_API UContextualAnimComponent : public USphereComponent
{
	GENERATED_BODY()

public:

	UContextualAnimComponent(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	UContextualAnimAsset* ContextualAnimAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableDebug;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ShowOnlyInnerProperties, EditCondition = "bEnableDebug"))
	FContextualAnimDebugParams DebugParams;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Animation")
	bool QueryData(const FContextualAnimQueryParams& QueryParams, FContextualAnimQueryResult& Result) const;

	bool TryStartContextualAnimation(AActor* Actor, const FContextualAnimQueryResult& Data);

	bool TryEndContextualAnimation(AActor* Actor);

	bool IsActorPlayingContextualAnimation(AActor* Actor) const;

	void SetIgnoreOwnerComponentsWhenMovingForActor(AActor* Actor, bool bShouldIgnore);

	UAnimInstance* GetAnimInstanceForActor(AActor* Actor) const;

protected:

	UPROPERTY()
	TMap<UAnimMontage*, AActor*> MontageToActorMap;

	UFUNCTION()
	void OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

};
