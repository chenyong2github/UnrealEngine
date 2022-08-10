// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectKey.h"
#include "MVVM/Extensions/DynamicExtensionContainer.h"

class UMovieSceneTrack;

namespace UE
{
namespace Sequencer
{

class FTrackRowModel;
class FSequenceModel;
class FViewModel;

class FTrackRowModelStorageExtension
	: public IDynamicExtension
{
public:

	UE_SEQUENCER_DECLARE_VIEW_MODEL_TYPE_ID(FTrackRowModelStorageExtension)

	FTrackRowModelStorageExtension();

	virtual void OnCreated(TSharedRef<FViewModel> InWeakOwner) override;
	virtual void OnReinitialize() override;

	TSharedPtr<FTrackRowModel> CreateModelForTrackRow(UMovieSceneTrack* InTrack, int32 InRowIndex, TSharedPtr<FViewModel> DesiredParent = nullptr);

	TSharedPtr<FTrackRowModel> FindModelForTrackRow(UMovieSceneTrack* InTrack, int32 InRowIndex) const;

private:

	using KeyType = TTuple<FObjectKey, int32>;

	TMap<KeyType, TWeakPtr<FTrackRowModel>> TrackToModel;

	FSequenceModel* OwnerModel;
};

} // namespace Sequencer
} // namespace UE

