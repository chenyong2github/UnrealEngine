// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneObjectBindingIDPicker.h"
#include "IPropertyUtilities.h"
#include "MovieSceneBindingOwnerInterface.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "SequenceBindingTree.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Textures/SlateIcon.h"
#include "EditorStyleSet.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "ISequencer.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Evaluation/MovieSceneSequenceHierarchy.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "MovieSceneObjectBindingIDPicker"

bool FMovieSceneObjectBindingIDPicker::IsEmpty() const
{
	return !DataTree.IsValid() || DataTree->IsEmpty();
}

void FMovieSceneObjectBindingIDPicker::Initialize()
{
	if (!DataTree.IsValid())
	{
		DataTree = MakeShared<FSequenceBindingTree>();
	}

	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();

	UMovieSceneSequence* Sequence = Sequencer.IsValid() ? Sequencer->GetRootMovieSceneSequence() : GetSequence();
	UMovieSceneSequence* ActiveSequence = Sequencer.IsValid() ? Sequencer->GetFocusedMovieSceneSequence() : GetSequence();
	FMovieSceneSequenceID ActiveSequenceID = Sequencer.IsValid() ? Sequencer->GetFocusedTemplateID() : MovieSceneSequenceID::Root;

	DataTree->ConditionalRebuild(Sequence, ActiveSequence, ActiveSequenceID);

	UpdateCachedData();
}

void FMovieSceneObjectBindingIDPicker::OnGetMenuContent(FMenuBuilder& MenuBuilder, TSharedPtr<FSequenceBindingNode> Node)
{
	check(Node.IsValid());

	bool bHadAnyEntries = false;

	if (Node->BindingID.GetGuid().IsValid())
	{
		bHadAnyEntries = true;
		MenuBuilder.AddMenuEntry(
			Node->DisplayString,
			FText(),
			Node->Icon,
			FUIAction(
				FExecuteAction::CreateRaw(this, &FMovieSceneObjectBindingIDPicker::SetBindingId, Node->BindingID)
			)
		);
	}

	for (const TSharedPtr<FSequenceBindingNode>& Child : Node->Children)
	{
		check(Child.IsValid())

		if (!Child->BindingID.GetGuid().IsValid())
		{
			if (Child->Children.Num())
			{
				bHadAnyEntries = true;
				MenuBuilder.AddSubMenu(
					Child->DisplayString,
					FText(),
					FNewMenuDelegate::CreateRaw(this, &FMovieSceneObjectBindingIDPicker::OnGetMenuContent, Child),
					false,
					Child->Icon,
					false
					);
			}
		}
		else
		{
			bHadAnyEntries = true;
			MenuBuilder.AddMenuEntry(
				Child->DisplayString,
				FText(),
				Child->Icon,
				FUIAction(
					FExecuteAction::CreateRaw(this, &FMovieSceneObjectBindingIDPicker::SetBindingId, Child->BindingID)
				)
			);
		}
	}

	if (!bHadAnyEntries)
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("NoEntries", "No Object Bindings"), FText(), FSlateIcon(), FUIAction());
	}
}

TSharedRef<SWidget> FMovieSceneObjectBindingIDPicker::GetPickerMenu()
{
	// Close self only to enable use inside context menus
	FMenuBuilder MenuBuilder(true, nullptr, nullptr, true);

	Initialize();
	GetPickerMenu(MenuBuilder);

	// Hold onto the menu widget so we can destroy it manually
	TSharedRef<SWidget> MenuWidget = MenuBuilder.MakeWidget();
	DismissWidget = MenuWidget;
	return MenuWidget;
}

void FMovieSceneObjectBindingIDPicker::GetPickerMenu(FMenuBuilder& MenuBuilder)
{
	OnGetMenuContent(MenuBuilder, DataTree->GetRootNode());
}

TSharedRef<SWidget> FMovieSceneObjectBindingIDPicker::GetCurrentItemWidget(TSharedRef<STextBlock> TextContent)
{
	TextContent->SetText(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FMovieSceneObjectBindingIDPicker::GetCurrentText)));
	
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				SNew(SImage)
				.Image_Raw(this, &FMovieSceneObjectBindingIDPicker::GetCurrentIconBrush)
			]

			+ SOverlay::Slot()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Right)
			[
				SNew(SImage)
				.Visibility_Raw(this, &FMovieSceneObjectBindingIDPicker::GetSpawnableIconOverlayVisibility)
				.Image(FEditorStyle::GetBrush("Sequencer.SpawnableIconOverlay"))
			]
		]

		+ SHorizontalBox::Slot()
		.Padding(4.f,0,0,0)
		.VAlign(VAlign_Center)
		[
			TextContent
		];
}

