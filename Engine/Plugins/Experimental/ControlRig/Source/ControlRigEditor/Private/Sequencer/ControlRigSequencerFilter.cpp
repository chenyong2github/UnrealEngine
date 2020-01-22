// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigSequencerFilter.h"
#include "ControlRig.h"
#include "ControlRigSkeletalMeshComponent.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Styling/SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "ControlRigSequencerTrackFilters"

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_ControlRigControls : public  FSequencerTrackFilter
{
	virtual FString GetName() const override { return TEXT("ControlRigControlsFilter"); }
	virtual FText GetDisplayName() const override { return LOCTEXT("SequenceTrackFilter_ControlRigControls", "Control Rig Controls"); }
	virtual FText GetToolTipText() const override { return LOCTEXT("SequencerTrackFilter_ControlRigControlsToolTip", "Show Only Control Rig Controls."); }
	virtual FSlateIcon GetIcon() const { return FSlateIconFinder::FindIconForClass(UControlRig::StaticClass()); }


	virtual bool PassesFilterWithDisplayName(FTrackFilterType InItem, const FText& InText) const
	{

		const UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(InItem);
		return (Track != nullptr);
	}

	virtual bool PassesFilter(FTrackFilterType InItem) const override
	{
		return true;
	}
};

class FSequencerTrackFilter_ControlRigSelectedControls : public FSequencerTrackFilter
{
	virtual FString GetName() const override { return TEXT("ControlRigControlsSelectedFilter"); }
	virtual FText GetDisplayName() const override { return LOCTEXT("SequenceTrackFilter_ControlRigSelectedControls", "Selected Control Rig Controls"); }
	virtual FText GetToolTipText() const override { return LOCTEXT("SequencerTrackFilter_ControlRigSelectedControls", "Show Only Selected Control Rig Controls."); }
	virtual FSlateIcon GetIcon() const { return FSlateIconFinder::FindIconForClass(UControlRig::StaticClass()); }

	virtual bool PassesFilter(FTrackFilterType InItem) const override
	{
		return true;
	}

	virtual bool PassesFilterWithDisplayName(FTrackFilterType InItem, const FText& InText) const
	{
		const UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(InItem);
		if (Track)
		{
			UControlRig *ControlRig = Track->GetControlRig();
			if (ControlRig)
			{
				FName Name(*InText.ToString());
				TArray<FName> ControlNames = ControlRig->CurrentControlSelection();
				for (const FName& ControlName : ControlNames)
				{
					if (Name == ControlName)
					{
						return true;
					}
				}
			}

		}
		return false;
	}

};

/*

	Bool,
	Float,
	Vector2D,
	Position,
	Scale,
	Rotator,
	Transform
*/

//////////////////////////////////////////////////////////////////////////
//

void UControlRigTrackFilter::AddTrackFilterExtensions(TArray< TSharedRef<class FSequencerTrackFilter> >& InOutFilterList) const
{
	InOutFilterList.Add(MakeShared<FSequencerTrackFilter_ControlRigControls>());
	InOutFilterList.Add(MakeShared<FSequencerTrackFilter_ControlRigSelectedControls>());

}

#undef LOCTEXT_NAMESPACE
