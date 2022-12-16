// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleFaderGroup.h"

#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleManager.h"
#include "DMXControlConsoleSelection.h"
#include "Style/DMXControlConsoleStyle.h"
#include "Views/SDMXControlConsoleFaderGroupView.h"
#include "Widgets/SDMXControlConsoleAddButton.h"

#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleFaderGroup"

void SDMXControlConsoleFaderGroup::Construct(const FArguments& InArgs, const TWeakPtr<SDMXControlConsoleFaderGroupView>& InFaderGroupView)
{
	FaderGroupView = InFaderGroupView;

	if (!ensureMsgf(FaderGroupView.IsValid(), TEXT("Invalid fader group view, cannot create fader group widget correctly.")))
	{
		return;
	}

	OnAddFaderGroup = InArgs._OnAddFaderGroup;
	OnAddFaderGroupRow = InArgs._OnAddFaderGroupRow;
	OnExpanded = InArgs._OnExpanded;
	OnDeleted = InArgs._OnDeleted;
	OnSelected = InArgs._OnSelected;

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(120.f)
		.HeightOverride(300.f)
		[
			SNew(SBorder)
			.BorderBackgroundColor(this, &SDMXControlConsoleFaderGroup::GetFaderGroupBorderColor)
			.BorderImage(FDMXControlConsoleStyle::Get().GetBrush("DMXControlConsole.WhiteBrush"))
			[
				SNew(SBorder)
				.BorderImage(this, &SDMXControlConsoleFaderGroup::GetFaderGroupBorderImage)
				[
					SNew(SVerticalBox)

					//Top interface
					+ SVerticalBox::Slot()
					.FillHeight(.1f)
					.Padding(2.f, 2.f, 2.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(.8f)
						[
							SAssignNew(FaderGroupNameTextBox, SEditableTextBox)
							.Text(this, &SDMXControlConsoleFaderGroup::OnGetFaderGroupNameText)
							.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
							.OnTextCommitted(this, &SDMXControlConsoleFaderGroup::OnFaderGroupNameCommitted)
						]
						+ SHorizontalBox::Slot()
						.FillWidth(.2f)
						[
							GenerateExpanderArrow()
						]
					]

					//Add slot button
					+ SVerticalBox::Slot()
					.FillHeight(.6f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(.8f)
						[
							SNew(SBox)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(25.f)
							.HeightOverride(25.f)
							.Padding(5.f)
							[
								SNew(SDMXControlConsoleAddButton)
								.OnClicked(OnAddFaderGroup)
							]
						]
					]

					//Add row button
					+ SVerticalBox::Slot()
					.HAlign(HAlign_Center)
					.AutoHeight()
					[
						SNew(SBox)
						.WidthOverride(25.f)
						.HeightOverride(25.f)
						.Padding(5.f)
						[
							SNew(SDMXControlConsoleAddButton)
							.OnClicked(OnAddFaderGroupRow)
							.Visibility(TAttribute<EVisibility>::CreateSP(this, &SDMXControlConsoleFaderGroup::GetAddRowButtonVisibility))
						]
					]
				]
			]
		]
	];
}

FReply SDMXControlConsoleFaderGroup::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsSelected() && InKeyEvent.GetKey() == EKeys::Delete)
	{
		OnDeleted.ExecuteIfBound();

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SDMXControlConsoleFaderGroup::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
		const bool bAllowMultiselect = MouseEvent.IsControlDown() || MouseEvent.IsLeftShiftDown();
		SelectionHandler->SetAllowMultiselect(bAllowMultiselect);

		OnSelected.ExecuteIfBound();
	}

	return FReply::Handled();
}

bool SDMXControlConsoleFaderGroup::IsSelected() const
{
	if (!FaderGroupView.IsValid())
	{
		return false;
	}

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	return SelectionHandler->IsSelected(FaderGroupView.Pin()->GetFaderGroup());
}

TSharedRef<SButton> SDMXControlConsoleFaderGroup::GenerateExpanderArrow()
{
	SAssignNew(ExpanderArrow, SButton)
		.ButtonStyle(FCoreStyle::Get(), "NoBorder")
		.ClickMethod(EButtonClickMethod::MouseDown)
		.ContentPadding(0.f)
		.ForegroundColor(FSlateColor::UseForeground())
		.IsFocusable(false)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.OnClicked(OnExpanded)
		[
			SNew(SImage)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Image(this, &SDMXControlConsoleFaderGroup::GetExpanderImage)
		];

	return ExpanderArrow.ToSharedRef();
}

