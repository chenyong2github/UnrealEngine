// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModelTypeID.h"

class UMovieSceneTrack;
class ISequencerTrackEditor;

namespace UE
{
namespace Sequencer
{

class SEQUENCER_API ITrackExtension
{
public:

	UE_SEQUENCER_DECLARE_VIEW_MODEL_TYPE_ID(ITrackExtension)

	virtual ~ITrackExtension(){}

	virtual UMovieSceneTrack* GetTrack() const = 0;
	virtual int32 GetRowIndex() const = 0;

	virtual TSharedPtr<ISequencerTrackEditor> GetTrackEditor() const = 0;
};

} // namespace Sequencer
} // namespace UE

