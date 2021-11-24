// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConsoleVariablesEditorListRow.h"

#include "ConsoleVariablesEditorModule.h"
#include "ConsoleVariablesEditorStyle.h"
#include "SConsoleVariablesEditorListValueInput.h"

#include "Kismet/KismetMathLibrary.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

void SConsoleVariablesEditorListRow::Construct(
	const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, const TWeakPtr<FConsoleVariablesEditorListRow> InRow)
{
	check(InRow.IsValid());

	Item = InRow;
	const FConsoleVariablesEditorListRowPtr PinnedItem = Item.Pin();

	// Set up flash animation
	FlashAnimation = FCurveSequence(0.f, FlashAnimationDuration, ECurveEaseFunction::QuadInOut);

	SMultiColumnTableRow<FConsoleVariablesEditorListRowPtr>::Construct(
		FSuperRowType::FArguments()
		.Padding(1.0f),
		InOwnerTable
	);

	if (PinnedItem->GetShouldFlashOnScrollIntoView())
	{
		FlashRow();

		PinnedItem->SetShouldFlashOnScrollIntoView(false);
	}
}

TSharedRef<SWidget> SConsoleVariablesEditorListRow::GenerateWidgetForColumn(const FName& InColumnName)
{
	const FConsoleVariablesEditorListRowPtr PinnedItem = Item.Pin();

	const TSharedPtr<SWidget> CellWidget = GenerateCells(InColumnName, PinnedItem);

	const TSharedRef<SImage> FlashImage = SNew(SImage)
										.Image(new FSlateColorBrush(FStyleColors::White))
										.Visibility_Raw(this, &SConsoleVariablesEditorListRow::GetFlashImageVisibility)
										.ColorAndOpacity_Raw(this, &SConsoleVariablesEditorListRow::GetFlashImageColorAndOpacity);

	FlashImages.Add(FlashImage);

	return SNew(SBox)
			[
				SNew(SOverlay)

				+SOverlay::Slot()
				[
					FlashImage
				]

				+SOverlay::Slot()
				[
					SNew(SBorder)
					.ToolTipText(FText::FromString(PinnedItem->GetCommandInfo().Pin()->ConsoleVariablePtr ?
						FString(PinnedItem->GetCommandInfo().Pin()->ConsoleVariablePtr->GetHelp()) : ""))
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					.BorderImage(GetBorderImage(PinnedItem->GetRowType()))
					[
						CellWidget.ToSharedRef()
					]
				]
			];
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
	Item.Reset();

	FlashImages.Empty();

	ValueChildInputWidget.Reset();
	
	SplitterManagerPtr.Reset();
	
	HoverableWidgetsPtr.Reset();
}

void SConsoleVariablesEditorListRow::FlashRow()
{
	FlashAnimation.Play(this->AsShared());
}

EVisibility SConsoleVariablesEditorListRow::GetFlashImageVisibility() const
{
	return FlashAnimation.IsPlaying() ? EVisibility::SelfHitTestInvisible : EVisibility::Hidden;
}

FSlateColor SConsoleVariablesEditorListRow::GetFlashImageColorAndOpacity() const
{
	if (FlashAnimation.IsPlaying())
	{
		// This equation modulates the alpha into a parabolic curve 
		const float Progress = FMath::Abs(FMath::Abs((FlashAnimation.GetLerp() - 0.5f) * 2) - 1);
		return FLinearColor::LerpUsingHSV(FLinearColor::Transparent, FlashColor, Progress);
	}

	return FLinearColor::Transparent; 
}

const FSlateBrush* SConsoleVariablesEditorListRow::GetBorderImage(
	const FConsoleVariablesEditorListRow::EConsoleVariablesEditorListRowType InRowType)
{
	switch (InRowType)
	{							
	case FConsoleVariablesEditorListRow::CommandGroup:
		return FConsoleVariablesEditorStyle::Get().GetBrush("ConsoleVariablesEditor.CommandGroupBorder");

	case FConsoleVariablesEditorListRow::HeaderRow:
		return FConsoleVariablesEditorStyle::Get().GetBrush("ConsoleVariablesEditor.HeaderRowBorder");

	default:
		return FConsoleVariablesEditorStyle::Get().GetBrush("ConsoleVariablesEditor.DefaultBorder");
	}
}

