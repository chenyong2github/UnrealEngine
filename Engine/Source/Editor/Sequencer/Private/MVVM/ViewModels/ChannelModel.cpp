// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVM/ViewModels/ChannelModel.h"
#include "MVVM/ViewModels/SectionModel.h"
#include "MVVM/ViewModels/SequenceModel.h"
#include "MVVM/ViewModels/ViewModelIterators.h"
#include "MVVM/ViewModels/SequencerModelUtils.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "MVVM/Views/SOutlinerItemViewBase.h"
#include "MVVM/Views/SSequencerKeyNavigationButtons.h"
#include "MVVM/Views/KeyRenderer.h"
#include "MVVM/Views/SChannelView.h"
#include "IKeyArea.h"
#include "ISequencerSection.h"
#include "ISequencerChannelInterface.h"
#include "SequencerHotspots.h"
#include "SequencerNodeTree.h"
#include "SKeyAreaEditorSwitcher.h"
#include "SequencerSectionPainter.h"
#include "SSequencerSection.h"
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
	if (KeyArea->ShouldShowCurve())
	{
		TViewModelPtr<FSequenceModel> Sequence = FindAncestorOfType<FSequenceModel>();
		if (Sequence)
		{
			return Sequence->GetSequencer()->GetSequencerSettings()->GetKeyAreaHeightWithCurves();
		}
	}
	return FOutlinerSizing(15.f);
}

TSharedPtr<ITrackLaneWidget> FChannelModel::CreateTrackLaneView(const FCreateTrackLaneViewParams& InParams)
{
	return SNew(SChannelView, SharedThis(this), InParams.TimeToPixel, InParams.Editor->GetTrackArea())
		.KeyBarColor(this, &FChannelModel::GetKeyBarColor);
}

FTrackLaneVirtualAlignment FChannelModel::ArrangeVirtualTrackLaneView() const
{
	TSharedPtr<FSectionModel> Section = FindAncestorOfType<FSectionModel>();
	if (Section)
	{
		return FTrackLaneVirtualAlignment::Proportional(Section->GetRange(), 1.f);
	}
	return FTrackLaneVirtualAlignment::Proportional(TRange<FFrameNumber>(), 1.f);
}

bool FChannelModel::UpdateCachedKeys(TSharedPtr<FCachedKeys>& OutCachedKeys) const
{
	struct FSequencerCachedKeys : FCachedKeys
	{
		FSequencerCachedKeys(const FChannelModel* InChannel)
		{
			Update(InChannel);
		}

		bool Update(const FChannelModel* InChannel)
		{
			UMovieSceneSection* Section = InChannel->GetSection();
			if (!Section || !CachedSignature.IsValid() || Section->GetSignature() != CachedSignature)
			{
				CachedSignature = Section ? Section->GetSignature() : FGuid();

				KeyTimes.Reset();
				KeyHandles.Reset();

				TArray<FFrameNumber> KeyFrames;
				InChannel->GetKeyArea()->GetKeyInfo(&KeyHandles, &KeyFrames);

				KeyTimes.SetNumUninitialized(KeyFrames.Num());
				for (int32 Index = 0; Index < KeyFrames.Num(); ++Index)
				{
					KeyTimes[Index] = KeyFrames[Index];
				}
				return true;
			}

			return false;
		}

		/** The guid with which the above array was generated */
		FGuid CachedSignature;
	};

	if (OutCachedKeys)
	{
		return StaticCastSharedPtr<FSequencerCachedKeys>(OutCachedKeys)->Update(this);
	}
	else
	{
		OutCachedKeys = MakeShared<FSequencerCachedKeys>(this);
		return true;
	}

	return false;
}

bool FChannelModel::GetFixedExtents(double& OutFixedMin, double& OutFixedMax) const
{
	TViewModelPtr<FSequenceModel> SequenceModel = FindAncestorOfType<FSequenceModel>();
	if (!SequenceModel)
	{
		return false;
	}

	FString KeyAreaName = KeyArea->GetName().ToString();
	if (SequenceModel->GetSequencer()->GetSequencerSettings()->HasKeyAreaCurveExtents(KeyAreaName))
	{
		SequenceModel->GetSequencer()->GetSequencerSettings()->GetKeyAreaCurveExtents(KeyAreaName, OutFixedMin, OutFixedMax);
		return true;
	}

	return false;
}

int32 FChannelModel::CustomPaint(const FGeometry& KeyGeometry, int32 LayerId) const
{
	return LayerId;
}

void FChannelModel::DrawKeys(TArrayView<const FKeyHandle> InKeyHandles, TArrayView<FKeyDrawParams> OutKeyDrawParams)
{
	KeyArea->DrawKeys(InKeyHandles, OutKeyDrawParams);
}

