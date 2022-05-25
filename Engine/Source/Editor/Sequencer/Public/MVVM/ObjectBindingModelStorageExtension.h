// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Guid.h"
#include "MVVM/Extensions/DynamicExtensionContainer.h"
#include "EventHandlers/ISequenceDataEventHandler.h"

struct FMovieSceneBinding;

namespace UE
{
namespace Sequencer
{

struct FViewModelChildren;

class FSequenceModel;
class FViewModel;
class FObjectBindingModel;
class FPlaceholderObjectBindingModel;

class FObjectBindingModelStorageExtension
	: public IDynamicExtension
	, private UE::MovieScene::TIntrusiveEventHandler<UE::MovieScene::ISequenceDataEventHandler>
{
public:

	UE_SEQUENCER_DECLARE_VIEW_MODEL_TYPE_ID(FObjectBindingModelStorageExtension);

	FObjectBindingModelStorageExtension();

	TSharedPtr<FViewModel> GetOrCreateModelForBinding(const FGuid& Binding);
	TSharedPtr<FViewModel> GetOrCreateModelForBinding(const FMovieSceneBinding& Binding);

	TSharedPtr<FObjectBindingModel> FindModelForObjectBinding(const FGuid& InObjectBindingID) const;

	virtual void OnCreated(TSharedRef<FViewModel> InWeakOwner) override;
	virtual void OnReinitialize() override;

private:

	void OnBindingAdded(const FMovieSceneBinding& Binding) override;
	void OnBindingRemoved(const FGuid& ObjectBindingID) override;
	void OnTrackAddedToBinding(UMovieSceneTrack* Track, const FGuid& Binding) override;
	void OnTrackRemovedFromBinding(UMovieSceneTrack* Track, const FGuid& Binding) override;
	void OnBindingParentChanged(const FGuid& Binding, const FGuid& NewParent) override;


	TSharedPtr<FObjectBindingModel> CreateModelForObjectBinding(const FMovieSceneBinding& Binding);

	TSharedPtr<FViewModel> CreatePlaceholderForObjectBinding(const FGuid& ObjectID);
	TSharedPtr<FViewModel> FindPlaceholderForObjectBinding(const FGuid& InObjectBindingID) const;

	/**
	 * Implementation function for creating a new model for a binding
	 * @param Binding The binding to create a model for
	 * @param RootChildren (optional) When specified, will place the object into this child list, if it does not define a desired parent ID.
	 */
	TSharedPtr<FViewModel> GetOrCreateModelForBinding(const FMovieSceneBinding& Binding, FViewModelChildren* RootChildren);

	void Compact();

private:

	TMap<FGuid, TWeakPtr<FObjectBindingModel>> ObjectBindingToModel;
	TMap<FGuid, TWeakPtr<FPlaceholderObjectBindingModel>> ObjectBindingToPlaceholder;

	FSequenceModel* OwnerModel;
};

} // namespace Sequencer
} // namespace UE

