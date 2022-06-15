// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Channels/MovieSceneBoolChannel.h"
#include "MovieSceneClipboard.h"

#include "MovieSceneConstraintChannel.generated.h"

USTRUCT()
struct MOVIESCENE_API FMovieSceneConstraintChannel : public FMovieSceneBoolChannel
{
	GENERATED_BODY()

	FMovieSceneConstraintChannel() {};

	// @todo 
	bool Evaluate(FFrameTime InTime, bool& OutValue) const;
};

template<>
struct TStructOpsTypeTraits<FMovieSceneConstraintChannel> : public TStructOpsTypeTraitsBase2<FMovieSceneConstraintChannel>
{
	enum { WithStructuredSerializeFromMismatchedTag = true };
};


template<>
struct TMovieSceneChannelTraits<FMovieSceneConstraintChannel> : TMovieSceneChannelTraitsBase<FMovieSceneConstraintChannel>
{
	enum { SupportsDefaults = false };

#if WITH_EDITOR

	/** Byte channels can have external values (ie, they can get their values from external objects for UI purposes) */
	typedef TMovieSceneExternalValue<bool> ExtendedEditorDataType;

#endif
};

// #if WITH_EDITOR
// namespace MovieSceneClipboard
// {
// 	template<> inline FName GetKeyTypeName<bool>()
// 	{
// 		static FName Name("Bool");
// 		return Name;
// 	}
// }
// #endif
