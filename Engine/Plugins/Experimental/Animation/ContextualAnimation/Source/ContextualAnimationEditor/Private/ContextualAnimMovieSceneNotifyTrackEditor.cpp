// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimMovieSceneNotifyTrackEditor.h"
#include "ContextualAnimMovieSceneNotifyTrack.h"
#include "ContextualAnimMovieSceneNotifySection.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimViewModel.h"
#include "ContextualAnimMovieSceneSequence.h"
#include "SequencerUtilities.h"
#include "SequencerSectionPainter.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimMontage.h"
#include "AnimNotifyState_IKWindow.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "PropertyCustomizationHelpers.h"
#include "PersonaUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SButton.h"
#include "ContextualAnimEditorTypes.h"

#define LOCTEXT_NAMESPACE "FContextualAnimMovieSceneNotifyTrackEditor"

// FContextualAnimMovieSceneNotifyTrackEditor
////////////////////////////////////////////////////////////////////////////////////////////////

FContextualAnimMovieSceneNotifyTrackEditor::FContextualAnimMovieSceneNotifyTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
	, NewIKTargetParams(nullptr)
{ 
}

void FContextualAnimMovieSceneNotifyTrackEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (NewIKTargetParams)
	{
		Collector.AddReferencedObject(NewIKTargetParams);
	}
}

UContextualAnimMovieSceneSequence& FContextualAnimMovieSceneNotifyTrackEditor::GetMovieSceneSequence() const
{
	UContextualAnimMovieSceneSequence* Sequence = GetFocusedMovieScene()->GetTypedOuter<UContextualAnimMovieSceneSequence>();
	check(Sequence);
	return *Sequence;
}

TSharedRef<ISequencerTrackEditor> FContextualAnimMovieSceneNotifyTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FContextualAnimMovieSceneNotifyTrackEditor(InSequencer));
}

TSharedRef<ISequencerSection> FContextualAnimMovieSceneNotifyTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	checkf(SectionObject.GetClass()->IsChildOf<UContextualAnimMovieSceneNotifySection>(), TEXT("Unsupported section."));
	return MakeShareable(new FContextualAnimNotifySection(SectionObject));
}

void FContextualAnimMovieSceneNotifyTrackEditor::FillNewNotifyStateMenu(FMenuBuilder& MenuBuilder, bool bIsReplaceWithMenu, UContextualAnimMovieSceneNotifyTrack* Track, int32 RowIndex)
{
	// MenuBuilder always has a search widget added to it by default, hence if larger then 1 then something else has been added to it
	if (MenuBuilder.GetMultiBox()->GetBlocks().Num() > 1)
	{
		MenuBuilder.AddMenuSeparator();
	}

	FOnClassPicked OnPicked(FOnClassPicked::CreateLambda([this, bIsReplaceWithMenu, Track, RowIndex](UClass* InClass)
		{
			//FSlateApplication::Get().DismissAllMenus();

			if (bIsReplaceWithMenu)
			{
				//@TODO: Implement
			}
			else
			{
				CreateNewSection(Track, RowIndex, InClass);
			}
		}
	));

	TSharedRef<SWidget> AnimNotifyStateClassPicker = PersonaUtils::MakeAnimNotifyStatePicker(&Track->GetAnimation(), OnPicked);
	MenuBuilder.AddWidget(AnimNotifyStateClassPicker, FText(), true, false);
}

void FContextualAnimMovieSceneNotifyTrackEditor::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	// Menu that appears when clicking on the Add Track button next to the Search Tracks bar
}

