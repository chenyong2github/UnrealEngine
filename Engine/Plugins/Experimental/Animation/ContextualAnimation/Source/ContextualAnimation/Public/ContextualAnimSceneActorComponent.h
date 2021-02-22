// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "ContextualAnimCompositeSceneAsset.h"
#include "ContextualAnimSceneActorComponent.generated.h"

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
class CONTEXTUALANIMATION_API UContextualAnimSceneActorComponent : public USphereComponent
{
	GENERATED_BODY()

public:

	UContextualAnimSceneActorComponent(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	UContextualAnimCompositeSceneAsset* SceneAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableDebug;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ShowOnlyInnerProperties, EditCondition = "bEnableDebug"))
	FContextualAnimDebugParams DebugParams;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Animation System")
	bool QueryData(const FContextualAnimQueryParams& QueryParams, FContextualAnimQueryResult& Result) const;
};
