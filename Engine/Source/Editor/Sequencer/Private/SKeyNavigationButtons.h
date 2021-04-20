// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Styling/SlateColor.h"
#include "Input/Reply.h"
#include "DisplayNodes/SequencerObjectBindingNode.h"
#include "DisplayNodes/SequencerSectionKeyAreaNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Sequencer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "SequencerCommonHelpers.h"
#include "MovieSceneCommonHelpers.h"
#include "ScopedTransaction.h"
#include "SequencerCommands.h"
#include "ISequencerModule.h"
#include "Modules/ModuleManager.h"
#include "IKeyArea.h"
#include "SequencerAddKeyOperation.h"

#define LOCTEXT_NAMESPACE "SKeyNavigationButtons"

/**
 * A widget for navigating between keys on a sequencer track
 */
class SKeyNavigationButtons
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SKeyNavigationButtons){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FSequencerDisplayNode>& InDisplayNode)
	{
		FText SetKeyToolTip = FText::Format(LOCTEXT("AddKeyButton", "Add a new key at the current time ({0})"), FSequencerCommands::Get().SetKey->GetInputText());
		FText PreviousKeyToolTip = FText::Format(LOCTEXT("PreviousKeyButton", "Set the time to the previous key ({0})"), FSequencerCommands::Get().StepToPreviousKey->GetInputText());
		FText NextKeyToolTip = FText::Format(LOCTEXT("NextKeyButton", "Set the time to the next key ({0})"), FSequencerCommands::Get().StepToNextKey->GetInputText());

		DisplayNode = InDisplayNode;

		const FSlateBrush* NoBorder = FEditorStyle::GetBrush( "NoBorder" );

		TAttribute<FLinearColor> HoverTint(this, &SKeyNavigationButtons::GetHoverTint);

		ChildSlot
		[
			SNew(SHorizontalBox)
			
			// Previous key slot
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(3, 0, 0, 0)
			[
				SNew(SBorder)
				.Padding(0)
				.BorderImage(NoBorder)
				.ColorAndOpacity(HoverTint)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "FlatButton")
					.ToolTipText(PreviousKeyToolTip)
					.OnClicked(this, &SKeyNavigationButtons::OnPreviousKeyClicked)
					.ForegroundColor( FSlateColor::UseForeground() )
					.ContentPadding(0)
					.IsFocusable(false)
					[
						SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.7"))
						.Text(FText::FromString(FString(TEXT("\xf060"))) /*fa-arrow-left*/)
					]
				]
			]
			// Add key slot
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SBorder)
				.Padding(0)
				.BorderImage(NoBorder)
				.ColorAndOpacity(HoverTint)
				.IsEnabled(!InDisplayNode->GetSequencer().IsReadOnly())
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "FlatButton")
					.ToolTipText(SetKeyToolTip)
					.OnClicked(this, &SKeyNavigationButtons::OnAddKeyClicked)
					.ForegroundColor( FSlateColor::UseForeground() )
					.ContentPadding(0)
					.IsFocusable(false)
					[
						SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.7"))
						.Text(FText::FromString(FString(TEXT("\xf055"))) /*fa-plus-circle*/)
					]
				]
			]
			// Next key slot
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SBorder)
				.Padding(0)
				.BorderImage(NoBorder)
				.ColorAndOpacity(HoverTint)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "FlatButton")
					.ToolTipText(NextKeyToolTip)
					.OnClicked(this, &SKeyNavigationButtons::OnNextKeyClicked)
					.ContentPadding(0)
					.ForegroundColor( FSlateColor::UseForeground() )
					.IsFocusable(false)
					[
						SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.7"))
						.Text(FText::FromString(FString(TEXT("\xf061"))) /*fa-arrow-right*/)
					]
				]
			]
		];
	}

	FLinearColor GetHoverTint() const
	{
		return DisplayNode->IsHovered() ? FLinearColor(1,1,1,0.9f) : FLinearColor(1,1,1,0.4f);
	}

	FReply OnPreviousKeyClicked()
	{
		FSequencer& Sequencer = DisplayNode->GetSequencer();

		FFrameTime ClosestPreviousKeyDistance = FFrameTime(TNumericLimits<int32>::Max(), 0.99999f);
		FFrameTime CurrentTime = Sequencer.GetLocalTime().Time;
		TOptional<FFrameTime> PreviousTime;

		TArray<FFrameNumber> AllTimes;

		TSet<TSharedPtr<IKeyArea>> KeyAreas;
		SequencerHelpers::GetAllKeyAreas( DisplayNode, KeyAreas );

		for ( TSharedPtr<IKeyArea> Keyarea : KeyAreas )
		{
			Keyarea->GetKeyTimes(AllTimes);
		}

		TSet<TWeakObjectPtr<UMovieSceneSection> > Sections;
		SequencerHelpers::GetAllSections( DisplayNode.ToSharedRef(), Sections );
		for ( TWeakObjectPtr<UMovieSceneSection> Section : Sections )
		{
			if (Section.IsValid())
			{
				Section->GetSnapTimes(AllTimes, true);
			}
		}

		for (FFrameNumber Time : AllTimes)
		{
			if (Time < CurrentTime && CurrentTime - Time < ClosestPreviousKeyDistance)
			{
				PreviousTime = Time;
				ClosestPreviousKeyDistance = CurrentTime - Time;
			}
		}

		if (!PreviousTime.IsSet() && AllTimes.Num() > 0)
		{
			AllTimes.Sort();

			PreviousTime = AllTimes.Last();
		}

		if (PreviousTime.IsSet())
		{
			Sequencer.SetLocalTime(PreviousTime.GetValue());
		}
		return FReply::Handled();
	}


	FReply OnNextKeyClicked()
	{
		FSequencer& Sequencer = DisplayNode->GetSequencer();
		FFrameTime ClosestNextKeyDistance = FFrameTime(TNumericLimits<int32>::Max(), 0.99999f);
		FFrameTime CurrentTime = Sequencer.GetLocalTime().Time;
		TOptional<FFrameTime> NextTime;

		TArray<FFrameNumber> AllTimes;

		TSet<TSharedPtr<IKeyArea>> KeyAreas;
		SequencerHelpers::GetAllKeyAreas( DisplayNode, KeyAreas );

		for ( TSharedPtr<IKeyArea> Keyarea : KeyAreas )
		{
			Keyarea->GetKeyTimes(AllTimes);
		}

		TSet<TWeakObjectPtr<UMovieSceneSection> > Sections;
		SequencerHelpers::GetAllSections( DisplayNode.ToSharedRef(), Sections );
		for ( TWeakObjectPtr<UMovieSceneSection> Section : Sections )
		{
			if (Section.IsValid())
			{
				Section->GetSnapTimes(AllTimes, true);
			}
		}

		for (FFrameNumber Time : AllTimes)
		{
			if (Time > CurrentTime && Time - CurrentTime < ClosestNextKeyDistance)
			{
				NextTime = Time;
				ClosestNextKeyDistance = Time - CurrentTime;
			}
		}

		if (!NextTime.IsSet() && AllTimes.Num() > 0)
		{
			AllTimes.Sort();

			NextTime = AllTimes[0];
		}

		if (NextTime.IsSet())
		{
			Sequencer.SetLocalTime(NextTime.GetValue());
		}

		return FReply::Handled();
	}


	FReply OnAddKeyClicked()
	{
		using namespace UE::Sequencer;
		FSequencer& Sequencer   = DisplayNode->GetSequencer();
		FFrameTime  CurrentTime = Sequencer.GetLocalTime().Time;

		FScopedTransaction Transaction(LOCTEXT("AddKeys", "Add Keys at Current Time"));
		FAddKeyOperation::FromNode(DisplayNode.ToSharedRef()).Commit(CurrentTime.FrameNumber, Sequencer);

		return FReply::Handled();
	}

	TSharedPtr<FSequencerDisplayNode> DisplayNode;
};

#undef LOCTEXT_NAMESPACE