TSharedPtr<SWidget> FContextualAnimMovieSceneNotifyTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	// [+Section] button on the track

	check(Track);

	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	TWeakObjectPtr<UContextualAnimMovieSceneNotifyTrack> WeakTrack = Cast<UContextualAnimMovieSceneNotifyTrack>(Track);
	const int32 RowIndex = Params.TrackInsertRowIndex;
	auto SubMenuCallback = [this, WeakTrack, RowIndex]
	{
		FMenuBuilder MenuBuilder(true, nullptr);

		UContextualAnimMovieSceneNotifyTrack* TrackPtr = WeakTrack.Get();
		if (TrackPtr)
		{
			//@TODO: Add shortcut for Motion Warping window too

			MenuBuilder.AddSubMenu(
				LOCTEXT("AddIKWindow", "Add IK Window"),
				LOCTEXT("AddIKWindowTooltip", "Adds new IK Window"),
				FNewMenuDelegate::CreateRaw(this, &FContextualAnimMovieSceneNotifyTrackEditor::BuildNewIKTargetSubMenu, TrackPtr, RowIndex));


			MenuBuilder.AddSubMenu(
				LOCTEXT("AddNotifyState", "Add Notify State"),
				LOCTEXT("AddNotifyStateToolTip", "Adds new AnimNotifyState"),
				FNewMenuDelegate::CreateRaw(this, &FContextualAnimMovieSceneNotifyTrackEditor::FillNewNotifyStateMenu, false, TrackPtr, RowIndex));
		}
		else
		{
			MenuBuilder.AddWidget(SNew(STextBlock).Text(LOCTEXT("InvalidTrack", "Track is no longer valid")), FText(), true);
		}

		return MenuBuilder.MakeWidget();
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			FSequencerUtilities::MakeAddButton(LOCTEXT("AddSection", "Section"), FOnGetContent::CreateLambda(SubMenuCallback), Params.NodeIsHovered, GetSequencer())
		];
}

void FContextualAnimMovieSceneNotifyTrackEditor::BuildNewIKTargetSubMenu(FMenuBuilder& MenuBuilder, UContextualAnimMovieSceneNotifyTrack* Track, int32 RowIndex)
{
	// Create new IK Target
	{
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("CreateNewIKTarget", "Create new IK Target"));

		MenuBuilder.AddSubMenu(
			LOCTEXT("NewIKTarget", "New IK Target"),
			LOCTEXT("NewIKTargetTooltip", "Creates a new IK Target and adds an IK window for it"),
			FNewMenuDelegate::CreateRaw(this, &FContextualAnimMovieSceneNotifyTrackEditor::BuildNewIKTargetWidget, Track, RowIndex),
			false,
			FSlateIcon()
		);

		MenuBuilder.EndSection();
	}

	// List of IK Targets already created
	{
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("AvailableIKTargets", "Available IK Targets"));

		const UContextualAnimSceneAsset* SceneAsset = GetMovieSceneSequence().GetViewModel().GetSceneAsset();
		const FContextualAnimTrack* AnimTrack = SceneAsset->FindAnimTrackByAnimation(&Track->GetAnimation());
		check(AnimTrack);

		const FContextualAnimIKTargetDefContainer& IKTargets = SceneAsset->GetIKTargetDefsForRoleInSection(AnimTrack->SectionIdx, AnimTrack->Role);
		for (const FContextualAnimIKTargetDefinition& IKTargetDef : IKTargets.IKTargetDefs)
		{
			const FName GoalName = IKTargetDef.GoalName;
			MenuBuilder.AddMenuEntry(
 			FText::FromName(GoalName),
			FText::GetEmpty(),
 			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Track, RowIndex, GoalName]() {
					CreateNewIKSection(Track, RowIndex, GoalName);
			})));
		}

		MenuBuilder.EndSection();
	}
}

UContextualAnimMovieSceneNotifySection* FContextualAnimMovieSceneNotifyTrackEditor::CreateNewIKSection(UContextualAnimMovieSceneNotifyTrack* Track, int32 RowIndex, const FName& GoalName)
{
	UContextualAnimMovieSceneNotifySection* NewSection = CreateNewSection(Track, RowIndex, UAnimNotifyState_IKWindow::StaticClass());
	check(NewSection);

	UAnimNotifyState_IKWindow* AnimNotify = Cast<UAnimNotifyState_IKWindow>(NewSection->GetAnimNotifyState());
	check(AnimNotify);
 
	AnimNotify->GoalName = GoalName;

	return NewSection;
}

