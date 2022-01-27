// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerUtilities.h"
#include "Misc/Paths.h"
#include "Layout/Margin.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"
#include "EditorStyleSet.h"
#include "SequencerTrackNode.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "MovieSceneTimeHelpers.h"
#include "MovieSceneFolder.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISequencerTrackEditor.h"
#include "ISequencer.h"
#include "Sequencer.h"
#include "SequencerNodeTree.h"
#include "DisplayNodes/SequencerObjectBindingNode.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "AssetRegistryModule.h"
#include "FileHelpers.h"
#include "LevelSequence.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

PRAGMA_DISABLE_OPTIMIZATION

#define LOCTEXT_NAMESPACE "FSequencerUtilities"

static EVisibility GetRolloverVisibility(TAttribute<bool> HoverState, TWeakPtr<SComboButton> WeakComboButton)
{
	TSharedPtr<SComboButton> ComboButton = WeakComboButton.Pin();
	if (HoverState.Get() || ComboButton->IsOpen())
	{
		return EVisibility::SelfHitTestInvisible;
	}
	else
	{
		return EVisibility::Collapsed;
	}
}

static EVisibility GetRolloverVisibility(TAttribute<bool> HoverState, TWeakPtr<SButton> WeakButton)
{
	TSharedPtr<SButton> Button = WeakButton.Pin();
	if (HoverState.Get())
	{
		return EVisibility::SelfHitTestInvisible;
	}
	else
	{
		return EVisibility::Collapsed;
	}
}

TSharedRef<SWidget> FSequencerUtilities::MakeAddButton(FText HoverText, FOnGetContent MenuContent, const TAttribute<bool>& HoverState, TWeakPtr<ISequencer> InSequencer)
{
	FSlateFontInfo SmallLayoutFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	TSharedRef<STextBlock> ComboButtonText = SNew(STextBlock)
		.Text(HoverText)
		.Font(SmallLayoutFont)
		.ColorAndOpacity( FSlateColor::UseForeground() );

	TSharedRef<SComboButton> ComboButton =

		SNew(SComboButton)
		.HasDownArrow(false)
		.IsFocusable(true)
		.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
		.ForegroundColor( FSlateColor::UseForeground() )
		.IsEnabled_Lambda([=]() { return InSequencer.IsValid() ? !InSequencer.Pin()->IsReadOnly() : false; })
		.OnGetMenuContent(MenuContent)
		.ContentPadding(FMargin(5, 2))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ButtonContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0,0,2,0))
			[
				SNew(SImage)
				.ColorAndOpacity( FSlateColor::UseForeground() )
				.Image(FEditorStyle::GetBrush("Plus"))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				ComboButtonText
			]
		];

	TAttribute<EVisibility> Visibility = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(GetRolloverVisibility, HoverState, TWeakPtr<SComboButton>(ComboButton)));
	ComboButtonText->SetVisibility(Visibility);

	return ComboButton;
}

TSharedRef<SWidget> FSequencerUtilities::MakeAddButton(FText HoverText, FOnClicked OnClicked, const TAttribute<bool>& HoverState, TWeakPtr<ISequencer> InSequencer)
{
	FSlateFontInfo SmallLayoutFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	TSharedRef<STextBlock> ButtonText = SNew(STextBlock)
		.Text(HoverText)
		.Font(SmallLayoutFont)
		.ColorAndOpacity(FSlateColor::UseForeground());

	TSharedRef<SButton> Button =

		SNew(SButton)
		.IsFocusable(true)
		.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
		.ForegroundColor(FSlateColor::UseForeground())
		.IsEnabled_Lambda([=]() { return InSequencer.IsValid() ? !InSequencer.Pin()->IsReadOnly() : false; })
		.OnClicked(OnClicked)
		.ContentPadding(FMargin(5, 2))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Content()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0, 0, 2, 0))
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FEditorStyle::GetBrush("Plus"))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				ButtonText
			]
		];

	TAttribute<EVisibility> Visibility = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(GetRolloverVisibility, HoverState, TWeakPtr<SButton>(Button)));
	ButtonText->SetVisibility(Visibility);

	return Button;
}

