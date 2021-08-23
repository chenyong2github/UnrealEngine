// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Channels/BezierChannelCurveModel.h"
#include "Channels/MovieSceneFloatChannel.h"

struct FMovieSceneFloatChannel;

class FFloatChannelCurveModel : public FBezierChannelCurveModel<FMovieSceneFloatChannel, FMovieSceneFloatValue, float>
{
public:
	FFloatChannelCurveModel(TMovieSceneChannelHandle<FMovieSceneFloatChannel> InChannel, UMovieSceneSection* InOwningSection, TWeakPtr<ISequencer> InWeakSequencer);

	// FCurveModel
	virtual void CreateKeyProxies(TArrayView<const FKeyHandle> InKeyHandles, TArrayView<UObject*> OutObjects) override;
	virtual TUniquePtr<IBufferedCurveModel> CreateBufferedCurveCopy() const override;
};