FText SDMXControlConsoleFaderGroup::OnGetFaderGroupNameText() const
{
	return FaderGroupView.IsValid() ? FText::FromString(FaderGroupView.Pin()->GetFaderGroupName()) : FText::GetEmpty();
}

void SDMXControlConsoleFaderGroup::OnFaderGroupNameCommitted(const FText& NewName, ETextCommit::Type InCommit)
{
	if (!ensureMsgf(FaderGroupView.IsValid(), TEXT("Invalid fader group view, cannot update fader group name correctly.")))
	{
		return;
	}

	if (!FaderGroupNameTextBox.IsValid())
	{
		return;
	}

	UDMXControlConsoleFaderGroup* FaderGroup = FaderGroupView.Pin()->GetFaderGroup();

	const FScopedTransaction FaderGroupTransaction(LOCTEXT("FaderGroupTransaction", "Edit Fader Group Name"));
	FaderGroup->PreEditChange(UDMXControlConsoleFaderGroup::StaticClass()->FindPropertyByName(UDMXControlConsoleFaderGroup::GetFaderGroupNamePropertyName()));
	FaderGroup->SetFaderGroupName(NewName.ToString());
	FaderGroup->PostEditChange();

	FaderGroupNameTextBox->SetText(FText::FromString(FaderGroup->GetFaderGroupName()));
}

EVisibility SDMXControlConsoleFaderGroup::GetAddRowButtonVisibility() const
{
	if (!FaderGroupView.IsValid())
	{
		return EVisibility::Collapsed;
	}

	const int32 Index = FaderGroupView.Pin()->GetIndex();
	return Index == 0 ? EVisibility::Visible : EVisibility::Hidden;
}

const FSlateBrush* SDMXControlConsoleFaderGroup::GetExpanderImage() const
{
	if (!FaderGroupView.IsValid())
	{
		return nullptr;
	}

	FName ResourceName;
	if (FaderGroupView.Pin()->IsExpanded())
	{
		if (ExpanderArrow->IsHovered())
		{
			constexpr TCHAR ExpandedHoveredName[] = TEXT("TreeArrow_Collapsed_Hovered");
			ResourceName = ExpandedHoveredName;
		}
		else
		{
			constexpr TCHAR ExpandedName[] = TEXT("TreeArrow_Collapsed");
			ResourceName = ExpandedName;
		}
	}
	else
	{
		if (ExpanderArrow->IsHovered())
		{
			constexpr TCHAR CollapsedHoveredName[] = TEXT("TreeArrow_Expanded_Hovered");
			ResourceName = CollapsedHoveredName;
		}
		else
		{
			constexpr TCHAR CollapsedName[] = TEXT("TreeArrow_Expanded");
			ResourceName = CollapsedName;
		}
	}

	return FCoreStyle::Get().GetBrush(ResourceName);
}

FSlateColor SDMXControlConsoleFaderGroup::GetFaderGroupBorderColor() const
{
	const UDMXControlConsoleFaderGroup* FaderGroup = FaderGroupView.IsValid() ? FaderGroupView.Pin()->GetFaderGroup() : nullptr;
	if (!FaderGroup)
	{
		return FLinearColor::White;
	}

	return FaderGroup->GetEditorColor();
}

const FSlateBrush* SDMXControlConsoleFaderGroup::GetFaderGroupBorderImage() const
{
	if (IsHovered())
	{
		if (IsSelected())
		{
			return FDMXControlConsoleStyle::Get().GetBrush("DMXControlConsole.FaderGroup_Highlighted");
		}
		else
		{
			return FDMXControlConsoleStyle::Get().GetBrush("DMXControlConsole.FaderGroup_Hovered");
		}
	}
	else
	{
		if (IsSelected())
		{
			return FDMXControlConsoleStyle::Get().GetBrush("DMXControlConsole.FaderGroup_Selected");
		}
		else
		{
			return FDMXControlConsoleStyle::Get().GetBrush("DMXControlConsole.BlackBrush");
		}
	}
}

#undef LOCTEXT_NAMESPACE
