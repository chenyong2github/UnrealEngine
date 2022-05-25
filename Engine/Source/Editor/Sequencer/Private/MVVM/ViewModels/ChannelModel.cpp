// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVM/ViewModels/ChannelModel.h"
#include "MVVM/ViewModels/SectionModel.h"
#include "MVVM/ViewModels/SequenceModel.h"
#include "MVVM/ViewModels/ViewModelIterators.h"
#include "MVVM/ViewModels/SequencerModelUtils.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "MVVM/Views/SOutlinerItemViewBase.h"
#include "MVVM/Views/SSequencerKeyNavigationButtons.h"
#include "IKeyArea.h"
#include "ISequencerSection.h"
#include "ISequencerChannelInterface.h"
#include "SequencerNodeTree.h"
#include "SKeyAreaEditorSwitcher.h"
#include "Channels/MovieSceneChannel.h"
#include "CurveModel.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SequencerChannelModel"

namespace UE
{
namespace Sequencer
{

FChannelModel::FChannelModel(FName InChannelName, TWeakPtr<ISequencerSection> InSection, FMovieSceneChannelHandle InChannel)
	: KeyArea(MakeShared<IKeyArea>(InSection, InChannel))
	, ChannelName(InChannelName)
{
}

FChannelModel::~FChannelModel()
{
}

bool FChannelModel::IsAnimated() const
{
	if (KeyArea)
	{
		FMovieSceneChannel* Channel = KeyArea->ResolveChannel();
		return Channel && Channel->GetNumKeys() > 0;
	}
	return false;
}

void FChannelModel::Initialize(TWeakPtr<ISequencerSection> InSection, FMovieSceneChannelHandle InChannel)
{
	if (!KeyArea)
	{
		KeyArea = MakeShared<IKeyArea>(InSection, InChannel);
	}
	else
	{
		KeyArea->Reinitialize(InSection, InChannel);
	}
}

FMovieSceneChannel* FChannelModel::GetChannel() const
{
	check(KeyArea);
	return KeyArea->ResolveChannel();
}

UMovieSceneSection* FChannelModel::GetSection() const
{
	return KeyArea->GetOwningSection();
}

FOutlinerSizing FChannelModel::GetDesiredSizing() const
{
	return FOutlinerSizing(15.f);
}

FChannelGroupModel::FChannelGroupModel(FName InChannelName, const FText& InDisplayText)
	: ChannelName(InChannelName)
	, DisplayText(InDisplayText)
{
	SetIdentifier(InChannelName);
}

FChannelGroupModel::~FChannelGroupModel()
{
}

bool FChannelGroupModel::IsAnimated() const
{
	for (TWeakViewModelPtr<FChannelModel> WeakChannel : Channels)
	{
		if (TSharedPtr<FChannelModel> Channel = WeakChannel.Pin())
		{
			if (Channel->IsAnimated())
			{
				return true;
			}
		}
	}
	return false;
}

void FChannelGroupModel::AddChannel(TWeakViewModelPtr<FChannelModel> InChannel)
{
	if (!Channels.Contains(InChannel))
	{
		Channels.Add(InChannel);
	}
}

TArrayView<const TWeakViewModelPtr<FChannelModel>> FChannelGroupModel::GetChannels() const
{
	return Channels;
}

TSharedPtr<IKeyArea> FChannelGroupModel::GetKeyArea(TSharedPtr<FSectionModel> InOwnerSection) const
{
	return GetKeyArea(InOwnerSection->GetSection());
}

TSharedPtr<IKeyArea> FChannelGroupModel::GetKeyArea(const UMovieSceneSection* InOwnerSection) const
{
	TSharedPtr<FChannelModel> Channel = GetChannel(InOwnerSection);
	return Channel ? Channel->GetKeyArea() : nullptr;
}

TSharedPtr<FChannelModel> FChannelGroupModel::GetChannel(TSharedPtr<FSectionModel> InOwnerSection) const
{
	return GetChannel(InOwnerSection->GetSection());
}

TSharedPtr<FChannelModel> FChannelGroupModel::GetChannel(const UMovieSceneSection* InOwnerSection) const
{
	for (TWeakViewModelPtr<FChannelModel> WeakChannel : Channels)
	{
		if (TSharedPtr<FChannelModel> Channel = WeakChannel.Pin())
		{
			if (Channel->GetSection() == InOwnerSection)
			{
				return Channel;
			}
		}
	}
	return nullptr;
}

TArray<TSharedRef<IKeyArea>> FChannelGroupModel::GetAllKeyAreas() const
{
	TArray<TSharedRef<IKeyArea>> KeyAreas;
	KeyAreas.Reserve(Channels.Num());
	for (TWeakViewModelPtr<FChannelModel> WeakChannel : Channels)
	{
		if (TSharedPtr<FChannelModel> Channel = WeakChannel.Pin())
		{
			if (Channel->GetKeyArea())
			{
				KeyAreas.Add(Channel->GetKeyArea().ToSharedRef());
			}
		}
	}
	return KeyAreas;
}

FOutlinerSizing FChannelGroupModel::RecomputeSizing()
{
	FOutlinerSizing MaxSizing;

	for (TWeakViewModelPtr<FChannelModel> WeakChannel : Channels)
	{
		if (TSharedPtr<FChannelModel> Channel = WeakChannel.Pin())
		{
			FOutlinerSizing Desired = Channel->GetDesiredSizing();

			MaxSizing.Height = FMath::Max(MaxSizing.Height, Desired.Height);
			MaxSizing.PaddingTop = FMath::Max(MaxSizing.PaddingTop, Desired.PaddingTop);
			MaxSizing.PaddingBottom = FMath::Max(MaxSizing.PaddingBottom, Desired.PaddingBottom);
		}
	}

	ComputedSizing = MaxSizing;

	for (TWeakViewModelPtr<FChannelModel> WeakChannel : Channels)
	{
		if (TSharedPtr<FChannelModel> Channel = WeakChannel.Pin())
		{
			Channel->SetComputedSizing(MaxSizing);
		}
	}

	return MaxSizing;
}

FOutlinerSizing FChannelGroupModel::GetOutlinerSizing() const
{
	return ComputedSizing;
}

FTrackAreaParameters FChannelGroupModel::GetTrackAreaParameters() const
{
	FTrackAreaParameters Parameters;
	Parameters.LaneType = ETrackAreaLaneType::None;
	return Parameters;
}

FViewModelVariantIterator FChannelGroupModel::GetTrackAreaModelList() const
{
	return &Channels;
}

TSharedRef<SWidget> FChannelGroupModel::CreateOutlinerView(const FCreateOutlinerViewParams& InParams)
{
	TSharedPtr<FSequencerEditorViewModel> EditorViewModel = GetEditor();

	return SNew(SOutlinerItemViewBase, SharedThis(this), InParams.Editor, InParams.TreeViewRow)
	.CustomContent()
	[
		// Even if this key area node doesn't have any key areas right now, it may in the future
		// so we always create the switcher, and just hide it if it is not relevant
		SNew(SHorizontalBox)
		.Visibility(this, &FChannelGroupModel::GetKeyEditorVisibility)

		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SKeyAreaEditorSwitcher, SharedThis(this), EditorViewModel->GetSequencer())
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SSequencerKeyNavigationButtons, SharedThis(this), EditorViewModel->GetSequencer())
		]
	];
}