void FSequencerUtilities::CreateNewSection(UMovieSceneTrack* InTrack, TWeakPtr<ISequencer> InSequencer, int32 InRowIndex, EMovieSceneBlendType InBlendType)
{
	TSharedPtr<ISequencer> Sequencer = InSequencer.Pin();
	if (!Sequencer.IsValid())
	{
		return;
	}

	FQualifiedFrameTime CurrentTime = Sequencer->GetLocalTime();
	FFrameNumber PlaybackEnd = UE::MovieScene::DiscreteExclusiveUpper(Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->GetPlaybackRange());

	FScopedTransaction Transaction(LOCTEXT("AddSectionTransactionText", "Add Section"));
	if (UMovieSceneSection* NewSection = InTrack->CreateNewSection())
	{
		int32 OverlapPriority = 0;
		for (UMovieSceneSection* Section : InTrack->GetAllSections())
		{
			OverlapPriority = FMath::Max(Section->GetOverlapPriority() + 1, OverlapPriority);

			// Move existing sections on the same row or beyond so that they don't overlap with the new section
			if (Section != NewSection && Section->GetRowIndex() >= InRowIndex)
			{
				Section->SetRowIndex(Section->GetRowIndex() + 1);
			}
		}

		InTrack->Modify();

		NewSection->SetRange(TRange<FFrameNumber>(CurrentTime.Time.FrameNumber, PlaybackEnd));
		NewSection->SetOverlapPriority(OverlapPriority);
		NewSection->SetRowIndex(InRowIndex);
		NewSection->SetBlendType(InBlendType);

		InTrack->AddSection(*NewSection);
		InTrack->UpdateEasing();

		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
		Sequencer->EmptySelection();
		Sequencer->SelectSection(NewSection);
		Sequencer->ThrobSectionSelection();
	}
	else
	{
		Transaction.Cancel();
	}
}

void FSequencerUtilities::PopulateMenu_CreateNewSection(FMenuBuilder& MenuBuilder, int32 RowIndex, UMovieSceneTrack* Track, TWeakPtr<ISequencer> InSequencer)
{
	if (!Track)
	{
		return;
	}
	
	auto CreateNewSection = [Track, InSequencer, RowIndex](EMovieSceneBlendType BlendType)
	{
		TSharedPtr<ISequencer> Sequencer = InSequencer.Pin();
		if (!Sequencer.IsValid())
		{
			return;
		}

		FQualifiedFrameTime CurrentTime = Sequencer->GetLocalTime();
		FFrameNumber PlaybackEnd = UE::MovieScene::DiscreteExclusiveUpper(Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->GetPlaybackRange());

		FScopedTransaction Transaction(LOCTEXT("AddSectionTransactionText", "Add Section"));
		if (UMovieSceneSection* NewSection = Track->CreateNewSection())
		{
			int32 OverlapPriority = 0;
			for (UMovieSceneSection* Section : Track->GetAllSections())
			{
				OverlapPriority = FMath::Max(Section->GetOverlapPriority() + 1, OverlapPriority);

				// Move existing sections on the same row or beyond so that they don't overlap with the new section
				if (Section != NewSection && Section->GetRowIndex() >= RowIndex)
				{
					Section->SetRowIndex(Section->GetRowIndex() + 1);
				}
			}

			Track->Modify();

			NewSection->SetRange(TRange<FFrameNumber>(CurrentTime.Time.FrameNumber, PlaybackEnd));
			NewSection->SetOverlapPriority(OverlapPriority);
			NewSection->SetRowIndex(RowIndex);
			NewSection->SetBlendType(BlendType);

			Track->AddSection(*NewSection);
			Track->UpdateEasing();

			Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
			Sequencer->EmptySelection();
			Sequencer->SelectSection(NewSection);
			Sequencer->ThrobSectionSelection();
		}
		else
		{
			Transaction.Cancel();
		}
	};

	FText NameOverride		= Track->GetSupportedBlendTypes().Num() == 1 ? LOCTEXT("AddSectionText", "Add New Section") : FText();
	FText TooltipOverride	= Track->GetSupportedBlendTypes().Num() == 1 ? LOCTEXT("AddSectionToolTip", "Adds a new section at the current time") : FText();

	const UEnum* MovieSceneBlendType = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMovieSceneBlendType"));
	for (EMovieSceneBlendType BlendType : Track->GetSupportedBlendTypes())
	{
		FText DisplayName = MovieSceneBlendType->GetDisplayNameTextByValue((int64)BlendType);
		FName EnumValueName = MovieSceneBlendType->GetNameByValue((int64)BlendType);
		MenuBuilder.AddMenuEntry(
			NameOverride.IsEmpty() ? DisplayName : NameOverride,
			TooltipOverride.IsEmpty() ? FText::Format(LOCTEXT("AddSectionFormatToolTip", "Adds a new {0} section at the current time"), DisplayName) : TooltipOverride,
			FSlateIcon("EditorStyle", EnumValueName),
			FUIAction(FExecuteAction::CreateLambda(CreateNewSection, BlendType))
		);
	}
}

