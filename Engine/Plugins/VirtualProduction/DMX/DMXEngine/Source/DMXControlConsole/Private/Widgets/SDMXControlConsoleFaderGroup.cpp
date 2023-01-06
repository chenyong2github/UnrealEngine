// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleFaderGroup.h"

#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleManager.h"
#include "DMXControlConsoleSelection.h"
#include "Library/DMXEntityFixturePatch.h"
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
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
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

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	SelectionHandler->GetOnFaderGroupSelectionChanged().AddSP(this, &SDMXControlConsoleFaderGroup::OnSelectionChanged);
}

FReply SDMXControlConsoleFaderGroup::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsSelected() && InKeyEvent.GetKey() == EKeys::Delete)
	{
		const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
		const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();

		const FScopedTransaction DeleteFaderGroupTransaction(LOCTEXT("DeleteFaderGroupTransaction", "Delete Fader Group"));

		if (SelectedFaderGroupsObjects.Num() > 1)
		{
			for (const TWeakObjectPtr<UObject>& SelectedFaderGroupObject : SelectedFaderGroupsObjects)
			{
				UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
				if (!SelectedFaderGroup)
				{
					continue;
				}

				SelectedFaderGroup->PreEditChange(nullptr);

				SelectedFaderGroup->Destroy();

				SelectedFaderGroup->PostEditChange();

				SelectionHandler->ClearFadersSelection(SelectedFaderGroup);
				SelectionHandler->RemoveFromSelection(SelectedFaderGroup);
			}
		}
		else
		{
			UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupsObjects[0]);
			if (SelectedFaderGroup)
			{
				SelectedFaderGroup->PreEditChange(nullptr);

				SelectionHandler->ReplaceInSelection(SelectedFaderGroup);
				SelectedFaderGroup->Destroy();

				SelectedFaderGroup->PostEditChange();
			}
		}

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SDMXControlConsoleFaderGroup::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		UDMXControlConsoleFaderGroup* FaderGroup = GetFaderGroup();
		if (FaderGroup)
		{
			const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
			const bool bWasInitiallySelected = IsSelected();

			if (!MouseEvent.IsLeftShiftDown())
			{
				if (!MouseEvent.IsControlDown())
				{
					SelectionHandler->ClearSelection();
				}

				if (!bWasInitiallySelected)
				{
					SelectionHandler->AddToSelection(FaderGroup);
					FaderGroupView.Pin()->ExpandFadersWidget();
				}
				else
				{
					SelectionHandler->RemoveFromSelection(FaderGroup);
				}
			}
			else 
			{
				SelectionHandler->Multiselect(FaderGroup);
			}
		}
	}

	return FReply::Handled();
}

UDMXControlConsoleFaderGroup* SDMXControlConsoleFaderGroup::GetFaderGroup() const
{
	return FaderGroupView.IsValid() ? FaderGroupView.Pin()->GetFaderGroup() : nullptr;
}

bool SDMXControlConsoleFaderGroup::IsSelected() const
{
	UDMXControlConsoleFaderGroup* FaderGroup = GetFaderGroup();
	if (!FaderGroup)
	{
		return false;
	}

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	return SelectionHandler->IsSelected(FaderGroup);
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

void SDMXControlConsoleFaderGroup::OnSelectionChanged(UDMXControlConsoleFaderGroup* InFaderGroup)
{
	if (!InFaderGroup)
	{
		return;
	}

	const UDMXControlConsoleFaderGroup* FaderGroup = GetFaderGroup();
	if (InFaderGroup != FaderGroup || !IsSelected())
	{
		return;
	}

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	if (SelectionHandler->GetSelectedFadersFromFaderGroup(InFaderGroup).IsEmpty())
	{
		FSlateApplication::Get().SetKeyboardFocus(AsShared());
	}
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
	const UDMXControlConsoleFaderGroup* FaderGroup = GetFaderGroup();
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
