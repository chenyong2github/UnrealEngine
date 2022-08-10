// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModels/TrackModel.h"

class UMovieSceneCinematicShotTrack;

namespace UE
{
namespace Sequencer
{

class MOVIESCENETOOLS_API FCinematicShotTrackModel
	: public FTrackModel
{
public:

	UE_SEQUENCER_DECLARE_CASTABLE(FCinematicShotTrackModel, FTrackModel);

	static TSharedPtr<FTrackModel> CreateTrackModel(UMovieSceneTrack* Track);

	explicit FCinematicShotTrackModel(UMovieSceneCinematicShotTrack* Track);

	FSortingKey GetSortingKey() const override;
};

} // namespace Sequencer
} // namespace UE

