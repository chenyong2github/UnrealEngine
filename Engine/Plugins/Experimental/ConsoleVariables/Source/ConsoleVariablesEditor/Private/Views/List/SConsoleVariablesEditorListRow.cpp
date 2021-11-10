// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConsoleVariablesEditorListRow.h"

#include "ConsoleVariablesEditorListRow.h"
#include "ConsoleVariablesEditorStyle.h"

#include "EditorStyleSet.h"
#include "SConsoleVariablesEditorListValueInput.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

void SConsoleVariablesEditorListRow::Construct(const FArguments& InArgs, const TWeakPtr<FConsoleVariablesEditorListRow> InRow, const FConsoleVariablesEditorListSplitterManagerPtr& InSplitterManagerPtr)
{
	check(InRow.IsValid());

	Item = InRow;

	FConsoleVariablesEditorListRowPtr PinnedItem = Item.Pin();
	
	SplitterManagerPtr = InSplitterManagerPtr;
	check(SplitterManagerPtr.IsValid());
	
	const FConsoleVariablesEditorListRow::EConsoleVariablesEditorListRowType RowType = PinnedItem->GetRowType();
	const bool bIsHeaderRow = RowType == FConsoleVariablesEditorListRow::HeaderRow;

	const FText DisplayText = FText::FromString(PinnedItem->GetCommandInfo().Pin()->Command);

	// For grouping row support
	const bool bDoesRowNeedSplitter = true;

	int32 IndentationDepth = 0;
	TWeakPtr<FConsoleVariablesEditorListRow> ParentRow = PinnedItem->GetDirectParentRow();
	while (ParentRow.IsValid())
	{
		IndentationDepth++;
		ParentRow = ParentRow.Pin()->GetDirectParentRow();
	}
	PinnedItem->SetChildDepth(IndentationDepth);

	ChildSlot
	[
		SNew(SBox)
		.Padding(FMargin(5,2))
		[
			SAssignNew(BorderPtr, SBorder)
			.Padding(FMargin(0, 5))
			.ToolTipText(FText::FromString(PinnedItem->GetCommandInfo().Pin()->ConsoleVariablePtr ?
				FString(PinnedItem->GetCommandInfo().Pin()->ConsoleVariablePtr->GetHelp()) : ""))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.BorderImage_Lambda([RowType]()
			{
				switch (RowType)
				{							
					case FConsoleVariablesEditorListRow::CommandGroup:
						return FConsoleVariablesEditorStyle::Get().GetBrush("ConsoleVariablesEditor.CommandGroupBorder");

					case FConsoleVariablesEditorListRow::HeaderRow:
						return FConsoleVariablesEditorStyle::Get().GetBrush("ConsoleVariablesEditor.HeaderRowBorder");

					default:
						return FConsoleVariablesEditorStyle::Get().GetBrush("ConsoleVariablesEditor.DefaultBorder");
				}
			})
		]
	];

	// Create name and checkbox

	const TSharedRef<SHorizontalBox> BasicRowWidgets = SNew(SHorizontalBox);

	BasicRowWidgets->AddSlot()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Left)
	.AutoWidth()
	.Padding(5.f, 2.f)
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([this] ()
		{
			if (ensure(Item.IsValid()))
			{
				return Item.Pin()->GetWidgetCheckedState();
			}

			return ECheckBoxState::Checked;
		})
		.OnCheckStateChanged_Lambda([this] (const ECheckBoxState NewStats)
		{
			if (TSharedPtr<FConsoleVariablesEditorListRow> PinnedItem = Item.Pin())  
			{
				if (PinnedItem->GetRowType() == FConsoleVariablesEditorListRow::SingleCommand && ValueChildInputWidget.IsValid())
				{
					
					PinnedItem->SetWidgetCheckedState(NewStats, true);
					
					if (PinnedItem->IsRowChecked())
					{
						PinnedItem->GetCommandInfo().Pin()->ExecuteCommand(ValueChildInputWidget->GetCachedValue());
					}
					else
					{
						PinnedItem->GetCommandInfo().Pin()->ExecuteCommand(PinnedItem->GetCommandInfo().Pin()->StartupValueAsString);
					}
				}
			}
		})
	];

	BasicRowWidgets->AddSlot()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Left)
	[
		SNew(STextBlock)
		.Text(bIsHeaderRow ? LOCTEXT("ConsoleVariablesEditorList_ConsoleVariableName", "Console Variable Name") : DisplayText)
	];

	// Create value widgets
	
	if (bDoesRowNeedSplitter)
	{
		SAssignNew(OuterSplitterPtr, SSplitter)
		.PhysicalSplitterHandleSize(5.0f)
		.HitDetectionSplitterHandleSize(5.0f);

		OuterSplitterPtr->AddSlot()
		.SizeRule(SSplitter::ESizeRule::FractionOfParent)
		.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this] (float InWidth) {}))
		.Value(TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SConsoleVariablesEditorListRow::GetNameColumnSize)))
		[
			BasicRowWidgets
		];

		OuterSplitterPtr->AddSlot()
		.OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SConsoleVariablesEditorListRow::SetNestedColumnSize))
		.Value(TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SConsoleVariablesEditorListRow::CalculateAndReturnNestedColumnSize)))
		[
			SAssignNew(NestedSplitterPtr, SSplitter)
			.PhysicalSplitterHandleSize(5.0f)
			.HitDetectionSplitterHandleSize(5.0f)
		];
	
		// Nested Splitter Slot 0

		TSharedPtr<SWidget> ValueChildWidget;

		if (bIsHeaderRow)
		{
			ValueChildWidget = 
				SNew(STextBlock)
				.Text(LOCTEXT("ConsoleVariablesEditorList_ConsoleVariableValueHeaderText", "Value"));
		}
		else
		{
			if (PinnedItem->GetCommandInfo().IsValid() && PinnedItem->GetCommandInfo().Pin()->ConsoleVariablePtr)
			{
				ValueChildWidget = ValueChildInputWidget = SConsoleVariablesEditorListValueInput::GetInputWidget(Item);
			}
		}

		const TSharedRef<SHorizontalBox> FinalValueWidget = SNew(SHorizontalBox);

		FinalValueWidget->AddSlot()
			.VAlign(VAlign_Center)
			.Padding(FMargin(2, 0))
			[
				ValueChildWidget.ToSharedRef()
			];

		FinalValueWidget->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(2, 0))
			[
				SNew(SButton)
				.IsFocusable(false)
				.ToolTipText(
					LOCTEXT("ResetRowValueTooltipText", "Reset this value to what is defined in the preset or what it was when the engine started."))
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
				.ContentPadding(0)
				.Visibility_Lambda([this]()
				{
					if (Item.Pin()->GetRowType() == FConsoleVariablesEditorListRow::SingleCommand)
					{
						return Item.Pin()->GetCommandInfo().Pin()->IsCurrentValueDifferentFromInputValue(Item.Pin()->GetPresetValue()) ?
							EVisibility::Visible : EVisibility::Hidden;
					}

					return EVisibility::Collapsed;
				})
				.OnClicked_Lambda([this]()
				{
					Item.Pin()->ResetToPresetValue();

					return FReply::Handled();
				})
				.Content()
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("PropertyWindow.DiffersFromDefault"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];

		NestedSplitterPtr->AddSlot()
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this] (float InWidth) {}))
			.Value(TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SConsoleVariablesEditorListRow::GetValueColumnSize)))
			[
				FinalValueWidget
			];

		// Splitter Slot 1
		
		TSharedPtr<SWidget> SourceWidget;

		if (bIsHeaderRow)
		{
			SourceWidget = 
				SNew(STextBlock)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.Text(LOCTEXT("ConsoleVariablesEditorList_SourceHeaderText", "Source"));
		}
		else
		{
			SourceWidget =
				SNew(SOverlay)
				.Visibility(EVisibility::SelfHitTestInvisible)

				+SOverlay::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return Item.Pin()->GetCommandInfo().Pin()->GetSource();
					})
				]

				+SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SAssignNew(HoverableWidgetsPtr, SConsoleVariablesEditorListRowHoverWidgets, Item)
					.Visibility(EVisibility::Collapsed)
				];
		}

		const TSharedRef<SWidget> FinalSourceWidget = SNew(SBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2, 0))
			[
				SourceWidget.ToSharedRef()
			];

		NestedSplitterPtr->AddSlot()
		.OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SConsoleVariablesEditorListRow::SetSourceColumnSize))
		.Value(TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SConsoleVariablesEditorListRow::GetSourceColumnSize)))
		[
			FinalSourceWidget
		];
		
		BorderPtr->SetContent(OuterSplitterPtr.ToSharedRef());
	}
	else
	{
		// Unreachable right now, left in for future grouping row support
		BorderPtr->SetContent(BasicRowWidgets);
	}

	if (PinnedItem->GetShouldFlashOnScrollIntoView())
	{
		FlashRow();

		PinnedItem->SetShouldFlashOnScrollIntoView(false);
	}
}

