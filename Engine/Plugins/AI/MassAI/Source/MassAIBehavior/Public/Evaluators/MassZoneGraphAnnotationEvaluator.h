// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "ZoneGraphTypes.h"
#include "MassZoneGraphAnnotationEvaluator.generated.h"

struct FStateTreeExecutionContext;

/**
 * Evaluator to expose ZoneGraph Annotation Tags for decision making.
 */
USTRUCT(meta = (DisplayName = "ZG Annotation Tags"))
struct MASSAIBEHAVIOR_API FMassZoneGraphAnnotationEvaluator : public FMassStateTreeEvaluatorBase
{
	GENERATED_BODY()

	FMassZoneGraphAnnotationEvaluator();

protected:
	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) override;

	UPROPERTY(meta=(BaseStruct="MassZoneGraphAnnotationTagsFragment"))
	FStateTreeExternalItemHandle BehaviorTagsHandle;

	UPROPERTY(EditAnywhere, Category = Parameters)
	FZoneGraphTagMask BehaviorTags = FZoneGraphTagMask::None;
};
