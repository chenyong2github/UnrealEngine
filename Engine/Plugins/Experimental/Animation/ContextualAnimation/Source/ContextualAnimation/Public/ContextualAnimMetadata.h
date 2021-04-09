// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimMetadata.generated.h"

struct FContextualAnimData;

UCLASS(BlueprintType, EditInlineNew)
class CONTEXTUALANIMATION_API UContextualAnimMetadata : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FVector2D OriginOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (ClampMin = "-180", ClampMax = "180", UIMin = "-180", UIMax = "180"))
	float DirectionOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (ClampMin = "0", UIMin = "0"))
	float MaxDistance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (ClampMin = "0", UIMin = "0"))
	float NearWidth = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (ClampMin = "0", UIMin = "0"))
	float FarWidth = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (ClampMin = "-180", ClampMax = "180", UIMin = "-180", UIMax = "180"))
	float Facing = 0.f;

	UContextualAnimMetadata(const FObjectInitializer& ObjectInitializer);

	class UContextualAnimSceneAsset* GetSceneAssetOwner() const;

	bool DoesQuerierPassConditions(const FContextualAnimQuerier& Querier, const FContextualAnimQueryContext& Context, const FTransform& EntryTransform) const;

};