// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModels/ViewModel.h"
#include "MVVM/Extensions/IOutlinerExtension.h"
#include "MVVM/Extensions/IGeometryExtension.h"
#include "MVVM/ViewModels/OutlinerItemModel.h"
#include "MVVM/Extensions/LinkedOutlinerExtension.h"
#include "MVVM/Extensions/ITrackAreaExtension.h"
#include "MVVM/Extensions/IDeletableExtension.h"
#include "EventHandlers/ISignedObjectEventHandler.h"
#include "Tree/ICurveEditorTreeItem.h"

#include "UObject/NameTypes.h"
#include "Internationalization/Text.h"

struct FMovieSceneChannel;
struct FMovieSceneChannelHandle;

class IKeyArea;
class ISequencerSection;
class UMovieSceneSection;
class FSequencerSectionKeyAreaNode;

namespace UE
{
namespace Sequencer
{

class FSectionModel;

/**
 * Model for a single channel inside a section.
 * For instance, this represents the "Location.X" channel of a single transform section.
 */
class FChannelModel
	: public FViewModel
	, public FLinkedOutlinerExtension
	, public FGeometryExtensionShim
	, public FLinkedOutlinerComputedSizingShim
{
public:

	UE_SEQUENCER_DECLARE_CASTABLE(FChannelModel, FViewModel, FLinkedOutlinerExtension, IGeometryExtension);

	FChannelModel(FName InChannelName, TWeakPtr<ISequencerSection> InSection, FMovieSceneChannelHandle InChannel);
	~FChannelModel();

	void Initialize(TWeakPtr<ISequencerSection> InSection, FMovieSceneChannelHandle InChannel);

	/** Returns the section object that owns the associated channel */
	UMovieSceneSection* GetSection() const;

	/** Returns the associated channel object */
	FMovieSceneChannel* GetChannel() const;

	/** Returns whether this channel has any keyframes on it */
	bool IsAnimated() const;

	/** Returns the channel's name */
	FName GetChannelName() const { return ChannelName; }

	/** Returns the key area for the channel */
	TSharedPtr<IKeyArea> GetKeyArea() const { return KeyArea; }

	/** Create the curve editor model for the associated channel */
	void CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels);

	/** Returns the desired sizing for the track area row */
	FOutlinerSizing GetDesiredSizing() const;

private:

	TSharedPtr<IKeyArea> KeyArea;
	FName ChannelName;
};

/**
 * Model for the outliner entry associated with all sections' channels of a given common name.
 * For instance, this represents the "Location.X" entry in the Sequencer outliner.
 */
class FChannelGroupModel
	: public FViewModel
	, public ITrackAreaExtension
{
public:

	UE_SEQUENCER_DECLARE_CASTABLE(FChannelGroupModel, FViewModel, ITrackAreaExtension);

	FChannelGroupModel(FName InChannelName, const FText& InDisplayText);
	~FChannelGroupModel();

	/** Returns whether any of the channels within this group have any keyframes on them */
	bool IsAnimated() const;

	/** Returns the common name for all channels in this group */
	FName GetChannelName() const { return ChannelName; }

	/** Returns the label for this group */
	FText GetDisplayText() const { return DisplayText; }

	/** Gets all the channel models in this group */
	TArrayView<const TWeakViewModelPtr<FChannelModel>> GetChannels() const;
	/** Adds a channel model to this group */
	void AddChannel(TWeakViewModelPtr<FChannelModel> InChannel);

	/** Get the key area of the channel associated with the given section */
	TSharedPtr<IKeyArea> GetKeyArea(TSharedPtr<FSectionModel> InOwnerSection) const;

	/** Get the key area of the channel associated with the given section */
	TSharedPtr<IKeyArea> GetKeyArea(const UMovieSceneSection* InOwnerSection) const;

	/** Get the channel model of the channel associated with the given section */
	TSharedPtr<FChannelModel> GetChannel(TSharedPtr<FSectionModel> InOwnerSection) const;

	/** Get the channel model of the channel associated with the given section */
	TSharedPtr<FChannelModel> GetChannel(const UMovieSceneSection* InOwnerSection) const;

	/** Get the key areas of all channels */
	TArray<TSharedRef<IKeyArea>> GetAllKeyAreas() const;

public:

	/*~ ITrackAreaExtension */
	FTrackAreaParameters GetTrackAreaParameters() const override;
	FViewModelVariantIterator GetTrackAreaModelList() const override;

	void CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels);
	bool HasCurves() const;

protected:

	TArray<TWeakViewModelPtr<FChannelModel>> Channels;
	FName ChannelName;
	FText DisplayText;
};


/**
 * Model for the outliner entry associated with all sections' channels of a given common name.
 * For instance, this represents the "Location.X" entry in the Sequencer outliner.
 */
class FChannelGroupOutlinerModel
	: public TOutlinerModelMixin<FChannelGroupModel>
	, public ICompoundOutlinerExtension
	, public IDeletableExtension
{
public:

	UE_SEQUENCER_DECLARE_CASTABLE(FChannelGroupOutlinerModel, FChannelGroupModel, FOutlinerItemModelMixin, ICompoundOutlinerExtension, IDeletableExtension);

	FChannelGroupOutlinerModel(FName InChannelName, const FText& InDisplayText);
	~FChannelGroupOutlinerModel();

public:

	/*~ ICompoundOutlinerExtension */
	FOutlinerSizing RecomputeSizing() override;

	/*~ IOutlinerExtension */
	FOutlinerSizing GetOutlinerSizing() const override;
	FText GetLabel() const override;
	FSlateFontInfo GetLabelFont() const override;
	TSharedRef<SWidget> CreateOutlinerView(const FCreateOutlinerViewParams& InParams) override;

	/*~ ICurveEditorTreeItem */
	void CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels) override;

	/*~ IDeletableExtension */
	bool CanDelete(FText* OutErrorMessage) const override;
	void Delete() override;

	/*~ ICurveEditorTreeItemExtension */
	bool HasCurves() const override;

private:

	EVisibility GetKeyEditorVisibility() const;
	FOutlinerSizing ComputedSizing;
};


} // namespace Sequencer
} // namespace UE

