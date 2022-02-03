// Copyright Epic Games, Inc. All Rights Reserved.

#include "SStateTreeViewRow.h"
#include "SStateTreeView.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "Templates/SharedPointer.h"

#include "EditorStyleSet.h"
#include "EditorFontGlyphs.h"
#include "StateTreeEditorStyle.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Layout/SScrollBox.h"

#include "StateTree.h"
#include "StateTreeState.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeViewModel.h"
#include "Algo/ForEach.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE { namespace StateTreeEditor {

static void AddUnique(TArray<FText>& Array, const FText& NewItem)
{
	if (!Array.FindByPredicate([&NewItem](const FText& Item) { return Item.IdenticalTo(NewItem); }))
	{
		Array.Add(NewItem);
	}
}
	
}} // UE::StateTreeEditor

void SStateTreeViewRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, UStateTreeState* InState, TSharedRef<FStateTreeViewModel> InStateTreeViewModel)
{
	StateTreeViewModel = InStateTreeViewModel;
	WeakState = InState;

	STableRow<UStateTreeState*>::ConstructInternal(STableRow::FArguments()
        .Padding(5.f)
        .OnDragDetected(this, &SStateTreeViewRow::HandleDragDetected)
        .OnCanAcceptDrop(this, &SStateTreeViewRow::HandleCanAcceptDrop)
        .OnAcceptDrop(this, &SStateTreeViewRow::HandleAcceptDrop)
        .Style(&FStateTreeEditorStyle::Get()->GetWidgetStyle<FTableRowStyle>("StateTree.Selection"))
    , InOwnerTableView);

	static const FLinearColor TasksBackground = FLinearColor(FColor(17, 117, 131));
	static const FLinearColor EvaluatorsBackground = FLinearColor(FColor(48, 48, 48));
	
	this->ChildSlot
    .HAlign(HAlign_Fill)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .VAlign(VAlign_Fill)
        .HAlign(HAlign_Left)
        .AutoWidth()
        [
            SNew(SExpanderArrow, SharedThis(this))
            .ShouldDrawWires(true)
            .IndentAmount(32)
            .BaseIndentLevel(0)
        ]

        // State Box
        + SHorizontalBox::Slot()
        .VAlign(VAlign_Center)
        .Padding(FMargin(0.0f, 4.0f))
        .AutoWidth()
        [
            SNew(SBox)
            .HeightOverride(28.0f)
            .VAlign(VAlign_Fill)
            [
                SNew(SBorder)
                .BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor(this, &SStateTreeViewRow::GetTitleColor)
                .Padding(FMargin(16.0f, 0.0f, 16.0f, 0.0f))
                [
                    // Conditions icon
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .VAlign(VAlign_Center)
                    .AutoWidth()
                    [
                        SNew(SBox)
                        .Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
                        .Visibility(this, &SStateTreeViewRow::GetConditionVisibility)
                        [
                            SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("Icons.Help"))
						]
					]

					// Selector icon
                    + SHorizontalBox::Slot()
                    .VAlign(VAlign_Center)
                    .AutoWidth()
                    [
                        SNew(SBox)
                        .Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
                        .Visibility(this, &SStateTreeViewRow::GetSelectorVisibility)
                        [
                            SNew(STextBlock)
                            .Text(FEditorFontGlyphs::Level_Down)
                            .TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Icon")
                        ]
                    ]

                    // State Name
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SAssignNew(NameTextBlock, SInlineEditableTextBlock)
						.Style(FStateTreeEditorStyle::Get(), "StateTree.State.TitleInlineEditableText")
						.OnVerifyTextChanged(this, &SStateTreeViewRow::VerifyNodeTextChanged)
						.OnTextCommitted(this, &SStateTreeViewRow::HandleNodeLabelTextCommitted)
						.Text(this, &SStateTreeViewRow::GetStateDesc)
						.Clipping(EWidgetClipping::ClipToBounds)
						.IsSelected(this, &SStateTreeViewRow::IsSelected)
					]
				]
			]
		]

		// Evaluators Box
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.0f, 4.0f))
		.AutoWidth()
		[
			SNew(SBox)
			.HeightOverride(28.0f)
			.VAlign(VAlign_Fill)
			.Visibility(this, &SStateTreeViewRow::GetEvaluatorsVisibility)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(EvaluatorsBackground)
				.Padding(FMargin(12.0f, 0.0f, 16.0f, 0.0f))
				[
					// Evaluator icon
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(FEditorFontGlyphs::Crosshairs)
						.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.DetailsIcon")
					]

					// Evalutors
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(this, &SStateTreeViewRow::GetEvaluatorsDesc)
						.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Details")
					]
				]
			]
		]

		// Tasks Box
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.0f, 4.0f))
		.AutoWidth()
		[
			SNew(SBox)
			.HeightOverride(28.0f)
			.VAlign(VAlign_Fill)
			.Visibility(this, &SStateTreeViewRow::GetTasksVisibility)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(TasksBackground)
				.Padding(FMargin(12.0f, 0.0f, 16.0f, 0.0f))
				[
					// Task icon
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(FEditorFontGlyphs::Paper_Plane)
						.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.DetailsIcon")
					]

					// Tasks list
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(this, &SStateTreeViewRow::GetTasksDesc)
						.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Details")
					]
				]
			]
		]

		// Completed transitions
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(FMargin(8.0f, 0.0f, 0, 0.0f))
			[
				SNew(STextBlock)
				.Text(this, &SStateTreeViewRow::GetCompletedTransitionsIcon)
				.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Icon")
				.Visibility(this, &SStateTreeViewRow::GetCompletedTransitionVisibility)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(FMargin(4.0f, 0, 0, 0))
			[
				SNew(STextBlock)
				.Text(this, &SStateTreeViewRow::GetCompletedTransitionsDesc)
				.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Details")
				.Visibility(this, &SStateTreeViewRow::GetCompletedTransitionVisibility)
			]
		]

		// Succeeded transitions
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(FMargin(8.0f, 0.0f, 0, 0))
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FEditorFontGlyphs::Check_Circle)
                .ColorAndOpacity(FLinearColor(FColor(110,143,67)))
				.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Icon")
				.Visibility(this, &SStateTreeViewRow::GetSucceededTransitionVisibility)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(FMargin(4.0f, 0.0f, 0, 0))
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SStateTreeViewRow::GetSucceededTransitionIcon)
				.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Icon")
				.Visibility(this, &SStateTreeViewRow::GetSucceededTransitionVisibility)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(FMargin(4.0f, 0, 0, 0))
			[
				SNew(STextBlock)
				.Text(this, &SStateTreeViewRow::GetSucceededTransitionDesc)
				.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Details")
				.Visibility(this, &SStateTreeViewRow::GetSucceededTransitionVisibility)
			]
		]

		// Failed transitions
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(FMargin(8.0f, 0.0f, 0, 0))
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FEditorFontGlyphs::Times_Circle)
                .ColorAndOpacity(FLinearColor(FColor(187,77,42)))
				.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Icon")
				.Visibility(this, &SStateTreeViewRow::GetFailedTransitionVisibility)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(FMargin(4.0f, 0.0f, 0, 0))
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SStateTreeViewRow::GetFailedTransitionIcon)
				.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Icon")
				.Visibility(this, &SStateTreeViewRow::GetFailedTransitionVisibility)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(FMargin(4.0f, 0, 0, 0))
			[
				SNew(STextBlock)
				.Text(this, &SStateTreeViewRow::GetFailedTransitionDesc)
				.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Details")
				.Visibility(this, &SStateTreeViewRow::GetFailedTransitionVisibility)
			]
		]
		// Transitions
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(FMargin(8.0f, 0.0f, 0, 0))
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Icons.Help"))
                .ColorAndOpacity(FLinearColor(FColor(31,151,167)))
				.Visibility(this, &SStateTreeViewRow::GetConditionalTransitionsVisibility)
			]
            + SHorizontalBox::Slot()
            .VAlign(VAlign_Center)
            .Padding(FMargin(4.0f, 0.0f, 0, 0))
            .AutoWidth()
            [
                SNew(STextBlock)
                .Text(FEditorFontGlyphs::Long_Arrow_Right)
                .TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Icon")
                .Visibility(this, &SStateTreeViewRow::GetConditionalTransitionsVisibility)
            ]
            + SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(FMargin(4.0f, 0, 0, 0))
			[
				SNew(STextBlock)
				.Text(this, &SStateTreeViewRow::GetConditionalTransitionsDesc)
				.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Details")
				.Visibility(this, &SStateTreeViewRow::GetConditionalTransitionsVisibility)
			]
		]
	];
}

