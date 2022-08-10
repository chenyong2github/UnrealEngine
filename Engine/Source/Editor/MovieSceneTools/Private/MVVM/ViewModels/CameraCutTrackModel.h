// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModels/TrackModel.h"

class UMovieSceneCameraCutTrack;

namespace UE
{
namespace Sequencer
{

class MOVIESCENETOOLS_API FCameraCutTrackModel
	: public FTrackModel
{
public:

	UE_SEQUENCER_DECLARE_CASTABLE(FCameraCutTrackModel, FTrackModel);

	static TSharedPtr<FTrackModel> CreateTrackModel(UMovieSceneTrack* Track);

	explicit FCameraCutTrackModel(UMovieSceneCameraCutTrack* Track);

	FSortingKey GetSortingKey() const override;
};

} // namespace Sequencer
} // namespace UE

