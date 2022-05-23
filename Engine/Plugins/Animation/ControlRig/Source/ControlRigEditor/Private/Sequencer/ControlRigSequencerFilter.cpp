// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigSequencerFilter.h"
#include "ControlRig.h"
#include "ControlRigSkeletalMeshComponent.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Styling/SlateIconFinder.h"
#include "Framework/Commands/Commands.h"
#include "ISequencer.h"

#define LOCTEXT_NAMESPACE "ControlRigSequencerTrackFilters"

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_ControlRigControlsCommands
	: public TCommands<FSequencerTrackFilter_ControlRigControlsCommands>
{
public:

	FSequencerTrackFilter_ControlRigControlsCommands()
		: TCommands<FSequencerTrackFilter_ControlRigControlsCommands>
		(
			"FSequencerTrackFilter_ControlRigControls",
			NSLOCTEXT("Contexts", "FSequencerTrackFilter_ControlRigControls", "FSequencerTrackFilter_ControlRigControls"),
			NAME_None,
			FAppStyle::GetAppStyleSetName() // Icon Style Set
			)
	{ }

	/** Toggle the control rig controls filter */
	TSharedPtr< FUICommandInfo > ToggleControlRigControls;

	/** Initialize commands */
	virtual void RegisterCommands() override
	{
		UI_COMMAND(ToggleControlRigControls, "Control Rig Controls", "Toggle the filter for Control Rig Controls.", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::F9));
	}
};

class FSequencerTrackFilter_ControlRigControls : public FSequencerTrackFilter
{
public:

	FSequencerTrackFilter_ControlRigControls()
	{
		FSequencerTrackFilter_ControlRigControlsCommands::Register();
	}

	~FSequencerTrackFilter_ControlRigControls()
	{
		FSequencerTrackFilter_ControlRigControlsCommands::Unregister();
	}

	virtual FString GetName() const override { return TEXT("ControlRigControlsFilter"); }
	virtual FText GetDisplayName() const override { return LOCTEXT("SequenceTrackFilter_ControlRigControls", "Control Rig Controls"); }
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

	virtual FText GetToolTipText() const override
	{
		if (!FSequencerTrackFilter_ControlRigControlsCommands::IsRegistered())
		{
			FSequencerTrackFilter_ControlRigControlsCommands::Register();
		}

		const FSequencerTrackFilter_ControlRigControlsCommands& Commands = FSequencerTrackFilter_ControlRigControlsCommands::Get();

		const TSharedRef<const FInputChord> FirstActiveChord = Commands.ToggleControlRigControls->GetFirstValidChord();

		FText Tooltip = LOCTEXT("SequencerTrackFilter_ControlRigControlsTip", "Show Only Control Rig Controls.");

		if (FirstActiveChord->IsValidChord())
		{
			return FText::Join(FText::FromString(TEXT(" ")), Tooltip, FirstActiveChord->GetInputText());
		}
		return Tooltip;
	}

