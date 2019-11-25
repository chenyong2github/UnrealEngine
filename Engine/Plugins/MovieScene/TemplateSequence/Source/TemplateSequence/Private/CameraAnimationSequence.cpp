// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CameraAnimationSequence.h"

UCameraAnimationSequence::UCameraAnimationSequence(const FObjectInitializer& ObjectInitializer)
    : UTemplateSequence(ObjectInitializer)
{
}

#if WITH_EDITOR
FText UCameraAnimationSequence::GetDisplayName() const
{
	return UMovieSceneSequence::GetDisplayName();
}
#endif
