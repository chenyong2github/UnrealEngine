// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneCameraShakeSourceTriggerSection.h"
#include "Channels/MovieSceneChannelProxy.h"

UMovieSceneCameraShakeSourceTriggerSection::UMovieSceneCameraShakeSourceTriggerSection(const FObjectInitializer& Init)
	: Super(Init)
{
#if WITH_EDITOR
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(Channel, FMovieSceneChannelMetaData());
#else
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(Channel);
#endif
}

