// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Channels/ChannelCurveModel.h"
#include "IBufferedCurveModel.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneChannelHandle.h"

struct FMovieSceneBoolChannel;
class UMovieSceneSection;
class ISequencer;

class FBoolChannelCurveModel : public FChannelCurveModel<FMovieSceneBoolChannel, bool, bool>
{
public:
	FBoolChannelCurveModel(TMovieSceneChannelHandle<FMovieSceneBoolChannel> InChannel, UMovieSceneSection* InOwningSection, TWeakPtr<ISequencer> InWeakSequencer);

	// FCurveModel
	virtual void CreateKeyProxies(TArrayView<const FKeyHandle> InKeyHandles, TArrayView<UObject*> OutObjects) override;
	virtual TUniquePtr<IBufferedCurveModel> CreateBufferedCurveCopy() const override;

protected:

	// FChannelCurveModel
	virtual double GetKeyValue(TArrayView<const bool> Values, int32 Index) const override;
	virtual void SetKeyValue(int32 Index, double KeyValue) const override;
};