void SStateTreeViewRow::RequestRename()
{
	if (NameTextBlock)
	{
		NameTextBlock->EnterEditingMode();
	}
}

FSlateColor SStateTreeViewRow::GetTitleColor() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		if (StateTreeViewModel && StateTreeViewModel->IsSelected(State))
		{
			return FLinearColor(FColor(236, 134, 39));
		}
		if (IsRoutine())
		{
			return FLinearColor(FColor(17, 117, 131));
		}
	}

	return FLinearColor(FColor(31, 151, 167));
}

FText SStateTreeViewRow::GetStateDesc() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return FText::FromName(State->Name);
	}
	return FText::FromName(FName());
}

EVisibility SStateTreeViewRow::GetConditionVisibility() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return State->EnterConditions.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
	}
	return EVisibility::Collapsed;
}

EVisibility SStateTreeViewRow::GetSelectorVisibility() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return State->Children.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
	}
	return EVisibility::Collapsed;
}

EVisibility SStateTreeViewRow::GetEvaluatorsVisibility() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		int32 ValidCount = 0;
		for (int32 i = 0; i < State->Evaluators.Num(); i++)
		{
			if (const FStateTreeEvaluatorBase* Eval = State->Evaluators[i].Node.GetPtr<FStateTreeEvaluatorBase>())
			{
				ValidCount++;
			}
		}
		return ValidCount > 0 ? EVisibility::Visible : EVisibility::Collapsed;
	}
	return EVisibility::Collapsed;
}

