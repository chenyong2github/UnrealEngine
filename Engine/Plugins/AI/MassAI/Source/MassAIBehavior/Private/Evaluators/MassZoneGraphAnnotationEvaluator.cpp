// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluators/MassZoneGraphAnnotationEvaluator.h"
#include "MassStateTreeExecutionContext.h"
#include "MassZoneGraphAnnotationFragments.h"


FMassZoneGraphAnnotationEvaluator::FMassZoneGraphAnnotationEvaluator()
{
}

bool FMassZoneGraphAnnotationEvaluator::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(AnnotationTagsFragmentHandle);

	Linker.LinkInstanceDataProperty(AnnotationTagsHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassZoneGraphAnnotationEvaluatorInstanceData, AnnotationTags));

	return true;
}

void FMassZoneGraphAnnotationEvaluator::Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) const
{
	const FMassZoneGraphAnnotationFragment& AnnotationTagsFragment = Context.GetExternalData(AnnotationTagsFragmentHandle);
	FZoneGraphTagMask& AnnotationTags = Context.GetInstanceData(AnnotationTagsHandle);
	AnnotationTags = AnnotationTagsFragment.Tags;
}