TSharedRef<SWidget> SConsoleVariablesEditorListRow::GenerateCells(const FName& InColumnName, const TSharedPtr<FConsoleVariablesEditorListRow> PinnedItem)
{
	if (InColumnName.IsEqual(SConsoleVariablesEditorList::CheckBoxColumnName))
	{
		return SNew(SBox)
				.HAlign(HAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Raw(this, &SConsoleVariablesEditorListRow::GetCheckboxState)
					.OnCheckStateChanged_Raw(this, &SConsoleVariablesEditorListRow::OnCheckboxStateChange)
				];
	}
	if (InColumnName.IsEqual(SConsoleVariablesEditorList::VariableNameColumnName))
	{
		return  SNew(STextBlock)
				.Text(FText::FromString(PinnedItem->GetCommandInfo().Pin()->Command));
	}
	if (InColumnName.IsEqual(SConsoleVariablesEditorList::ValueColumnName))
	{
		return GenerateValueCellWidget(PinnedItem);
	}
	if (InColumnName.IsEqual(SConsoleVariablesEditorList::SourceColumnName))
	{
		return SNew(SOverlay)
				.Visibility(EVisibility::SelfHitTestInvisible)

				+SOverlay::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return Item.Pin()->GetCommandInfo().Pin()->GetSourceAsText();
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

	return SNullWidget::NullWidget;
}

ECheckBoxState SConsoleVariablesEditorListRow::GetCheckboxState() const
{
	if (ensure(Item.IsValid()))
	{
		return Item.Pin()->GetWidgetCheckedState();
	}

	return ECheckBoxState::Checked;
}

void SConsoleVariablesEditorListRow::OnCheckboxStateChange(const ECheckBoxState InNewState) const
{
	if (const TSharedPtr<FConsoleVariablesEditorListRow> PinnedItem = Item.Pin())  
	{
		PinnedItem->SetWidgetCheckedState(InNewState, true);
		
		if (PinnedItem->GetRowType() == FConsoleVariablesEditorListRow::SingleCommand && ValueChildInputWidget.IsValid())
		{									
			if (PinnedItem->IsRowChecked())
			{
				PinnedItem->GetCommandInfo().Pin()->ExecuteCommand(ValueChildInputWidget->GetCachedValue());

				FConsoleVariablesEditorModule::Get().SendMultiUserConsoleVariableChange(
					PinnedItem->GetCommandInfo().Pin()->Command, ValueChildInputWidget->GetCachedValue());
			}
			else
			{
				PinnedItem->GetCommandInfo().Pin()->ExecuteCommand(PinnedItem->GetCommandInfo().Pin()->StartupValueAsString);

				FConsoleVariablesEditorModule::Get().SendMultiUserConsoleVariableChange(
					PinnedItem->GetCommandInfo().Pin()->Command, PinnedItem->GetCommandInfo().Pin()->StartupValueAsString);

				PinnedItem->GetCommandInfo().Pin()->SetSourceFlag(PinnedItem->GetCommandInfo().Pin()->StartupSource);
			}
		}

		if (const TWeakPtr<SConsoleVariablesEditorList> ListView = PinnedItem->GetListViewPtr(); ListView.IsValid())
		{
			ListView.Pin()->OnListItemCheckBoxStateChange(InNewState);
		}
	}
}

TSharedRef<SWidget> SConsoleVariablesEditorListRow::GenerateValueCellWidget(const TSharedPtr<FConsoleVariablesEditorListRow> PinnedItem)
{
	if (PinnedItem->GetCommandInfo().IsValid() && PinnedItem->GetCommandInfo().Pin()->ConsoleVariablePtr)
	{
		const TSharedRef<SHorizontalBox> FinalValueWidget = SNew(SHorizontalBox);
		ValueChildInputWidget = SConsoleVariablesEditorListValueInput::GetInputWidget(Item);

		FinalValueWidget->AddSlot()
		.VAlign(VAlign_Center)
		.Padding(FMargin(2, 0))
		[
			ValueChildInputWidget.ToSharedRef()
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
				if (const TSharedPtr<FConsoleVariablesEditorListRow> PinnedItem = Item.Pin(); PinnedItem->GetRowType() == FConsoleVariablesEditorListRow::SingleCommand)
				{
					return PinnedItem->IsRowChecked() && Item.Pin()->GetCommandInfo().Pin()->IsCurrentValueDifferentFromInputValue(Item.Pin()->GetPresetValue()) ?
						EVisibility::Visible : EVisibility::Hidden;
				}

				return EVisibility::Collapsed;
			})
			.OnClicked_Lambda([this]()
			{
				Item.Pin()->ResetToPresetValue();

				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("PropertyWindow.DiffersFromDefault"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];

		return FinalValueWidget;
	}

	return SNullWidget::NullWidget;
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