FText SStateTreeViewRow::GetEvaluatorsDesc() const
{
	const UStateTreeState* State = WeakState.Get();
	if (!State)
	{
		return FText::GetEmpty();
	}

	TArray<FText> Names;

	for (int32 i = 0; i < State->Evaluators.Num(); i++)
	{
		if (const FStateTreeEvaluatorBase* Eval = State->Evaluators[i].Node.GetPtr<FStateTreeEvaluatorBase>())
		{
			Names.Add(FText::FromName(Eval->Name));
		}
	}

	return FText::Join((FText::FromString(TEXT(", "))), Names);
}

EVisibility SStateTreeViewRow::GetTasksVisibility() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		int32 ValidCount = 0;
		for (int32 i = 0; i < State->Tasks.Num(); i++)
		{
			if (const FStateTreeTaskBase* Task = State->Tasks[i].Node.GetPtr<FStateTreeTaskBase>())
			{
				ValidCount++;
			}
		}
		return ValidCount > 0 ? EVisibility::Visible : EVisibility::Collapsed;
	}
	return EVisibility::Collapsed;
}

FText SStateTreeViewRow::GetTasksDesc() const
{
	const UStateTreeState* State = WeakState.Get();
	if (!State)
	{
		return FText::GetEmpty();
	}

	TArray<FText> Names;
	for (int32 i = 0; i < State->Tasks.Num(); i++)
	{
		if (const FStateTreeTaskBase* Task = State->Tasks[i].Node.GetPtr<FStateTreeTaskBase>())
		{
			Names.Add(FText::FromName(Task->Name));
		}
	}

	return FText::Join((FText::FromString(TEXT(" & "))), Names);
}