void FMovieSceneObjectBindingIDPicker::SetBindingId(FMovieSceneObjectBindingID InBindingId)
{
	SetRemappedCurrentValue(InBindingId);
	UpdateCachedData();

	TSharedPtr<SWidget> MenuWidget = DismissWidget.Pin();
	if (MenuWidget.IsValid())
	{
		FSlateApplication::Get().DismissMenuByWidget(MenuWidget.ToSharedRef());
	}
}

void FMovieSceneObjectBindingIDPicker::UpdateCachedData()
{
	FMovieSceneObjectBindingID CurrentValue = GetRemappedCurrentValue();
	TSharedPtr<FSequenceBindingNode> Object = CurrentValue.IsValid() ? DataTree->FindNode(CurrentValue) : nullptr;

	if (!Object.IsValid())
	{
		CurrentIcon = FSlateIcon();
		CurrentText = LOCTEXT("UnresolvedBinding", "Unresolved Binding");
		ToolTipText = LOCTEXT("UnresolvedBinding_ToolTip", "The specified binding could not be located in the sequence");
		bIsCurrentItemSpawnable = false;
	}
	else
	{
		CurrentText = Object->DisplayString;
		CurrentIcon = Object->Icon;
		bIsCurrentItemSpawnable = Object->bIsSpawnable;

		ToolTipText = FText();
		while (Object.IsValid() && Object->BindingID != FMovieSceneObjectBindingID())
		{
			ToolTipText = ToolTipText.IsEmpty() ? Object->DisplayString : FText::Format(LOCTEXT("ToolTipFormat", "{0} -> {1}"), Object->DisplayString, ToolTipText);
			Object = DataTree->FindNode(Object->ParentID);
		}
	}
}

FText FMovieSceneObjectBindingIDPicker::GetToolTipText() const
{
	return ToolTipText;
}

FText FMovieSceneObjectBindingIDPicker::GetCurrentText() const
{
	return CurrentText;
}

FSlateIcon FMovieSceneObjectBindingIDPicker::GetCurrentIcon() const
{
	return CurrentIcon;
}

const FSlateBrush* FMovieSceneObjectBindingIDPicker::GetCurrentIconBrush() const
{
	return CurrentIcon.GetOptionalIcon();
}

EVisibility FMovieSceneObjectBindingIDPicker::GetSpawnableIconOverlayVisibility() const
{
	return bIsCurrentItemSpawnable ? EVisibility::Visible : EVisibility::Collapsed;
}

FMovieSceneObjectBindingID FMovieSceneObjectBindingIDPicker::GetRemappedCurrentValue() const
{
	FMovieSceneObjectBindingID ID = GetCurrentValue();

	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();

	// If the ID is in local space, remap it to the root space as according to the LocalSequenceID we were created with
	if (Sequencer.IsValid() && LocalSequenceID != MovieSceneSequenceID::Root && ID.IsValid() && ID.GetBindingSpace() == EMovieSceneObjectBindingSpace::Local)
	{
		ID = ID.ResolveLocalToRoot(LocalSequenceID, Sequencer->GetEvaluationTemplate().GetHierarchy());
	}

	return ID;
}

void FMovieSceneObjectBindingIDPicker::SetRemappedCurrentValue(FMovieSceneObjectBindingID InValue)
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();

	// If we have a local sequence ID set, and the supplied binding is in root space, we attempt to remap it into the local sequence ID's space, and use a sequence ID
	// that will resolve from LocalSequenceID instead of from the root. This ensures that you can work on sub sequences on their own, or within a master sequence
	// and the binding will resolve correctly.
	if (LocalSequenceID.IsValid() && Sequencer.IsValid() && InValue.GetGuid().IsValid() && InValue.GetBindingSpace() == EMovieSceneObjectBindingSpace::Root)
	{
		const FMovieSceneSequenceHierarchy& Hierarchy = Sequencer->GetEvaluationTemplate().GetHierarchy();

		FMovieSceneSequenceID NewLocalSequenceID = MovieSceneSequenceID::Root;
		FMovieSceneSequenceID CurrentSequenceID = InValue.GetSequenceID();
		
		while (CurrentSequenceID.IsValid())
		{
			if (LocalSequenceID == CurrentSequenceID)
			{
				// Found it
				InValue = FMovieSceneObjectBindingID(InValue.GetGuid(), NewLocalSequenceID, EMovieSceneObjectBindingSpace::Local);
				break;
			}

			const FMovieSceneSequenceHierarchyNode* CurrentNode = Hierarchy.FindNode(CurrentSequenceID);
			if (!ensureAlwaysMsgf(CurrentNode, TEXT("Malformed sequence hierarchy")))
			{
				break;
			}
			else if (const FMovieSceneSubSequenceData* SubData = Hierarchy.FindSubData(CurrentSequenceID))
			{
				NewLocalSequenceID = NewLocalSequenceID.AccumulateParentID(SubData->DeterministicSequenceID);
			}
			
			CurrentSequenceID = CurrentNode->ParentID;
		}
	}

	SetCurrentValue(InValue);
}

#undef LOCTEXT_NAMESPACE