TUniquePtr<FCurveModel> FChannelModel::CreateCurveModel()
{
	if (TViewModelPtr<FSequenceModel> SequenceModel = FindAncestorOfType<FSequenceModel>())
	{
		return KeyArea->CreateCurveEditorModel(SequenceModel->GetSequencer().ToSharedRef());
	}

	return nullptr;
}

FLinearColor FChannelModel::GetKeyBarColor() const
{
	if (TViewModelPtr<FSequenceModel> SequenceModel = FindAncestorOfType<FSequenceModel>())
	{
		if (SequenceModel->GetSequencer()->GetSequencerSettings()->GetShowChannelColors())
		{
			TOptional<FLinearColor> ChannelColor = KeyArea->GetColor();
			if (ChannelColor)
			{
				return ChannelColor.GetValue();
			}
		}
	}

	TViewModelPtr<ITrackExtension> Track = FindAncestorOfType<ITrackExtension>();
	UMovieSceneTrack* TrackObject = Track ? Track->GetTrack() : nullptr;
	if (TrackObject)
	{
		FLinearColor Tint = FSequencerSectionPainter::BlendColor(TrackObject->GetColorTint()).LinearRGBToHSV();

		// If this is a top level chanel, draw using the fill color
		FViewModelPtr OutlinerItem = GetLinkedOutlinerItem();
		if (OutlinerItem && !OutlinerItem->IsA<FChannelGroupModel>())
		{
			Tint.G *= .5f;
			Tint.B = FMath::Max(.03f, Tint.B*.1f);
		}

		return Tint.HSVToLinearRGB().CopyWithNewOpacity(1.f);
	}
	return FColor(160, 160, 160);
}

FChannelGroupModel::FChannelGroupModel(FName InChannelName, const FText& InDisplayText)
	: ChannelName(InChannelName)
	, DisplayText(InDisplayText)
{
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

FTrackAreaParameters FChannelGroupModel::GetTrackAreaParameters() const
{
	FTrackAreaParameters Parameters;
	Parameters.LaneType = ETrackAreaLaneType::Inline;
	return Parameters;
}

FViewModelVariantIterator FChannelGroupModel::GetTrackAreaModelList() const
{
	return &Channels;
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

FChannelGroupOutlinerModel::FChannelGroupOutlinerModel(FName InChannelName, const FText& InDisplayText)
	: TOutlinerModelMixin<FChannelGroupModel>(InChannelName, InDisplayText)
{
	SetIdentifier(InChannelName);
}

FChannelGroupOutlinerModel::~FChannelGroupOutlinerModel()
{}

FOutlinerSizing FChannelGroupOutlinerModel::RecomputeSizing()
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

FOutlinerSizing FChannelGroupOutlinerModel::GetOutlinerSizing() const
{
	return ComputedSizing;
}

TSharedRef<SWidget> FChannelGroupOutlinerModel::CreateOutlinerView(const FCreateOutlinerViewParams& InParams)
{
	TSharedPtr<FSequencerEditorViewModel> EditorViewModel = GetEditor();

	return SNew(SOutlinerItemViewBase, SharedThis(this), InParams.Editor, InParams.TreeViewRow)
	.CustomContent()
	[
		// Even if this key area node doesn't have any key areas right now, it may in the future
		// so we always create the switcher, and just hide it if it is not relevant
		SNew(SHorizontalBox)
		.Visibility(this, &FChannelGroupOutlinerModel::GetKeyEditorVisibility)

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

EVisibility FChannelGroupOutlinerModel::GetKeyEditorVisibility() const
{
	return GetChannels().Num() == 0 ? EVisibility::Collapsed : EVisibility::Visible;
}

FText FChannelGroupOutlinerModel::GetLabel() const
{
	return GetDisplayText();
}

FSlateFontInfo FChannelGroupOutlinerModel::GetLabelFont() const
{
	return IsAnimated()
		? FAppStyle::GetFontStyle("Sequencer.AnimationOutliner.ItalicFont")
		: FOutlinerItemModelMixin::GetLabelFont();
}

bool FChannelGroupOutlinerModel::HasCurves() const
{
	return FChannelGroupModel::HasCurves();
}

bool FChannelGroupOutlinerModel::CanDelete(FText* OutErrorMessage) const
{
	return true;
}

void FChannelGroupOutlinerModel::Delete()
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

void FChannelGroupOutlinerModel::CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels)
{
	FChannelGroupModel::CreateCurveModels(OutCurveModels);
}

} // namespace Sequencer
} // namespace UE

#undef LOCTEXT_NAMESPACE

