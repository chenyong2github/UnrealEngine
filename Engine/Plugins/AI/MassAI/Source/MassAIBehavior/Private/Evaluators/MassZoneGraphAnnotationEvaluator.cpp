// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluators/MassZoneGraphAnnotationEvaluator.h"
#include "MassStateTreeExecutionContext.h"
#include "MassZoneGraphAnnotationFragments.h"


FMassZoneGraphAnnotationEvaluator::FMassZoneGraphAnnotationEvaluator()
{
}

void FMassZoneGraphAnnotationEvaluator::Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime)
{
	const FMassZoneGraphAnnotationTagsFragment& BehaviorTagsFragment = Context.GetExternalItem(BehaviorTagsHandle).Get<FMassZoneGraphAnnotationTagsFragment>();
	BehaviorTags = BehaviorTagsFragment.Tags;
}
