// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerChannelTraits.h"
#include "EditorStyleSet.h"
#include "CurveModel.h"

namespace Sequencer
{


void DrawKeys(FMovieSceneChannel* Channel, TArrayView<const FKeyHandle> InHandles, const UMovieSceneSection* InOwner, TArrayView<FKeyDrawParams> OutKeyDrawParams)
{
	// By default just render diamonds for keys
	FKeyDrawParams DefaultParams;
	DefaultParams.BorderBrush = DefaultParams.FillBrush = FEditorStyle::Get().GetBrush("Sequencer.KeyDiamond");

	for (FKeyDrawParams& Param : OutKeyDrawParams)
	{
		Param = DefaultParams;
	}
}

bool SupportsCurveEditorModels(const FMovieSceneChannelHandle& ChannelHandle)
{
	return false;
}

TUniquePtr<FCurveModel> CreateCurveEditorModel(const FMovieSceneChannelHandle& ChannelHandle, UMovieSceneSection* OwningSection, TSharedRef<ISequencer> InSequencer)
{
	return nullptr;
}

}	// namespace Sequencer