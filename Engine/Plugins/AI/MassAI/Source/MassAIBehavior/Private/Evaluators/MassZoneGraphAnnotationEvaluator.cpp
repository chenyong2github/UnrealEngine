// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluators/MassZoneGraphAnnotationEvaluator.h"
#include "MassStateTreeExecutionContext.h"
#include "MassZoneGraphAnnotationFragments.h"


FMassZoneGraphAnnotationEvaluator::FMassZoneGraphAnnotationEvaluator()
{
}

bool FMassZoneGraphAnnotationEvaluator::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalItem(BehaviorTagsHandle);

	return true;
}

void FMassZoneGraphAnnotationEvaluator::Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime)
{
	const FMassZoneGraphAnnotationTagsFragment& BehaviorTagsFragment = Context.GetExternalItem(BehaviorTagsHandle);
	BehaviorTags = BehaviorTagsFragment.Tags;
}
