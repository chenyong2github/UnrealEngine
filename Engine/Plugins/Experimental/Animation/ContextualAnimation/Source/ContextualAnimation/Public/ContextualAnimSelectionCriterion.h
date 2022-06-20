// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimSelectionCriterion.generated.h"

UENUM(BlueprintType)
enum class EContextualAnimCriterionType : uint8
{
	Spatial,
	Other
};

// UContextualAnimSelectionCriterion
//===========================================================================

UCLASS(Abstract, BlueprintType, EditInlineNew)
class CONTEXTUALANIMATION_API UContextualAnimSelectionCriterion : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	EContextualAnimCriterionType Type = EContextualAnimCriterionType::Spatial;

	UContextualAnimSelectionCriterion(const FObjectInitializer& ObjectInitializer);

	class UContextualAnimSceneAsset* GetSceneAssetOwner() const;

	virtual bool DoesQuerierPassCondition(const FContextualAnimSceneBindingContext& Primary, const FContextualAnimSceneBindingContext& Querier) const { return false; }
};

// UContextualAnimSelectionCriterion_TriggerArea
//===========================================================================

UCLASS()
class CONTEXTUALANIMATION_API UContextualAnimSelectionCriterion_TriggerArea : public UContextualAnimSelectionCriterion
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "Defaults", meta = (EditFixedOrder))
	TArray<FVector> PolygonPoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (ClampMin = "0", UIMin = "0"))
	float Height = 100.f;

	UContextualAnimSelectionCriterion_TriggerArea(const FObjectInitializer& ObjectInitializer);

	virtual bool DoesQuerierPassCondition(const FContextualAnimSceneBindingContext& Primary, const FContextualAnimSceneBindingContext& Querier) const override;
};

// UContextualAnimSelectionCriterion_Facing
//===========================================================================

UCLASS()
class CONTEXTUALANIMATION_API UContextualAnimSelectionCriterion_Facing : public UContextualAnimSelectionCriterion
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (ClampMin = "0", ClampMax = "180", UIMin = "0", UIMax = "180"))
	float MaxAngle = 0.f;

	UContextualAnimSelectionCriterion_Facing(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

	virtual bool DoesQuerierPassCondition(const FContextualAnimSceneBindingContext& Primary, const FContextualAnimSceneBindingContext& Querier) const override;
};