void SConsoleVariablesEditorListRow::FlashRow() const
{
	FLinearColor Color = BorderPtr->GetColorAndOpacity();
	BorderPtr->SetColorAndOpacity(FLinearColor::White);
	BorderPtr->SetColorAndOpacity(Color);
}

void SConsoleVariablesEditorListRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	if (HoverableWidgetsPtr.IsValid())
	{
		HoverableWidgetsPtr->SetVisibility(EVisibility::Visible);
	}
}

void SConsoleVariablesEditorListRow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	if (HoverableWidgetsPtr.IsValid())
	{
		HoverableWidgetsPtr->SetVisibility(EVisibility::Collapsed);
	}
}

SConsoleVariablesEditorListRow::~SConsoleVariablesEditorListRow()
{
	// Remove delegate bindings

	// Unbind event to the splitter being resized first
	if (NestedSplitterPtr.IsValid())
	{
		for (int32 SplitterSlotCount = 0; SplitterSlotCount < NestedSplitterPtr->GetChildren()->Num(); SplitterSlotCount++)
		{
			NestedSplitterPtr->SlotAt(SplitterSlotCount).OnSlotResized().Unbind();
		}
	}

	if (OuterSplitterPtr.IsValid())
	{
		for (int32 SplitterSlotCount = 0; SplitterSlotCount < OuterSplitterPtr->GetChildren()->Num(); SplitterSlotCount++)
		{
			OuterSplitterPtr->SlotAt(SplitterSlotCount).OnSlotResized().Unbind();
		}
	}

	OuterSplitterPtr.Reset();
	NestedSplitterPtr.Reset();
	SplitterManagerPtr.Reset();
}

