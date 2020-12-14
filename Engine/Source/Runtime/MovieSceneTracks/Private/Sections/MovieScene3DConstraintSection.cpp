// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieScene3DConstraintSection.h"
#include "MovieSceneObjectBindingID.h"


UMovieScene3DConstraintSection::UMovieScene3DConstraintSection( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{ 
	bSupportsInfiniteRange = true;
}

void UMovieScene3DConstraintSection::OnBindingsUpdated(const TMap<FGuid, FGuid>& OldGuidToNewGuidMap)
{
	if (OldGuidToNewGuidMap.Contains(ConstraintBindingID.GetGuid()))
	{
		Modify();

		ConstraintBindingID.SetGuid(OldGuidToNewGuidMap[ConstraintBindingID.GetGuid()]);
	}
}

void UMovieScene3DConstraintSection::GetReferencedBindings(TArray<FGuid>& OutBindings)
{
	OutBindings.Add(ConstraintBindingID.GetGuid());
}

void UMovieScene3DConstraintSection::PostLoad()
{
	Super::PostLoad();

	if (ConstraintId_DEPRECATED.IsValid())
	{
		if (!ConstraintBindingID.IsValid())
		{
			ConstraintBindingID = UE::MovieScene::FRelativeObjectBindingID(ConstraintId_DEPRECATED);
		}
		ConstraintId_DEPRECATED.Invalidate();
	}
}
