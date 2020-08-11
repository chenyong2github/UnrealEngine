// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieScenePreAnimatedPropertyHelper.h"


namespace UE
{
namespace MovieScene
{


TMap<uint16, FMovieSceneAnimTypeID> FGlobalPreAnimatedStateIDs::CustomGlobalPreAnimatedTypeID;
TMap<uint16, FMovieSceneAnimTypeID> FGlobalPreAnimatedStateIDs::FastGlobalPreAnimatedTypeID;
TMap<FName,  FMovieSceneAnimTypeID> FGlobalPreAnimatedStateIDs::SlowGlobalPreAnimatedTypeID;

FMovieSceneAnimTypeID FGlobalPreAnimatedStateIDs::GetCustom(uint16 CustomAccessorIndex)
{
	const FMovieSceneAnimTypeID* Existing = CustomGlobalPreAnimatedTypeID.Find(CustomAccessorIndex);
	if (Existing)
	{
		return *Existing;
	}
	return CustomGlobalPreAnimatedTypeID.Add(CustomAccessorIndex, FMovieSceneAnimTypeID::Unique());
}

FMovieSceneAnimTypeID FGlobalPreAnimatedStateIDs::GetFast(uint16 FastPropertyOffset)
{
	const FMovieSceneAnimTypeID* Existing = FastGlobalPreAnimatedTypeID.Find(FastPropertyOffset);
	if (Existing)
	{
		return *Existing;
	}
	return FastGlobalPreAnimatedTypeID.Add(FastPropertyOffset, FMovieSceneAnimTypeID::Unique());
}

FMovieSceneAnimTypeID FGlobalPreAnimatedStateIDs::GetSlow(FName PropertyPath)
{

	const FMovieSceneAnimTypeID* Existing = SlowGlobalPreAnimatedTypeID.Find(PropertyPath);
	if (Existing)
	{
		return *Existing;
	}
	return SlowGlobalPreAnimatedTypeID.Add(PropertyPath, FMovieSceneAnimTypeID::Unique());
}


} // namespace MovieScene
} // namespace UE