bool SStateTreeViewRow::HasParentTransitionForEvent(const UStateTreeState& State, const EStateTreeTransitionEvent Event) const
{
	EStateTreeTransitionEvent CombinedEvents = EStateTreeTransitionEvent::None;
	for (const UStateTreeState* ParentState = State.Parent; ParentState != nullptr; ParentState = ParentState->Parent)
	{
		for (const FStateTreeTransition& Transition : ParentState->Transitions)
		{
			CombinedEvents |= Transition.Event;
		}
	}
	return EnumHasAllFlags(CombinedEvents, Event);
}

FText SStateTreeViewRow::GetTransitionsDesc(const UStateTreeState& State, const EStateTreeTransitionEvent Event) const
{
	TArray<FText> DescItems;
	for (const FStateTreeTransition& Transition : State.Transitions)
	{
		if (Transition.Event == Event)
		{
			switch (Transition.State.Type)
			{
			case EStateTreeTransitionType::NotSet:
				DescItems.Add(LOCTEXT("TransitionNoneStyled", "[None]"));
				break;
			case EStateTreeTransitionType::Succeeded:
				DescItems.Add(LOCTEXT("TransitionTreeSucceededStyled", "[Succeeded]"));
				break;
			case EStateTreeTransitionType::Failed:
				DescItems.Add(LOCTEXT("TransitionTreeFailedStyled", "[Failed]"));
				break;
			case EStateTreeTransitionType::NextState:
				DescItems.Add(LOCTEXT("TransitionNextStateStyled", "[Next]"));
				break;
			case EStateTreeTransitionType::GotoState:
				DescItems.Add(FText::FromName(Transition.State.Name));
				break;
			default:
				ensureMsgf(false, TEXT("Unhandled transition type."));
				break;
			}
		}
	}

	if (State.Children.Num() == 0
		&& DescItems.Num() == 0
		&& Event != EStateTreeTransitionEvent::OnCondition)
	{
		if (HasParentTransitionForEvent(State, Event))
		{
			DescItems.Add(LOCTEXT("TransitionActionHandleInParentStyled", "[Parent]"));
		}
		else
		{
			DescItems.Add(LOCTEXT("TransitionActionMissingTransition", "Missing Transition"));
		}
	}
	
	return FText::Join(FText::FromString(TEXT(", ")), DescItems);
}

FText SStateTreeViewRow::GetTransitionsIcon(const UStateTreeState& State, const EStateTreeTransitionEvent Event) const
{
	enum EIconType
	{
		IconNone = 0,
        IconRightArrow =	1 << 0,
        IconDownArrow =		1 << 1,
        IconLevelUp =		1 << 2,
        IconWarning =		1 << 3,
    };
	uint8 IconType = IconNone;
	
	for (const FStateTreeTransition& Transition : State.Transitions)
	{
		// The icons here depict "transition direction", not the type specifically.
		if (Transition.Event == Event)
		{
			switch (Transition.State.Type)
			{
			case EStateTreeTransitionType::NotSet:
				IconType |= IconRightArrow;
				break;
			case EStateTreeTransitionType::Succeeded:
				IconType |= IconRightArrow;
				break;
			case EStateTreeTransitionType::Failed:
				IconType |= IconRightArrow;
				break;
			case EStateTreeTransitionType::NextState:
				IconType |= IconDownArrow;
				break;
			case EStateTreeTransitionType::GotoState:
				IconType |= IconRightArrow;
				break;
			default:
				ensureMsgf(false, TEXT("Unhandled transition type."));
				break;
			}
		}
	}

	if (FMath::CountBits(static_cast<uint64>(IconType)) > 1)
	{
		// Prune down to just one icon.
		IconType = IconRightArrow;
	}
	
	if (State.Children.Num() == 0
        && IconType == IconNone
        && Event != EStateTreeTransitionEvent::OnCondition)
	{
		if (HasParentTransitionForEvent(State, Event))
		{
			IconType = IconLevelUp;
		}
		else
		{
			IconType = IconWarning;
		}
	}

	switch (IconType)
	{
		case IconRightArrow:
			return FEditorFontGlyphs::Long_Arrow_Right;
		case IconDownArrow:
			return FEditorFontGlyphs::Long_Arrow_Down;
		case IconLevelUp:
			return FEditorFontGlyphs::Level_Up;
		case IconWarning:
			return FEditorFontGlyphs::Exclamation_Triangle;
		default:
			return FText::GetEmpty();
	}

	return FText::GetEmpty();
}