EVisibility FChannelGroupModel::GetKeyEditorVisibility() const
{
	return GetChannels().Num() == 0 ? EVisibility::Collapsed : EVisibility::Visible;
}

FText FChannelGroupModel::GetLabel() const
{
	return GetDisplayText();
}

FSlateFontInfo FChannelGroupModel::GetLabelFont() const
{
	return IsAnimated()
		? FAppStyle::GetFontStyle("Sequencer.AnimationOutliner.ItalicFont")
		: FOutlinerItemModel::GetLabelFont();
}

bool FChannelGroupModel::HasCurves() const
{
	for (const TSharedRef<IKeyArea>& KeyArea : GetAllKeyAreas())
	{
		const ISequencerChannelInterface* EditorInterface = KeyArea->FindChannelEditorInterface();
		if (EditorInterface && EditorInterface->SupportsCurveEditorModels_Raw(KeyArea->GetChannel()))
		{
			return true;
		}
	}
	return false;
}

void FChannelGroupModel::CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels)
{
	TSharedPtr<FSequenceModel> SequenceModel = FindAncestorOfType<FSequenceModel>();
	if (!SequenceModel)
	{
		return;
	}

	TSharedPtr<ISequencer> Sequencer = SequenceModel->GetSequencer();
	if (!Sequencer)
	{
		return;
	}

	for (TWeakViewModelPtr<FChannelModel> WeakChannel : GetChannels())
	{
		if (TSharedPtr<FChannelModel> Channel = WeakChannel.Pin())
		{
			TUniquePtr<FCurveModel> NewCurve = Channel->GetKeyArea()->CreateCurveEditorModel(Sequencer.ToSharedRef());
			if (NewCurve.IsValid())
			{
				OutCurveModels.Add(MoveTemp(NewCurve));
			}
		}
	}
}

bool FChannelGroupModel::CanDelete(FText* OutErrorMessage) const
{
	return true;
}

void FChannelGroupModel::Delete()
{
	TArray<FName> PathFromTrack;
	TViewModelPtr<ITrackExtension> Track = GetParentTrackNodeAndNamePath(this, PathFromTrack);

	Track->GetTrack()->Modify();

	for (const FViewModelPtr& Channel : GetTrackAreaModelList())
	{
		if (TViewModelPtr<FSectionModel> Section = Channel->FindAncestorOfType<FSectionModel>())
		{
			Section->GetSectionInterface()->RequestDeleteCategory(PathFromTrack);
		}
	}
}

} // namespace Sequencer
} // namespace UE

#undef LOCTEXT_NAMESPACE

