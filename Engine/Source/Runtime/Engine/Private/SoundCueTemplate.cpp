// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Sound/SoundCueTemplate.h"


USoundCueTemplate::USoundCueTemplate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void USoundCueTemplate::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		RebuildGraph(*this);
	}
}

void USoundCueTemplate::RebuildGraph(USoundCue& SoundCue) const
{
	SoundCue.ResetGraph();

	OnRebuildGraph(SoundCue);

	SoundCue.LinkGraphNodesFromSoundNodes();
	SoundCue.PostEditChange();
	SoundCue.MarkPackageDirty();
}
#endif // WITH_EDITOR