void FSequencerUtilities::PopulateMenu_SetBlendType(FMenuBuilder& MenuBuilder, UMovieSceneSection* Section, TWeakPtr<ISequencer> InSequencer)
{
	PopulateMenu_SetBlendType(MenuBuilder, TArray<TWeakObjectPtr<UMovieSceneSection>>({ Section }), InSequencer);
}

void FSequencerUtilities::PopulateMenu_SetBlendType(FMenuBuilder& MenuBuilder, const TArray<TWeakObjectPtr<UMovieSceneSection>>& InSections, TWeakPtr<ISequencer> InSequencer)
{
	auto Execute = [InSections, InSequencer](EMovieSceneBlendType BlendType)
	{
		FScopedTransaction Transaction(LOCTEXT("SetBlendType", "Set Blend Type"));
		for (TWeakObjectPtr<UMovieSceneSection> WeakSection : InSections)
		{
			if (UMovieSceneSection* Section = WeakSection.Get())
			{
				Section->Modify();
				Section->SetBlendType(BlendType);
			}
		}
			
		TSharedPtr<FSequencer> Sequencer = StaticCastSharedPtr<FSequencer>(InSequencer.Pin());
		if (Sequencer.IsValid())
		{
			// If the blend type is changed to additive or relative, restore the state of the objects boud to this section before evaluating again. 
			// This allows the additive or relative to evaluate based on the initial values of the object, rather than the current animated values.
			if (BlendType == EMovieSceneBlendType::Additive || BlendType == EMovieSceneBlendType::Relative)
			{
				TSet<UObject*> ObjectsToRestore;
				TSharedRef<FSequencerNodeTree> SequencerNodeTree = Sequencer->GetNodeTree();
				for (TWeakObjectPtr<UMovieSceneSection> WeakSection : InSections)
				{
					if (UMovieSceneSection* Section = WeakSection.Get())
					{
						TOptional<FSectionHandle> SectionHandle = SequencerNodeTree->GetSectionHandle(Section);
						if (!SectionHandle)
						{
							continue;
						}

						TSharedPtr<FSequencerObjectBindingNode> ParentObjectBindingNode = SectionHandle->GetTrackNode()->FindParentObjectBindingNode();
						if (!ParentObjectBindingNode.IsValid())
						{
							continue;
						}

						for (TWeakObjectPtr<> BoundObject : Sequencer->FindObjectsInCurrentSequence(ParentObjectBindingNode->GetObjectBinding()))
						{
							if (AActor* BoundActor = Cast<AActor>(BoundObject))
							{
								for (UActorComponent* Component : TInlineComponentArray<UActorComponent*>(BoundActor))
								{
									if (Component)
									{
										ObjectsToRestore.Add(Component);
									}
								}
							}

							ObjectsToRestore.Add(BoundObject.Get());
						}
					}
				}

				for (UObject* ObjectToRestore : ObjectsToRestore)
				{
					Sequencer->PreAnimatedState.RestorePreAnimatedState(*ObjectToRestore);
				}
			}

			Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
		}
	};

	const UEnum* MovieSceneBlendType = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMovieSceneBlendType"));
	for (int32 NameIndex = 0; NameIndex < MovieSceneBlendType->NumEnums() - 1; ++NameIndex)
	{
		EMovieSceneBlendType BlendType = (EMovieSceneBlendType)MovieSceneBlendType->GetValueByIndex(NameIndex);

		// Include this if any section supports it
		bool bAnySupported = false;
		for (TWeakObjectPtr<UMovieSceneSection> WeakSection : InSections)
		{
			UMovieSceneSection* Section = WeakSection.Get();
			if (Section && Section->GetSupportedBlendTypes().Contains(BlendType))
			{
				bAnySupported = true;
				break;
			}
		}

		if (!bAnySupported)
		{
			continue;
		}

		FName EnumValueName = MovieSceneBlendType->GetNameByIndex(NameIndex);
		MenuBuilder.AddMenuEntry(
			MovieSceneBlendType->GetDisplayNameTextByIndex(NameIndex),
			MovieSceneBlendType->GetToolTipTextByIndex(NameIndex),
			FSlateIcon("EditorStyle", EnumValueName),
			FUIAction(
				FExecuteAction::CreateLambda(Execute, BlendType),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([InSections, BlendType]
				{
					int32 NumActiveBlendTypes = 0;
					for (TWeakObjectPtr<UMovieSceneSection> WeakSection : InSections)
					{
						UMovieSceneSection* Section = WeakSection.Get();
						if (Section && Section->GetBlendType() == BlendType)
						{
							++NumActiveBlendTypes;
						}
					}
					return NumActiveBlendTypes == InSections.Num();
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
}

FName FSequencerUtilities::GetUniqueName( FName CandidateName, const TArray<FName>& ExistingNames )
{
	if (!ExistingNames.Contains(CandidateName))
	{
		return CandidateName;
	}

	FString CandidateNameString = CandidateName.ToString();
	FString BaseNameString = CandidateNameString;
	if ( CandidateNameString.Len() >= 3 && CandidateNameString.Right(3).IsNumeric() )
	{
		BaseNameString = CandidateNameString.Left( CandidateNameString.Len() - 3 );
	}

	FName UniqueName = FName(*BaseNameString);
	int32 NameIndex = 1;
	while ( ExistingNames.Contains( UniqueName ) )
	{
		UniqueName = FName( *FString::Printf(TEXT("%s%i"), *BaseNameString, NameIndex ) );
		NameIndex++;
	}

	return UniqueName;
}

TArray<FString> FSequencerUtilities::GetAssociatedMapPackages(const ULevelSequence* InSequence)
{
	if (!InSequence)
	{
		return TArray<FString>();
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	const FName LSMapPathName = *InSequence->GetOutermost()->GetPathName();

	TArray<FString> AssociatedMaps;
	TArray<FAssetIdentifier> AssociatedAssets;

	// This makes the assumption these functions will append the array, and not clear it.
	AssetRegistryModule.Get().GetReferencers(LSMapPathName, AssociatedAssets);
	AssetRegistryModule.Get().GetDependencies(LSMapPathName, AssociatedAssets);

	for (FAssetIdentifier& AssociatedMap : AssociatedAssets)
	{
		FString MapFilePath;
		FString LevelPath = AssociatedMap.PackageName.ToString();
		if (FEditorFileUtils::IsMapPackageAsset(LevelPath, MapFilePath))
		{
			AssociatedMaps.AddUnique(LevelPath);
		}
	}

	AssociatedMaps.Sort([](const FString& One, const FString& Two) { return FPaths::GetBaseFilename(One) < FPaths::GetBaseFilename(Two); });
	return AssociatedMaps;
}

FGuid FSequencerUtilities::DoAssignActor(ISequencer * InSequencerPtr, AActor* Actor, FGuid InObjectBinding)
{
	if (Actor == nullptr)
	{
		return FGuid();
	}

	UMovieSceneSequence* OwnerSequence = InSequencerPtr->GetFocusedMovieSceneSequence();
	UMovieScene* OwnerMovieScene = OwnerSequence->GetMovieScene();

	if (OwnerMovieScene->IsReadOnly())
	{
		FSequencerUtilities::ShowReadOnlyError();
		return FGuid();
	}

	FScopedTransaction AssignActor(LOCTEXT("AssignActor", "Assign Actor"));

	Actor->Modify();
	OwnerSequence->Modify();
	OwnerMovieScene->Modify();

	TArrayView<TWeakObjectPtr<>> RuntimeObjects = InSequencerPtr->FindObjectsInCurrentSequence(InObjectBinding);

	UObject* RuntimeObject = RuntimeObjects.Num() ? RuntimeObjects[0].Get() : nullptr;

	// Replace the object itself
	FMovieScenePossessable NewPossessableActor;
	FGuid NewGuid;
	{
		// Get the object guid to assign, remove the binding if it already exists
		FGuid ParentGuid = InSequencerPtr->FindObjectId(*Actor, InSequencerPtr->GetFocusedTemplateID());
		FString NewActorLabel = Actor->GetActorLabel();
		if (ParentGuid.IsValid())
		{
			OwnerMovieScene->RemovePossessable(ParentGuid);
			OwnerSequence->UnbindPossessableObjects(ParentGuid);
		}

		// Add this object
		NewPossessableActor = FMovieScenePossessable(NewActorLabel, Actor->GetClass());
		NewGuid = NewPossessableActor.GetGuid();
		if (!NewPossessableActor.BindSpawnableObject(InSequencerPtr->GetFocusedTemplateID(), Actor, InSequencerPtr))
		{
			OwnerSequence->BindPossessableObject(NewPossessableActor.GetGuid(), *Actor, InSequencerPtr->GetPlaybackContext());
		}

		// Defer replacing this object until the components have been updated
	}

	auto UpdateComponent = [&](FGuid OldComponentGuid, UActorComponent* NewComponent)
	{
		FMovieSceneSequenceIDRef FocusedGuid = InSequencerPtr->GetFocusedTemplateID();

		// Get the object guid to assign, remove the binding if it already exists
		FGuid NewComponentGuid = InSequencerPtr->FindObjectId(*NewComponent, FocusedGuid);
		if (NewComponentGuid.IsValid())
		{
			OwnerMovieScene->RemovePossessable(NewComponentGuid);
			OwnerSequence->UnbindPossessableObjects(NewComponentGuid);
		}

		// Add this object
		FMovieScenePossessable NewPossessable(NewComponent->GetName(), NewComponent->GetClass());
		OwnerSequence->BindPossessableObject(NewPossessable.GetGuid(), *NewComponent, Actor);

		// Replace
		OwnerMovieScene->ReplacePossessable(OldComponentGuid, NewPossessable);
		OwnerSequence->UnbindPossessableObjects(OldComponentGuid);
		InSequencerPtr->State.Invalidate(OldComponentGuid, FocusedGuid);
		InSequencerPtr->State.Invalidate(NewPossessable.GetGuid(), FocusedGuid);

		FMovieScenePossessable* ThisPossessable = OwnerMovieScene->FindPossessable(NewPossessable.GetGuid());
		if (ensure(ThisPossessable))
		{
			ThisPossessable->SetParent(NewGuid);
		}
	};

	// Handle components
	AActor* ActorToReplace = Cast<AActor>(RuntimeObject);
	if (ActorToReplace != nullptr && ActorToReplace->IsActorBeingDestroyed() == false)
	{
		for (UActorComponent* ComponentToReplace : ActorToReplace->GetComponents())
		{
			if (ComponentToReplace != nullptr)
			{
				FGuid ComponentGuid = InSequencerPtr->FindObjectId(*ComponentToReplace, InSequencerPtr->GetFocusedTemplateID());
				if (ComponentGuid.IsValid())
				{
					bool bComponentWasUpdated = false;
					for (UActorComponent* NewComponent : Actor->GetComponents())
					{
						if (NewComponent->GetFullName(Actor) == ComponentToReplace->GetFullName(ActorToReplace))
						{
							UpdateComponent(ComponentGuid, NewComponent);
							bComponentWasUpdated = true;
						}
					}

					// Clear the parent guid since this possessable component doesn't match to any component on the new actor
					if (!bComponentWasUpdated)
					{
						FMovieScenePossessable* ThisPossessable = OwnerMovieScene->FindPossessable(ComponentGuid);
						ThisPossessable->SetParent(FGuid());
					}
				}
			}
		}
	}
	else // If the actor didn't exist, try to find components who's parent guids were the previous actors guid.
	{
		TMap<FString, UActorComponent*> ComponentNameToComponent;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			ComponentNameToComponent.Add(Component->GetName(), Component);
		}
		for (int32 i = 0; i < OwnerMovieScene->GetPossessableCount(); i++)
		{
			FMovieScenePossessable& OldPossessable = OwnerMovieScene->GetPossessable(i);
			if (OldPossessable.GetParent() == InObjectBinding)
			{
				UActorComponent** ComponentPtr = ComponentNameToComponent.Find(OldPossessable.GetName());
				if (ComponentPtr != nullptr)
				{
					UpdateComponent(OldPossessable.GetGuid(), *ComponentPtr);
				}
			}
		}
	}

	// Replace the actor itself after components have been updated
	OwnerMovieScene->ReplacePossessable(InObjectBinding, NewPossessableActor);
	OwnerSequence->UnbindPossessableObjects(InObjectBinding);

	InSequencerPtr->State.Invalidate(InObjectBinding, InSequencerPtr->GetFocusedTemplateID());
	InSequencerPtr->State.Invalidate(NewPossessableActor.GetGuid(), InSequencerPtr->GetFocusedTemplateID());

	// Try to fix up folders
	TArray<UMovieSceneFolder*> FoldersToCheck;
	FoldersToCheck.Append(InSequencerPtr->GetFocusedMovieSceneSequence()->GetMovieScene()->GetRootFolders());
	bool bFolderFound = false;
	while (FoldersToCheck.Num() > 0 && bFolderFound == false)
	{
		UMovieSceneFolder* Folder = FoldersToCheck[0];
		FoldersToCheck.RemoveAt(0);
		if (Folder->GetChildObjectBindings().Contains(InObjectBinding))
		{
			Folder->RemoveChildObjectBinding(InObjectBinding);
			Folder->AddChildObjectBinding(NewGuid);
			bFolderFound = true;
		}

		for (UMovieSceneFolder* ChildFolder : Folder->GetChildFolders())
		{
			FoldersToCheck.Add(ChildFolder);
		}
	}

	InSequencerPtr->RestorePreAnimatedState();

	InSequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

	return NewGuid;
}

void FSequencerUtilities::UpdateBindingIDs(ISequencer* InSequencerPtr, UMovieSceneCompiledDataManager* InCompiledDataManagerPtr, FGuid OldGuid, FGuid NewGuid)
{
	FMovieSceneSequenceIDRef FocusedGuid = InSequencerPtr->GetFocusedTemplateID();

	TMap<UE::MovieScene::FFixedObjectBindingID, UE::MovieScene::FFixedObjectBindingID> OldFixedToNewFixedMap;
	OldFixedToNewFixedMap.Add(UE::MovieScene::FFixedObjectBindingID(OldGuid, FocusedGuid), UE::MovieScene::FFixedObjectBindingID(NewGuid, FocusedGuid));

	const FMovieSceneSequenceHierarchy* Hierarchy = InCompiledDataManagerPtr->FindHierarchy(InSequencerPtr->GetEvaluationTemplate().GetCompiledDataID());

	if (UMovieScene* MovieScene = InSequencerPtr->GetRootMovieSceneSequence()->GetMovieScene())
	{
		for (UMovieSceneSection* Section : MovieScene->GetAllSections())
		{
			if (Section)
			{
				Section->OnBindingIDsUpdated(OldFixedToNewFixedMap, InSequencerPtr->GetRootTemplateID(), Hierarchy, *InSequencerPtr);
			}
		}
	}

	if (Hierarchy)
	{
		for (const TTuple<FMovieSceneSequenceID, FMovieSceneSubSequenceData>& Pair : Hierarchy->AllSubSequenceData())
		{
			if (UMovieSceneSequence* Sequence = Pair.Value.GetSequence())
			{
				if (UMovieScene* MovieScene = Sequence->GetMovieScene())
				{
					for (UMovieSceneSection* Section : MovieScene->GetAllSections())
					{
						if (Section)
						{
							Section->OnBindingIDsUpdated(OldFixedToNewFixedMap, Pair.Key, Hierarchy, *InSequencerPtr);
						}
					}
				}
			}
		}
	}
}

void FSequencerUtilities::ShowReadOnlyError()
{
	FNotificationInfo Info(LOCTEXT("SequenceReadOnly", "Sequence is read only."));
	Info.ExpireDuration = 5.0f;
	FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
}


#undef LOCTEXT_NAMESPACE
PRAGMA_ENABLE_OPTIMIZATION