// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotFilters.h"

EFilterResult::Type ULevelSnapshotFilter::IsActorValid(const FIsActorValidParams& Params) const
{
	return EFilterResult::DoNotCare;
}

EFilterResult::Type ULevelSnapshotFilter::IsPropertyValid(const FIsPropertyValidParams& Params) const
{
	return EFilterResult::DoNotCare;
}

EFilterResult::Type ULevelSnapshotBlueprintFilter::IsActorValid_Implementation(const FIsActorValidParams& Params) const
{
	return EFilterResult::DoNotCare;
}

EFilterResult::Type ULevelSnapshotBlueprintFilter::IsPropertyValid_Implementation(const FIsPropertyValidParams& Params) const
{
	return EFilterResult::DoNotCare;
}