EVisibility SStateTreeViewRow::GetTransitionsVisibility(const UStateTreeState& State, const EStateTreeTransitionEvent Event) const
{
	TStaticArray<int32, 5> Counts;
	Algo::ForEach(Counts, [](int32& Count) { Count = 0; }); 

	for (const FStateTreeTransition& Transition : State.Transitions)
	{
		Counts[static_cast<uint8>(Transition.Event)]++;
	}

	if (State.Children.Num() == 0
	    && Counts[static_cast<uint8>(Event)] == 0
	    && Event != EStateTreeTransitionEvent::OnCondition)
	{
		// Find the missing transition type, note: Completed = Succeeded|Failed.
		EStateTreeTransitionEvent HandledEvents = EStateTreeTransitionEvent::None;
		HandledEvents |= Counts[static_cast<uint8>(EStateTreeTransitionEvent::OnCompleted)] > 0 ? EStateTreeTransitionEvent::OnCompleted : EStateTreeTransitionEvent::None;
		HandledEvents |= Counts[static_cast<uint8>(EStateTreeTransitionEvent::OnSucceeded)] > 0 ? EStateTreeTransitionEvent::OnSucceeded : EStateTreeTransitionEvent::None;
		HandledEvents |= Counts[static_cast<uint8>(EStateTreeTransitionEvent::OnFailed)] > 0 ? EStateTreeTransitionEvent::OnFailed : EStateTreeTransitionEvent::None;
		const EStateTreeTransitionEvent MissingEvent = HandledEvents ^ EStateTreeTransitionEvent::OnCompleted;
		
		return MissingEvent == Event ? EVisibility::Visible : EVisibility::Collapsed;
	}
	
	return Counts[static_cast<uint8>(Event)] > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SStateTreeViewRow::GetCompletedTransitionVisibility() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return GetTransitionsVisibility(*State, EStateTreeTransitionEvent::OnCompleted);
	}
	return EVisibility::Visible;
}

FText SStateTreeViewRow::GetCompletedTransitionsDesc() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return GetTransitionsDesc(*State, EStateTreeTransitionEvent::OnCompleted);
	}
	return LOCTEXT("Invalid", "Invalid");
}

FText SStateTreeViewRow::GetCompletedTransitionsIcon() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return GetTransitionsIcon(*State, EStateTreeTransitionEvent::OnCompleted);
	}
	return FText::GetEmpty();
}

EVisibility SStateTreeViewRow::GetSucceededTransitionVisibility() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return GetTransitionsVisibility(*State, EStateTreeTransitionEvent::OnSucceeded);
	}
	return EVisibility::Collapsed;
}

FText SStateTreeViewRow::GetSucceededTransitionDesc() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return GetTransitionsDesc(*State, EStateTreeTransitionEvent::OnSucceeded);
	}
	return FText::GetEmpty();
}

FText SStateTreeViewRow::GetSucceededTransitionIcon() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return GetTransitionsIcon(*State, EStateTreeTransitionEvent::OnSucceeded);
	}
	return FText::GetEmpty();
}

