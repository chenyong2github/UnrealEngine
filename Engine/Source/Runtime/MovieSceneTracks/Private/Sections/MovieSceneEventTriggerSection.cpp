// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneEventTriggerSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Engine/Blueprint.h"


UMovieSceneEventTriggerSection::UMovieSceneEventTriggerSection(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	bSupportsInfiniteRange = true;
	SetRange(TRange<FFrameNumber>::All());

#if WITH_EDITOR

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(EventChannel, FMovieSceneChannelMetaData());

#else

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(EventChannel);

#endif
}
