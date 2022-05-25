// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/List.h"
#include "EventHandlers/MovieSceneDataEventContainer.h"

struct FGuid;
struct FMovieSceneBinding;

class UMovieScene;
class UMovieSceneTrack;
class UMovieSceneFolder;

namespace UE
{
namespace MovieScene
{

class ISequenceDataEventHandler
{
public:

	virtual void OnMasterTrackAdded(UMovieSceneTrack*)     {}

	virtual void OnMasterTrackRemoved(UMovieSceneTrack*)   {}

	virtual void OnBindingAdded(const FMovieSceneBinding&) {}

	virtual void OnBindingRemoved(const FGuid&)            {}

	virtual void OnRootFolderAdded(UMovieSceneFolder*)     {}

	virtual void OnRootFolderRemoved(UMovieSceneFolder*)   {}

	virtual void OnTrackAddedToBinding(UMovieSceneTrack* Track, const FGuid& ObjectBindingID) {}

	virtual void OnTrackRemovedFromBinding(UMovieSceneTrack* Track, const FGuid& ObjectBindingID) {}

	virtual void OnBindingParentChanged(const FGuid& ObjectBindingID, const FGuid& NewParent) {}

};

} // namespace MovieScene
} // namespace UE