float SConsoleVariablesEditorListRow::GetNameColumnSize() const
{
	return 1.0f - CachedNestedColumnWidthAdjusted;
}

float SConsoleVariablesEditorListRow::CalculateAndReturnNestedColumnSize()
{
	check(Item.IsValid());
	
	const TSharedPtr<FConsoleVariablesEditorListRow>& PinnedItem = Item.Pin();
	const uint8 ChildDepth = PinnedItem->GetChildDepth();
	const float StartWidth = SplitterManagerPtr->NestedColumnWidth;

	if (ChildDepth > 0)
	{
		constexpr float LocalPixelOffset = 10.0f;
		const float LocalPixelDifference = LocalPixelOffset * ChildDepth;
		const float LocalSizeX = GetTickSpaceGeometry().GetLocalSize().X;
		const float NestedItemCoefficient = (LocalSizeX + LocalPixelDifference) / LocalSizeX;
		
		CachedNestedColumnWidthAdjusted = StartWidth * NestedItemCoefficient;
		
		return CachedNestedColumnWidthAdjusted;
	}

	return CachedNestedColumnWidthAdjusted = StartWidth;
}

float SConsoleVariablesEditorListRow::GetSourceColumnSize() const
{
	const float EndWidth = SplitterManagerPtr->SnapshotPropertyColumnWidth;
	return EndWidth;
}

float SConsoleVariablesEditorListRow::GetValueColumnSize() const
{
	return 1.0f - GetSourceColumnSize();
}

void SConsoleVariablesEditorListRow::SetNestedColumnSize(const float InWidth) const
{
	SplitterManagerPtr->NestedColumnWidth = InWidth;
}

void SConsoleVariablesEditorListRow::SetSourceColumnSize(const float InWidth) const
{
	SplitterManagerPtr->SnapshotPropertyColumnWidth = InWidth;
}

void SConsoleVariablesEditorListRowHoverWidgets::Construct(const FArguments& InArgs, const TWeakPtr<FConsoleVariablesEditorListRow> InRow)
{
	check(InRow.IsValid());

	Item = InRow;

	ChildSlot
	[
		// Remove Button
		SAssignNew(RemoveButtonPtr, SButton)
		.ButtonColorAndOpacity(FStyleColors::Transparent)
		.OnClicked_Lambda([this]()
		{
			return Item.Pin()->OnRemoveButtonClicked();
		})
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.Delete"))
			.ColorAndOpacity(FSlateColor::UseForeground())
		]
	];
}

void SConsoleVariablesEditorListRowHoverWidgets::OnMouseEnter(const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	RemoveButtonPtr->SetBorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.4f));
}

void SConsoleVariablesEditorListRowHoverWidgets::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	RemoveButtonPtr->SetBorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
}

SConsoleVariablesEditorListRowHoverWidgets::~SConsoleVariablesEditorListRowHoverWidgets()
{
	RemoveButtonPtr.Reset();

	Item.Reset();
}

#undef LOCTEXT_NAMESPACE
