// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimMetadata.generated.h"

struct FContextualAnimData;

USTRUCT(BlueprintType)
struct FContextualAnimDistanceParam
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (UIMin = 0, ClampMin = 0))
	float Value = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (UIMin = 0, ClampMin = 0))
	float MinDistance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (UIMin = 0, ClampMin = 0))
	float MaxDistance = 0.f;
};

USTRUCT(BlueprintType)
struct FContextualAnimAngleParam
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Defaults")
	float Value = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	float Tolerance = 0.f;
};

UCLASS(BlueprintType, EditInlineNew)
class CONTEXTUALANIMATION_API UContextualAnimMetadata : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	float OffsetFromOrigin = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FContextualAnimDistanceParam Distance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FContextualAnimAngleParam Angle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FContextualAnimAngleParam Facing;

	UContextualAnimMetadata(const FObjectInitializer& ObjectInitializer);

	class UContextualAnimSceneAssetBase* GetSceneAssetOwner() const;
};