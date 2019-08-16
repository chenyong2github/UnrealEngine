// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SequencerTrackFilterBase.h"
#include "Misc/IFilter.h"
#include "Misc/FilterCollection.h"
#include "CollectionManagerTypes.h"
#include "Styling/SlateIconFinder.h"
#include "EditorStyleSet.h"
#include "SequencerDisplayNode.h"
#include "MovieSceneSequence.h"

#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneLevelVisibilityTrack.h"
#include "Tracks/MovieSceneParticleTrack.h"

#include "Camera/CameraComponent.h"
#include "Components/LightComponentBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Particles/ParticleSystem.h"

class UWorld;

#define LOCTEXT_NAMESPACE "Sequencer"

class FSequencerTrackFilterCollection : public TFilterCollection<FTrackFilterType>
{
public:
	/**
	 *	Returns whether the specified Item passes any of the filters in the collection
	 *
	 *	@param	InItem	The Item to check against all child filter restrictions
	 *	@return			Whether the Item passed any child filter restrictions
	 */
	 // @todo Maybe this should get moved in to TFilterCollection
	bool PassesAnyFilters(/*ItemType*/ FTrackFilterType InItem) const
	{
		for (int Index = 0; Index < ChildFilters.Num(); Index++)
		{
			if (ChildFilters[Index]->PassesFilter(InItem))
			{
				return true;
			}
		}

		return false;
	}

	// @todo Maybe this should get moved in to TFilterCollection
	bool Contains(const TSharedPtr< IFilter< FTrackFilterType > >& InItem) const
	{
		for (const TSharedPtr<IFilter<FTrackFilterType>>& Filter : ChildFilters)
		{
			if (InItem == Filter)
			{
				return true;
			}
		}
		return false;
	}

	// @todo Maybe this should get moved in to TFilterCollection
	void RemoveAll()
	{
		for (auto Iterator = ChildFilters.CreateIterator(); Iterator; ++Iterator)
		{
			const TSharedPtr< IFilter< FTrackFilterType > >& Filter = *Iterator;

			if (Filter.IsValid())
			{
				Filter->OnChanged().RemoveAll(this);
			}
		}

		ChildFilters.Empty();

		ChangedEvent.Broadcast();
	}
};

