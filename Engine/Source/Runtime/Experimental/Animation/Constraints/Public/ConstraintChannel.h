// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneChannelEditorData.h"
#include "Channels/MovieSceneChannelTraits.h"
#include "ConstraintsManager.h"
#include "Containers/UnrealString.h"
#include "Misc/FrameTime.h"
#include "MovieSceneClipboard.h"
#include "Templates/Function.h"
#include "UObject/Class.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#include "ConstraintChannel.generated.h"

USTRUCT()
struct CONSTRAINTS_API FMovieSceneConstraintChannel : public FMovieSceneBoolChannel
{
	GENERATED_BODY()

	FMovieSceneConstraintChannel() {};

	// @todo 
	bool Evaluate(FFrameTime InTime, bool& OutValue) const;

#if WITH_EDITOR
	using ExtraLabelFunction = TFunction< FString() >;
	ExtraLabelFunction ExtraLabel;
#endif
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


USTRUCT()
struct CONSTRAINTS_API FConstraintAndActiveChannel
{
	GENERATED_USTRUCT_BODY()

	FConstraintAndActiveChannel() {}
	FConstraintAndActiveChannel(const TObjectPtr<UTickableConstraint>& InConstraint)
		: Constraint(InConstraint)
	{};

	UPROPERTY()
	TSoftObjectPtr<UTickableConstraint> Constraint;

	UPROPERTY()
	FMovieSceneConstraintChannel ActiveChannel;
};