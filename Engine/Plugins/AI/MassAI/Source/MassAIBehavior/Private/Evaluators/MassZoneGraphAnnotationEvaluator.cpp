// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluators/MassZoneGraphAnnotationEvaluator.h"
#include "MassStateTreeExecutionContext.h"
#include "MassZoneGraphAnnotationFragments.h"
#include "StateTreeLinker.h"


FMassZoneGraphAnnotationEvaluator::FMassZoneGraphAnnotationEvaluator()
{
}

bool FMassZoneGraphAnnotationEvaluator::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(AnnotationTagsFragmentHandle);

	Linker.LinkInstanceDataProperty(AnnotationTagsHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassZoneGraphAnnotationEvaluatorInstanceData, AnnotationTags));

	return true;
}

void FMassZoneGraphAnnotationEvaluator::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FMassZoneGraphAnnotationFragment& AnnotationTagsFragment = Context.GetExternalData(AnnotationTagsFragmentHandle);
	FZoneGraphTagMask& AnnotationTags = Context.GetInstanceData(AnnotationTagsHandle);
	AnnotationTags = AnnotationTagsFragment.Tags;
}
