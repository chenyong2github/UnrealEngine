// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "ContextualAnimComponent.generated.h"

class UContextualAnimAsset;
struct FContextualAnimData;

/** Stores the result of a query function */
USTRUCT(BlueprintType)
struct FContextualAnimQueryResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Defaults")
	TSoftObjectPtr<UAnimMontage> Animation;

	UPROPERTY(BlueprintReadWrite, Category = "Defaults")
	FTransform EntryTransform;

	UPROPERTY(BlueprintReadWrite, Category = "Defaults")
	FTransform SyncTransform;

	UPROPERTY(BlueprintReadWrite, Category = "Defaults")
	float AnimStartTime = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Defaults")
	int32 DataIndex = INDEX_NONE;

	void Reset()
	{
		Animation = nullptr;
		EntryTransform = SyncTransform = FTransform::Identity;
		AnimStartTime = 0.f;
		DataIndex = INDEX_NONE;
	}
};

/** Stores the parameters passed into query function */
USTRUCT(BlueprintType)
struct FContextualAnimQueryParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TWeakObjectPtr<const AActor> Querier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	bool bComplexQuery;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	bool bFindAnimStartTime;

	FContextualAnimQueryParams()
		: Querier(nullptr), bComplexQuery(false), bFindAnimStartTime(false){}

	FContextualAnimQueryParams(const AActor* InQuerier, bool bInComplexQuery, bool bInFindAnimStartTime)
		: Querier(InQuerier), bComplexQuery(bInComplexQuery), bFindAnimStartTime(bInFindAnimStartTime) {}
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
};