	virtual void BindCommands(TSharedRef<FUICommandList> CommandBindings, TWeakPtr<ISequencer> Sequencer) override
	{
		// See comment above
		if (!FSequencerTrackFilter_ControlRigControlsCommands::IsRegistered())
		{
			FSequencerTrackFilter_ControlRigControlsCommands::Register();
		}

		const FSequencerTrackFilter_ControlRigControlsCommands& Commands = FSequencerTrackFilter_ControlRigControlsCommands::Get();

		CommandBindings->MapAction(
			Commands.ToggleControlRigControls,
			FExecuteAction::CreateLambda([this, Sequencer] { Sequencer.Pin()->SetTrackFilterEnabled(GetDisplayName(), !Sequencer.Pin()->IsTrackFilterEnabled(GetDisplayName())); }),
			FCanExecuteAction::CreateLambda([this, Sequencer] { return true; }),
			FIsActionChecked::CreateLambda([this, Sequencer] { return Sequencer.Pin()->IsTrackFilterEnabled(GetDisplayName()); }));
	}
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_ControlRigSelectedControlsCommands
	: public TCommands<FSequencerTrackFilter_ControlRigSelectedControlsCommands>
{
public:

	FSequencerTrackFilter_ControlRigSelectedControlsCommands()
		: TCommands<FSequencerTrackFilter_ControlRigSelectedControlsCommands>
		(
			"FSequencerTrackFilter_ControlRigSelectedControls",
			NSLOCTEXT("Contexts", "FSequencerTrackFilter_ControlRigSelectedControls", "FSequencerTrackFilter_ControlRigSelectedControls"),
			NAME_None,
			FAppStyle::GetAppStyleSetName() // Icon Style Set
			)
	{ }

	/** Toggle the control rig selected controls filter */
	TSharedPtr< FUICommandInfo > ToggleControlRigSelectedControls;

	/** Initialize commands */
	virtual void RegisterCommands() override
	{
		UI_COMMAND(ToggleControlRigSelectedControls, "Control Rig Selected Controls", "Toggle the filter for Control Rig Selected Controls.", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::F10));
	}
};

class FSequencerTrackFilter_ControlRigSelectedControls : public FSequencerTrackFilter
{
public:

	FSequencerTrackFilter_ControlRigSelectedControls()
	{
		FSequencerTrackFilter_ControlRigSelectedControlsCommands::Register();
	}

	~FSequencerTrackFilter_ControlRigSelectedControls()
	{
		FSequencerTrackFilter_ControlRigSelectedControlsCommands::Unregister();
	}

	virtual FString GetName() const override { return TEXT("ControlRigControlsSelectedFilter"); }
	virtual FText GetDisplayName() const override { return LOCTEXT("SequenceTrackFilter_ControlRigSelectedControls", "Selected Control Rig Controls"); }
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

	virtual FText GetToolTipText() const override
	{
		if (!FSequencerTrackFilter_ControlRigSelectedControlsCommands::IsRegistered())
		{
			FSequencerTrackFilter_ControlRigSelectedControlsCommands::Register();
		}

		const FSequencerTrackFilter_ControlRigSelectedControlsCommands& Commands = FSequencerTrackFilter_ControlRigSelectedControlsCommands::Get();

		const TSharedRef<const FInputChord> FirstActiveChord = Commands.ToggleControlRigSelectedControls->GetFirstValidChord();

		FText Tooltip = LOCTEXT("SequencerTrackFilter_ControlRigSelectedControlsTip", "Show Only Selected Control Rig Controls.");

		if (FirstActiveChord->IsValidChord())
		{
			return FText::Join(FText::FromString(TEXT(" ")), Tooltip, FirstActiveChord->GetInputText());
		}
		return Tooltip;
	}

	virtual void BindCommands(TSharedRef<FUICommandList> CommandBindings, TWeakPtr<ISequencer> Sequencer) override
	{
		// See comment above
		if (!FSequencerTrackFilter_ControlRigSelectedControlsCommands::IsRegistered())
		{
			FSequencerTrackFilter_ControlRigSelectedControlsCommands::Register();
		}

		const FSequencerTrackFilter_ControlRigSelectedControlsCommands& Commands = FSequencerTrackFilter_ControlRigSelectedControlsCommands::Get();

		CommandBindings->MapAction(
			Commands.ToggleControlRigSelectedControls,
			FExecuteAction::CreateLambda([this, Sequencer] { Sequencer.Pin()->SetTrackFilterEnabled(GetDisplayName(), !Sequencer.Pin()->IsTrackFilterEnabled(GetDisplayName())); }),
			FCanExecuteAction::CreateLambda([this, Sequencer] { return true; }),
			FIsActionChecked::CreateLambda([this, Sequencer] { return Sequencer.Pin()->IsTrackFilterEnabled(GetDisplayName()); }));
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
