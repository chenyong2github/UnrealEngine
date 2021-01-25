// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "ContextualAnimComponent.generated.h"

class UContextualAnimAsset;
struct FContextualAnimData;

USTRUCT(BlueprintType)
struct FContextualAnimEntryPoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TSoftObjectPtr<UAnimMontage> Animation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FTransform EntryTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FTransform SyncTransform;
};

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
class CONTEXTUALANIMATION_API UContextualAnimComponent : public UPrimitiveComponent
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

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const;

	const FContextualAnimData* FindBestDataForActor(const AActor* Querier) const;

	const FContextualAnimData* FindClosestDataForActor(const AActor* Querier) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Animation")
	bool FindBestEntryPointForActor(const AActor* Querier, FContextualAnimEntryPoint& Result) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Animation")
	bool FindClosestEntryPointForActor(const AActor* Querier, FContextualAnimEntryPoint& Result) const;
};