class FSequencerTrackFilter_AudioTracks : public FSequencerTrackFilter_ClassType< UMovieSceneAudioTrack >
{
	virtual FString GetName() const override { return TEXT("SequencerAudioTracksFilter"); }
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_AudioTracks", "Audio"); }
	virtual FText GetToolTipText() const override { return LOCTEXT("SequencerTrackFilter_AudioTracksToolTip", "Show only Audio tracks."); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Tracks.Audio"); }

	virtual bool SupportsSequence(UMovieSceneSequence* InSequence) const override
	{
		static UClass* LevelSequenceClass = FindObject<UClass>(ANY_PACKAGE, TEXT("LevelSequence"), true);
		static UClass* WidgetAnimationClass = FindObject<UClass>(ANY_PACKAGE, TEXT("WidgetAnimation"), true);
		return InSequence != nullptr &&
			((LevelSequenceClass != nullptr && InSequence->GetClass()->IsChildOf(LevelSequenceClass)) ||
			(WidgetAnimationClass != nullptr && InSequence->GetClass()->IsChildOf(WidgetAnimationClass)));
	}
};

class FSequencerTrackFilter_EventTracks : public FSequencerTrackFilter_ClassType< UMovieSceneEventTrack >
{
	virtual FString GetName() const override { return TEXT("SequencerEventTracksFilter"); }
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_EventTracks", "Event"); }
	virtual FText GetToolTipText() const override { return LOCTEXT("SequencerTrackFilter_EventTracksToolTip", "Show only Event tracks."); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Tracks.Event"); }

	virtual bool SupportsSequence(UMovieSceneSequence* InSequence) const override
	{
		static UClass* LevelSequenceClass = FindObject<UClass>(ANY_PACKAGE, TEXT("LevelSequence"), true);
		static UClass* WidgetAnimationClass = FindObject<UClass>(ANY_PACKAGE, TEXT("WidgetAnimation"), true);
		return InSequence != nullptr &&
			((LevelSequenceClass != nullptr && InSequence->GetClass()->IsChildOf(LevelSequenceClass)) ||
			(WidgetAnimationClass != nullptr && InSequence->GetClass()->IsChildOf(WidgetAnimationClass)));
	}
};

class FSequencerTrackFilter_LevelVisibilityTracks : public FSequencerTrackFilter_ClassType< UMovieSceneLevelVisibilityTrack >
{
	virtual FString GetName() const override { return TEXT("SequencerLevelVisibilityTracksFilter"); }
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_LevelVisibilityTracks", "Level Visibility"); }
	virtual FText GetToolTipText() const override { return LOCTEXT("SequencerTrackFilter_LevelVisibilityTracksToolTip", "Show only Level Visibility tracks."); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Tracks.LevelVisibility"); }
};

class FSequencerTrackFilter_ParticleTracks : public FSequencerTrackFilter_ClassType< UMovieSceneParticleTrack >
{
	virtual FString GetName() const override { return TEXT("SequencerParticleTracksFilter"); }
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_ParticleTracks", "Particle Systems"); }
	virtual FText GetToolTipText() const override { return LOCTEXT("SequencerTrackFilter_ParticleTracksToolTip", "Show only Particle System tracks."); }
	virtual FSlateIcon GetIcon() const override { return FSlateIconFinder::FindIconForClass(UParticleSystem::StaticClass()); }
};

class FSequencerTrackFilter_SkeletalMeshObjects : public FSequencerTrackFilter_ComponentType< USkeletalMeshComponent >
{
	virtual FString GetName() const override { return TEXT("SequencerSkeletalMeshObjectsFilter"); }
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_SkeletalMeshObjects", "Skeletal Mesh"); }
	virtual FText GetToolTipText() const override { return LOCTEXT("SequencerTrackFilter_SkeletalMeshObjectsToolTip", "Show only SkeletalMesh objects."); }
	virtual FSlateIcon GetIcon() const override { return FSlateIconFinder::FindIconForClass(USkeletalMeshComponent::StaticClass()); }
};

class FSequencerTrackFilter_CameraObjects : public FSequencerTrackFilter_ComponentType< UCameraComponent >
{
	virtual FString GetName() const override { return TEXT("SequencerCameraObjectsFilter"); }
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_CameraObjects", "Cameras"); }
	virtual FText GetToolTipText() const override { return LOCTEXT("SequencerTrackFilter_CameraObjectsToolTip", "Show only Camera objects."); }
	virtual FSlateIcon GetIcon() const override { return FSlateIconFinder::FindIconForClass(UCameraComponent::StaticClass()); }
};

class FSequencerTrackFilter_LightObjects : public FSequencerTrackFilter_ComponentType< ULightComponentBase >
{
	virtual FString GetName() const override { return TEXT("SequencerLightObjectsFilter"); }
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_LightObjects", "Lights"); }
	virtual FText GetToolTipText() const override { return LOCTEXT("SequencerTrackFilter_LightObjectsToolTip", "Show only Light objects."); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.Light"); }
};

class FSequencerTrackFilter_LevelFilter : public FSequencerTrackFilter
{
public:
	~FSequencerTrackFilter_LevelFilter();

	virtual bool PassesFilter(FTrackFilterType InItem) const override;

	virtual FString GetName() const override { return TEXT("SequencerSubLevelFilter"); }
	virtual FText GetDisplayName() const override { return FText::GetEmpty(); }
	virtual FText GetToolTipText() const override { return FText::GetEmpty(); }

	void UpdateWorld(UWorld* World);
	void ResetFilter();

	bool IsActive() const { return HiddenLevels.Num() > 0; }

	bool IsLevelHidden(const FString& LevelName) const;
	void HideLevel(const FString& LevelName);
	void UnhideLevel(const FString& LevelName);

private:
	void HandleLevelsChanged();

	// List of sublevels which should not pass filter
	TArray<FString> HiddenLevels;

	TWeakObjectPtr<UWorld> CachedWorld;
};

#undef LOCTEXT_NAMESPACE