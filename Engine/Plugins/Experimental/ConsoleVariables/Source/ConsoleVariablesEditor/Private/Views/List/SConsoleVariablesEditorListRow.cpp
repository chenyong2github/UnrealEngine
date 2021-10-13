// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConsoleVariablesEditorListRow.h"

#include "ConsoleVariablesEditorListRow.h"
#include "ConsoleVariablesEditorStyle.h"

#include "EditorStyleSet.h"
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

void SConsoleVariablesEditorListRow::Construct(const FArguments& InArgs, const TWeakPtr<FConsoleVariablesEditorListRow> InRow, const FConsoleVariablesEditorListSplitterManagerPtr& InSplitterManagerPtr)
{	
	check(InRow.IsValid());

	Item = InRow;

	FConsoleVariablesEditorListRowPtr PinnedItem = Item.Pin();
	
	SplitterManagerPtr = InSplitterManagerPtr;
	check(SplitterManagerPtr.IsValid());
	
	const FConsoleVariablesEditorListRow::EConsoleVariablesEditorListRowType RowType = PinnedItem->GetRowType();
	const FText DisplayText = FText::FromString(PinnedItem->GetCommandInfo().Command);

	const bool bIsHeaderRow = RowType == FConsoleVariablesEditorListRow::HeaderRow;

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

	TSharedPtr<SBorder> BorderPtr;

	ChildSlot
	[
		SNew(SBox)
		.Padding(FMargin(5,2))
		[
			SAssignNew(BorderPtr, SBorder)
			.Padding(FMargin(0, 5))
			.ToolTipText(FText::FromString(PinnedItem->GetCommandInfo().HelpText))
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
			if (Item.IsValid())  
			{
				Item.Pin()->SetWidgetCheckedState(NewStats, true);
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
		.Style(FEditorStyle::Get(), "DetailsView.Splitter")
		.PhysicalSplitterHandleSize(5.0f)
		.HitDetectionSplitterHandleSize(5.0f);

		OuterSplitterPtr->AddSlot().SizeRule(SSplitter::ESizeRule::FractionOfParent)
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
			.Style(FEditorStyle::Get(), "DetailsView.Splitter")
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
			if (PinnedItem->GetCommandInfo().ValueType == EConsoleVariablesUiVariableType::ConsoleVariablesType_Float)
			{
				ValueChildWidget = SNew(SSpinBox<float>)
				.Value_Lambda([this]
				{
					if (Item.IsValid())
					{
						return FCString::Atof(*Item.Pin()->GetCommandInfo().ValueAsString);
					}

					return 0.f;
				})
				.OnValueChanged_Lambda([this] (const float InValue)
				{
					if (Item.IsValid())
					{
						Item.Pin()->GetCommandInfo().SetValue(InValue, true);
					}
				});
			}
			else if (PinnedItem->GetCommandInfo().ValueType == EConsoleVariablesUiVariableType::ConsoleVariablesType_Integer)
			{
				ValueChildWidget = SNew(SSpinBox<int32>)
				.Value_Lambda([this]
				{
					if (Item.IsValid())
					{
						return FCString::Atoi(*Item.Pin()->GetCommandInfo().ValueAsString);
					}

					return 0;
				})
				.OnValueChanged_Lambda([this] (const int32 InValue)
				{
					if (Item.IsValid())
					{
						Item.Pin()->GetCommandInfo().SetValue(InValue, true);
					}
				});
			}
			else if (PinnedItem->GetCommandInfo().ValueType == EConsoleVariablesUiVariableType::ConsoleVariablesType_Bool)
			{
				ValueChildWidget = SNew(SSpinBox<int32>)
				.Value_Lambda([this]
				{
					if (Item.IsValid())
					{
						return FCString::Atoi(*Item.Pin()->GetCommandInfo().ValueAsString);
					}

					return 0;
				})
				.MinSliderValue(0)
				.MaxSliderValue(2)
				.OnValueChanged_Lambda([this] (const int32 InValue)
				{
					if (Item.IsValid())
					{
						Item.Pin()->GetCommandInfo().SetValue(InValue, true);
					}
				});
			}
			else 
			{
				ValueChildWidget = SNew(SEditableText)
				.Text_Lambda([this]
				{
					if (Item.IsValid())
					{
						return FText::FromString(*Item.Pin()->GetCommandInfo().ValueAsString);
					}

					return FText::GetEmpty();
				})
				.OnTextCommitted_Lambda([this] (const FText& InValue, ETextCommit::Type InTextCommitType)
				{
					if (InTextCommitType == ETextCommit::OnEnter && Item.IsValid())
					{
						Item.Pin()->GetCommandInfo().SetValue(InValue.ToString(), true);
					}
				});;
			}
		}

		const TSharedRef<SWidget> FinalValueWidget = SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2, 0))
			[
				ValueChildWidget.ToSharedRef()
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
				.Text(LOCTEXT("ConsoleVariablesEditorList_ConsoleVariableSourceHeaderText", "Source"));
		}
		else
		{
			SourceWidget =
				SNew(SOverlay)

				+SOverlay::Slot()
				.HAlign(HAlign_Left)
				[
					SNew(STextBlock)
					.Text_Lambda([PinnedItem]()
					{
						return PinnedItem->GetSource();
					})
				]

				+SOverlay::Slot()
				.HAlign(HAlign_Right)
				[
					SAssignNew(HoverableWidgetsPtr, SConsoleVariablesEditorListRowHoverWidgets, Item)
					.Visibility(EVisibility::Collapsed)
				];
		}

		NestedSplitterPtr->AddSlot()
		.OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SConsoleVariablesEditorListRow::SetSourceColumnSize))
		.Value(TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SConsoleVariablesEditorListRow::GetSourceColumnSize)))
		[
			SourceWidget.ToSharedRef()
		];
		
		BorderPtr->SetContent(OuterSplitterPtr.ToSharedRef());
	}
	else
	{
		// Unreachable right now, left in for future grouping row support
		BorderPtr->SetContent(BasicRowWidgets);
	}
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
		.ButtonColorAndOpacity(FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f)))
		.OnClicked_Lambda([this]()
		{
			return Item.Pin()->OnRemoveButtonClicked();
		})
		[
			SNew(SImage)
			.Image(FEditorStyle::Get().GetBrush("Icons.Delete"))
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

