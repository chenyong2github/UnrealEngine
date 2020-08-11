// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Channels/ChannelCurveModel.h"
#include "IBufferedCurveModel.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneChannelHandle.h"

struct FMovieSceneIntegerChannel;
class UMovieSceneSection;
class ISequencer;

class FIntegerChannelCurveModel : public FChannelCurveModel<FMovieSceneIntegerChannel, int32, int32>
{
public:
	FIntegerChannelCurveModel(TMovieSceneChannelHandle<FMovieSceneIntegerChannel> InChannel, UMovieSceneSection* InOwningSection, TWeakPtr<ISequencer> InWeakSequencer);

	// FCurveModel
	virtual void CreateKeyProxies(TArrayView<const FKeyHandle> InKeyHandles, TArrayView<UObject*> OutObjects) override;
	virtual TUniquePtr<IBufferedCurveModel> CreateBufferedCurveCopy() const override;

protected:

	// FChannelCurveModel
	virtual double GetKeyValue(TArrayView<const int32> Values, int32 Index) const override;
	virtual void SetKeyValue(int32 Index, double KeyValue) const override;
};