void FContextualAnimMovieSceneNotifyTrackEditor::BuildNewIKTargetWidget(FMenuBuilder& MenuBuilder, UContextualAnimMovieSceneNotifyTrack* Track, int32 RowIndex)
{
	if(NewIKTargetParams == nullptr)
	{
		NewIKTargetParams = NewObject<UContextualAnimNewIKTargetParams>(UContextualAnimNewIKTargetParams::StaticClass());
	}
	
	check(NewIKTargetParams);

	NewIKTargetParams->Reset(*GetMovieSceneSequence().GetViewModel().GetSceneAsset(), Track->GetAnimation());

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.bAllowSearch = false;
	Args.bAllowFavoriteSystem = false;

	TSharedPtr<IDetailsView> Widget = PropertyModule.CreateDetailView(Args);
	Widget->SetObject(NewIKTargetParams);

	MenuBuilder.AddWidget(
		SNew(SBox)
		.MinDesiredWidth(500.0f)
		.MaxDesiredWidth(500.f)
		.MaxDesiredHeight(400.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			[
				Widget.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			[
				SNew(SButton)
				.ContentPadding(3)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.IsEnabled_Lambda([this](){ return NewIKTargetParams->HasValidData(); })
				.OnClicked_Lambda([this, Track, RowIndex]()
				{
					GetMovieSceneSequence().GetViewModel().AddNewIKTarget(*NewIKTargetParams);

					// Create IK section for the newly created target
	 				CreateNewIKSection(Track, RowIndex, NewIKTargetParams->GoalName);

					FSlateApplication::Get().DismissAllMenus();

					return FReply::Handled();
				})
			.Text(LOCTEXT("OK", "OK"))
			]
		],
		FText(), true, false);
}

void FContextualAnimMovieSceneNotifyTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{	
	// Builds menu that appears when clicking on the +Track button on an Object Track

	UMovieSceneSequence* MovieSequence = GetSequencer()->GetFocusedMovieSceneSequence();
	if(MovieSequence && MovieSequence->GetMovieScene()->FindPossessable(ObjectBindings[0]))
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NotifyTrack", "Notify Track"),
			LOCTEXT("NotifyTrackTooltip", "Adds a notify track"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FContextualAnimMovieSceneNotifyTrackEditor::AddNewNotifyTrack, ObjectBindings)
			)
		);
	}
}

void FContextualAnimMovieSceneNotifyTrackEditor::AddNewNotifyTrack(TArray<FGuid> ObjectBindings)
{
	// Copied from AnimTimelineTrack_Notifies.cpp/FAnimTimelineTrack_Notifies::GetNewTrackName()
	auto GetNewTrackName = [](UAnimSequenceBase* InAnimSequenceBase) -> FName
	{
		TArray<FName> TrackNames;
		TrackNames.Reserve(50);

		for (const FAnimNotifyTrack& Track : InAnimSequenceBase->AnimNotifyTracks)
		{
			TrackNames.Add(Track.TrackName);
		}

		FName NameToTest;
		int32 TrackIndex = 1;

		do
		{
			NameToTest = *FString::FromInt(TrackIndex++);
		} while (TrackNames.Contains(NameToTest));

		return NameToTest;
	};

	// @TODO: Commented out for now until we add the new behavior where the user needs to double-click on the animation to edit the notifies
	/*for (const FGuid& ObjectBinding : ObjectBindings)
	{
 		UAnimSequenceBase* Animation = GetMovieSceneSequence().GetViewModel().FindAnimationByGuid(ObjectBinding);
 		check(Animation);

		// Copied from AnimTimelineTrack_Notifies.cpp/FAnimTimelineTrack_Notifies::AddTrack()

		FAnimNotifyTrack NewNotifyTrack;
		NewNotifyTrack.TrackName = GetNewTrackName(Animation);
		NewNotifyTrack.TrackColor = FLinearColor::White;

		Animation->AnimNotifyTracks.Add(NewNotifyTrack);

		GetMovieSceneSequence().GetViewModel().AnimationModified(*Animation);

		// Create and Initialize MovieSceneTrack
		UContextualAnimMovieSceneNotifyTrack* MovieSceneTrack = GetMovieSceneSequence().GetMovieScene()->AddTrack<UContextualAnimMovieSceneNotifyTrack>(ObjectBinding);
		check(MovieSceneTrack);

		MovieSceneTrack->Initialize(*Animation, NewNotifyTrack);
	}

	GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);*/
}

bool FContextualAnimMovieSceneNotifyTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return (Type == UContextualAnimMovieSceneNotifyTrack::StaticClass());
}

bool FContextualAnimMovieSceneNotifyTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	ETrackSupport TrackSupported = InSequence ? InSequence->IsTrackSupported(UContextualAnimMovieSceneNotifyTrack::StaticClass()) : ETrackSupport::NotSupported;
	return TrackSupported == ETrackSupport::Supported;
}

UContextualAnimMovieSceneNotifySection* FContextualAnimMovieSceneNotifyTrackEditor::CreateNewSection(UContextualAnimMovieSceneNotifyTrack* Track, int32 RowIndex, UClass* NotifyClass)
{
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (SequencerPtr.IsValid())
	{
		UAnimSequenceBase& Animation = Track->GetAnimation();

		const FName TrackName = FName(Track->GetDisplayName().ToString());
		const int32 TrackIndex = Animation.AnimNotifyTracks.IndexOfByPredicate([TrackName](const FAnimNotifyTrack& AnimNotifyTrack) { return AnimNotifyTrack.TrackName == TrackName; });
		check(TrackIndex != INDEX_NONE);

		// Add new AnimNotifyEvent to the animation
		FAnimNotifyEvent& NewEvent = Animation.Notifies.AddDefaulted_GetRef();
		{
			NewEvent.Guid = FGuid::NewGuid();

			const float StartTime = 0.f;
			NewEvent.Link(&Animation, StartTime);
			NewEvent.TriggerTimeOffset = GetTriggerTimeOffsetForType(Animation.CalculateOffsetForNotify(StartTime));
			NewEvent.TrackIndex = TrackIndex;

			if (NotifyClass)
			{
				UObject* AnimNotifyClass = NewObject<UObject>(&Animation, NotifyClass, NAME_None, RF_Transactional);
				NewEvent.NotifyStateClass = Cast<UAnimNotifyState>(AnimNotifyClass);
				NewEvent.Notify = Cast<UAnimNotify>(AnimNotifyClass);

				if (NewEvent.NotifyStateClass)
				{
					// Set default duration to 1 frame for AnimNotifyState.
					NewEvent.SetDuration(1 / 30.f);
					NewEvent.EndLink.Link(&Animation, NewEvent.EndLink.GetTime());
					NewEvent.TriggerWeightThreshold = NewEvent.NotifyStateClass->GetDefaultTriggerWeightThreshold();
					NewEvent.NotifyName = FName(*NewEvent.NotifyStateClass->GetNotifyName());
				}
				else if (NewEvent.Notify)
				{
					NewEvent.TriggerWeightThreshold = NewEvent.Notify->GetDefaultTriggerWeightThreshold();
					NewEvent.NotifyName = FName(*NewEvent.Notify->GetNotifyName());
				}
			}
			else
			{
				NewEvent.Notify = nullptr;
				NewEvent.NotifyStateClass = nullptr;
			}
		}

		// Create new movie scene section
		UContextualAnimMovieSceneNotifySection* NewSection = CastChecked<UContextualAnimMovieSceneNotifySection>(Track->CreateNewSection());

		NewSection->SetRowIndex(RowIndex);

		// Set range and cache guid
		NewSection->Initialize(NewEvent);
	
		GetMovieSceneSequence().GetViewModel().AnimationModified(Animation);

		// Add section to the track
		Track->AddSection(*NewSection);
		Track->UpdateEasing();
		Track->Modify();

		// Select new section
		SequencerPtr->EmptySelection();
		SequencerPtr->SelectSection(NewSection);
		SequencerPtr->ThrobSectionSelection();

		SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);

		return NewSection;
	}

	return nullptr;
}

// FContextualAnimNotifySection
////////////////////////////////////////////////////////////////////////////////////////////////

FContextualAnimNotifySection::FContextualAnimNotifySection(UMovieSceneSection& InSection)
	: Section(*CastChecked<UContextualAnimMovieSceneNotifySection>(&InSection))
{
}

UMovieSceneSection* FContextualAnimNotifySection::GetSectionObject()
{
	return &Section;
}

int32 FContextualAnimNotifySection::OnPaintSection(FSequencerSectionPainter& InPainter) const
{
	const UAnimNotifyState* AnimNotifyState = Section.GetAnimNotifyState();
	if (AnimNotifyState)
	{
		//@TODO: Use AnimNotifyState->NotifyColor
		return InPainter.PaintSectionBackground();
	}

	return InPainter.PaintSectionBackground(FLinearColor::Red);
}

FText FContextualAnimNotifySection::GetSectionTitle() const
{
	const UAnimNotifyState* AnimNotifyState = Section.GetAnimNotifyState();
	if (AnimNotifyState)
	{
		return FText::FromString(AnimNotifyState->GetNotifyName());
	}

	return LOCTEXT("InvalidAnimNotifyState", "Invalid AnimNotifyState");
}

#undef LOCTEXT_NAMESPACE