EVisibility SStateTreeViewRow::GetFailedTransitionVisibility() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return GetTransitionsVisibility(*State, EStateTreeTransitionEvent::OnFailed);
	}
	return EVisibility::Collapsed;
}

FText SStateTreeViewRow::GetFailedTransitionDesc() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return GetTransitionsDesc(*State, EStateTreeTransitionEvent::OnFailed);
	}
	return LOCTEXT("Invalid", "Invalid");
}

FText SStateTreeViewRow::GetFailedTransitionIcon() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return GetTransitionsIcon(*State, EStateTreeTransitionEvent::OnFailed);
	}
	return FEditorFontGlyphs::Ban;
}

EVisibility SStateTreeViewRow::GetConditionalTransitionsVisibility() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		return GetTransitionsVisibility(*State, EStateTreeTransitionEvent::OnCondition);
	}
	return EVisibility::Collapsed;
}

FText SStateTreeViewRow::GetConditionalTransitionsDesc() const
{
	TArray<FText> DescItems;
	if (const UStateTreeState* State = WeakState.Get())
	{
		return GetTransitionsDesc(*State, EStateTreeTransitionEvent::OnCondition);
	}
	return FText::Join(FText::FromString(TEXT(", ")), DescItems);
}

bool SStateTreeViewRow::IsRoutine() const
{
	// Routines can be identified by not having parent state.
	const UStateTreeState* State = WeakState.Get();
	return State ? State->Parent == nullptr : false;
}

bool SStateTreeViewRow::IsSelected() const
{
	if (const UStateTreeState* State = WeakState.Get())
	{
		if (StateTreeViewModel)
		{
			return StateTreeViewModel->IsSelected(State);
		}
	}
	return false;
}


bool SStateTreeViewRow::VerifyNodeTextChanged(const FText& NewLabel, FText& OutErrorMessage)
{
	return !NewLabel.IsEmptyOrWhitespace();
}

void SStateTreeViewRow::HandleNodeLabelTextCommitted(const FText& NewLabel, ETextCommit::Type CommitType)
{
	if (StateTreeViewModel)
	{
		if (UStateTreeState* State = WeakState.Get())
		{
			StateTreeViewModel->RenameState(State, FName(*FText::TrimPrecedingAndTrailing(NewLabel).ToString()));
		}
	}
}


FReply SStateTreeViewRow::HandleDragDetected(const FGeometry&, const FPointerEvent&)
{
	return FReply::Handled().BeginDragDrop(FActionTreeViewDragDrop::New(WeakState.Get()));
}

TOptional<EItemDropZone> SStateTreeViewRow::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, UStateTreeState* TargetState)
{
	TSharedPtr<FActionTreeViewDragDrop> DragDropOperation = DragDropEvent.GetOperationAs<FActionTreeViewDragDrop>();
	if (DragDropOperation.IsValid())
	{
		// Cannot drop on selection or child of selection.
		if (StateTreeViewModel && StateTreeViewModel->IsChildOfSelection(TargetState))
		{
			return TOptional<EItemDropZone>();
		}

		return DropZone;
	}

	return TOptional<EItemDropZone>();
}

FReply SStateTreeViewRow::HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, UStateTreeState* TargetState)
{
	TSharedPtr<FActionTreeViewDragDrop> DragDropOperation = DragDropEvent.GetOperationAs<FActionTreeViewDragDrop>();
	if (DragDropOperation.IsValid())
	{
		if (StateTreeViewModel)
		{
			if (DropZone == EItemDropZone::AboveItem)
			{
				StateTreeViewModel->MoveSelectedStatesBefore(TargetState);
			}
			else if (DropZone == EItemDropZone::BelowItem)
			{
				StateTreeViewModel->MoveSelectedStatesAfter(TargetState);
			}
			else
			{
				StateTreeViewModel->MoveSelectedStatesInto(TargetState);
			}

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
