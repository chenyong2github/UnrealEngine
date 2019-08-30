// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SExposeBindingWidget.h"

#include "Sequencer.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"

#include "DisplayNodes/SequencerObjectBindingNode.h"

#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"

#include "EditorStyleSet.h"
#include "EditorFontGlyphs.h"
#include "ScopedTransaction.h"

#include "Algo/Sort.h"

#define LOCTEXT_NAMESPACE "SExposeBindingWidget"

void SExposeBindingWidget::Construct(const FArguments& InArgs, TWeakPtr<ISequencer> InWeakSequencer)
{
	WeakSequencer = InWeakSequencer;

	Reconstruct();
}

void SExposeBindingWidget::Reconstruct()
{
	TSharedPtr<ISequencer> Sequencer  = WeakSequencer.Pin();
	UMovieSceneSequence*   Sequence   = Sequencer ? Sequencer->GetFocusedMovieSceneSequence() : nullptr;
	UMovieScene*           MovieScene = Sequence  ? Sequence->GetMovieScene()                 : nullptr;

	if (!MovieScene)
	{
		ChildSlot
		[
			SNullWidget::NullWidget
		];

		return;
	}

	TArray<FGuid> SelectedObjectBindings;
	for (const TSharedRef<FSequencerDisplayNode>& Node : Sequencer->GetSelection().GetSelectedOutlinerNodes())
	{
		if (Node->GetType() == ESequencerNode::Object)
		{
			SelectedObjectBindings.Add(StaticCastSharedRef<FSequencerObjectBindingNode>(Node)->GetObjectBinding());
		}
	}

	TSharedRef<SVerticalBox> VerticalBox = SNew(SVerticalBox);

	FMovieSceneSequenceID      SequenceID = Sequencer->GetFocusedTemplateID();

	TArray<FName> ExposedNames;

	for (const TTuple<FName, FMovieSceneObjectBindingIDs>& Pair : MovieScene->AllBindingGroups())
	{
		bool bContainsAll = false;
		for (const FGuid& ID : SelectedObjectBindings)
		{
			FMovieSceneObjectBindingID ThisBindingID(ID, SequenceID);
			if (!Pair.Value.IDs.Contains(ThisBindingID))
			{
				bContainsAll = false;
				break;
			}
			
			bContainsAll = true;
		}

		if (bContainsAll)
		{
			ExposedNames.Add(Pair.Key);
		}
	}

	// Sort exposed names alphabetically
	Algo::Sort(ExposedNames, [](const FName& A, const FName& B) { return A.Compare(B) < 0; });

	for (FName ExposedName : ExposedNames)
	{
		VerticalBox->AddSlot()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(FMargin(7.f, 5.f))
			[
				SNew(STextBlock)
				.Text(FText::FromName(ExposedName))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(7.f, 5.f))
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.OnClicked(this, &SExposeBindingWidget::RemoveFromExposedName, ExposedName)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FLinearColor::Red)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
					.Text(FEditorFontGlyphs::Times)
				]
			]
		];
	}

	TSharedRef<SEditableTextBox> EditableText = SNew(SEditableTextBox)
	.OnTextCommitted(this, &SExposeBindingWidget::OnNewTextCommitted)
	.HintText(LOCTEXT("AddNew_Hint", "Add new"));

	auto OnClicked = [this, WeakEditableText = TWeakPtr<SEditableTextBox>(EditableText)]() -> FReply
	{
		TSharedPtr<SEditableTextBox> LocalEditableText = WeakEditableText.Pin();
		if (LocalEditableText)
		{
			this->OnNewTextCommitted(LocalEditableText->GetText(), ETextCommit::OnEnter);
		}
		return FReply::Handled();
	};

	VerticalBox->AddSlot()
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(FMargin(7.f, 5.f))
		[
			SNew(SBox)
			.MinDesiredWidth(100.f)
			[
				EditableText
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(FMargin(7.f, 5.f))
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.OnClicked_Lambda(OnClicked)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FLinearColor::Green)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
				.Text(FEditorFontGlyphs::Plus)
			]
		]
	];

	ChildSlot
	[
		VerticalBox
	];
}

void SExposeBindingWidget::OnNewTextCommitted(const FText& InNewText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter && !InNewText.IsEmpty())
	{
		FName NewName = *InNewText.ToString();
		ExposeAsName(NewName);
	}
}

void SExposeBindingWidget::ExposeAsName(FName InNewName)
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	UMovieSceneSequence*   Sequence = Sequencer ? Sequencer->GetFocusedMovieSceneSequence() : nullptr;
	UMovieScene*           MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;

	FScopedTransaction Transaction(FText::Format(LOCTEXT("ExposeBinding_Transaction", "Expose binding as {0}"), FText::FromName(InNewName)));

	MovieScene->Modify();

	for (const TSharedRef<FSequencerDisplayNode>& Node : Sequencer->GetSelection().GetSelectedOutlinerNodes())
	{
		if (Node->GetType() == ESequencerNode::Object)
		{
			FGuid ObjectBindingID = StaticCastSharedRef<FSequencerObjectBindingNode>(Node)->GetObjectBinding();

			MovieScene->ExposeBinding(InNewName, FMovieSceneObjectBindingID(ObjectBindingID, Sequencer->GetFocusedTemplateID(), EMovieSceneObjectBindingSpace::Local));
		}
	}

	Reconstruct();
}

FReply SExposeBindingWidget::RemoveFromExposedName(FName InNameToRemove)
{
	TSharedPtr<ISequencer> Sequencer  = WeakSequencer.Pin();
	UMovieSceneSequence*   Sequence   = Sequencer ? Sequencer->GetFocusedMovieSceneSequence() : nullptr;
	UMovieScene*           MovieScene = Sequence  ? Sequence->GetMovieScene()                 : nullptr;

	FScopedTransaction Transaction(FText::Format(LOCTEXT("ExposeBinding_Transaction", "Stop exposing binding as {0}"), FText::FromName(InNameToRemove)));

	MovieScene->Modify();

	for (const TSharedRef<FSequencerDisplayNode>& Node : Sequencer->GetSelection().GetSelectedOutlinerNodes())
	{
		if (Node->GetType() == ESequencerNode::Object)
		{
			FGuid ObjectBindingID = StaticCastSharedRef<FSequencerObjectBindingNode>(Node)->GetObjectBinding();

			MovieScene->RemoveExposedBinding(InNameToRemove, FMovieSceneObjectBindingID(ObjectBindingID, Sequencer->GetFocusedTemplateID(), EMovieSceneObjectBindingSpace::Local));
		}
	}

	Reconstruct();
	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE