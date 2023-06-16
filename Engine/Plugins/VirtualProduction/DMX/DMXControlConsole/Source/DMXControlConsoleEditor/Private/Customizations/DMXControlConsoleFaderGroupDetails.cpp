// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleFaderGroupDetails.h"

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Algo/ForEach.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DMXControlConsole.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFaderGroupRow.h"
#include "IPropertyUtilities.h"
#include "Layout/Visibility.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Models/DMXControlConsoleEditorModel.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleFaderGroupDetails"

void FDMXControlConsoleFaderGroupDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	PropertyUtilities = InDetailLayout.GetPropertyUtilities();

	IDetailCategoryBuilder& FaderGroupCategory = InDetailLayout.EditCategory("DMX Fader Group", FText::GetEmpty());

	const TSharedPtr<IPropertyHandle> FaderGroupNameHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderGroup::GetFaderGroupNamePropertyName());
	InDetailLayout.HideProperty(FaderGroupNameHandle);
	const TSharedPtr<IPropertyHandle> EditorColorHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderGroup::GetEditorColorPropertyName());
	InDetailLayout.HideProperty(EditorColorHandle);
	const TSharedPtr<IPropertyHandle> IsMutedHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderGroup::GetIsMutedPropertyName());
	InDetailLayout.HideProperty(IsMutedHandle);
	
	FaderGroupCategory.AddProperty(FaderGroupNameHandle);
	FaderGroupCategory.AddProperty(EditorColorHandle)
		.Visibility(TAttribute<EVisibility>::CreateSP(this, &FDMXControlConsoleFaderGroupDetails::GetEditorColorVisibility));
	
	// Fixture Patch section
	FaderGroupCategory.AddCustomRow(FText::GetEmpty())
		.NameContent()
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.Text(LOCTEXT("FixturePatch", "Fixture Patch"))
		]
		.ValueContent()
		[
			SNew(SEditableTextBox)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.IsReadOnly(true)
			.Text(this, &FDMXControlConsoleFaderGroupDetails::GetFixturePatchText)
		];

	// Clear button section
	FaderGroupCategory.AddCustomRow(FText::GetEmpty())
		.Visibility(TAttribute<EVisibility>::CreateSP(this, &FDMXControlConsoleFaderGroupDetails::GetClearButtonVisibility))
		.WholeRowContent()
		[
			SNew(SBox)
			.Padding(5.f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.OnClicked(this, &FDMXControlConsoleFaderGroupDetails::OnClearButtonClicked)
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(LOCTEXT("ClearButtonTitle", "Clear"))
				]
			]
		];

	// Mute property section
	FaderGroupCategory.AddProperty(IsMutedHandle);

	// Lock CheckBox section
	FaderGroupCategory.AddCustomRow(FText::GetEmpty())
		.NameContent()
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.Text(LOCTEXT("FaderGroupLoxkCheckBox", "Is Locked"))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FDMXControlConsoleFaderGroupDetails::IsLockChecked)
			.OnCheckStateChanged(this, &FDMXControlConsoleFaderGroupDetails::OnLockToggleChanged)
		];
}

void FDMXControlConsoleFaderGroupDetails::ForceRefresh() const
{
	if (!PropertyUtilities.IsValid())
	{
		return;
	}
	
	PropertyUtilities->ForceRefresh();
}

bool FDMXControlConsoleFaderGroupDetails::DoSelectedFaderGroupsHaveAnyFixturePatches() const
{
	UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel->GetSelectionHandler();
	TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupObjects = SelectionHandler->GetSelectedFaderGroups();

	// Remove Fader Groups which don't match filtering
	SelectedFaderGroupObjects.RemoveAll([](const TWeakObjectPtr<UObject>& SelectedFaderGroupObject)
		{
			const UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
			return SelectedFaderGroup && !SelectedFaderGroup->IsMatchingFilter();
		});

	bool bHasFixturePatch = false;
	if (SelectedFaderGroupObjects.IsEmpty())
	{
		return bHasFixturePatch;
	}

	for (const TWeakObjectPtr<UObject>& SelectedFaderGroupObject : SelectedFaderGroupObjects)
	{
		const UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
		if (!SelectedFaderGroup)
		{
			continue;
		}

		const UDMXEntityFixturePatch* FixturePatch = SelectedFaderGroup->GetFixturePatch();
		if (!FixturePatch)
		{
			continue;
		}

		bHasFixturePatch = true;
		break;
	}

	return bHasFixturePatch;
}

FReply FDMXControlConsoleFaderGroupDetails::OnClearButtonClicked()
{
	const FScopedTransaction FaderGroupFixturePatchClearTransaction(LOCTEXT("FaderGroupFixturePatchClearTransaction", "Clear Fixture Patch"));

	UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel->GetSelectionHandler();
	const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();
	for (const TWeakObjectPtr<UObject>& SelectedFaderGroupObject : SelectedFaderGroupsObjects)
	{
		UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
		if (!SelectedFaderGroup || !SelectedFaderGroup->IsMatchingFilter() || !SelectedFaderGroup->HasFixturePatch())
		{
			continue;
		}

		SelectedFaderGroup->PreEditChange(nullptr);

		// Replace patched Fader Group with a not patched one
		UDMXControlConsoleFaderGroupRow& OwnerRow = SelectedFaderGroup->GetOwnerFaderGroupRowChecked();
		const int32 Index = SelectedFaderGroup->GetIndex();
		SelectedFaderGroup->SetIsActive(false);

		OwnerRow.PreEditChange(UDMXControlConsoleFaderGroupRow::StaticClass()->FindPropertyByName(UDMXControlConsoleFaderGroupRow::GetFaderGroupsPropertyName()));
		UDMXControlConsoleFaderGroup* ClearedFaderGroup = OwnerRow.AddFaderGroup(Index);
		OwnerRow.PostEditChange();

		// Handle selection for both FaderGroups
		constexpr bool bNotifySelectionChange = false;
		SelectionHandler->AddToSelection(ClearedFaderGroup, bNotifySelectionChange);
		SelectionHandler->RemoveFromSelection(SelectedFaderGroup, bNotifySelectionChange);

		SelectedFaderGroup->PostEditChange();
	}
	SelectionHandler->RemoveInvalidObjectsFromSelection();

	return FReply::Handled();
}

ECheckBoxState FDMXControlConsoleFaderGroupDetails::IsLockChecked() const
{
	UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel->GetSelectionHandler();
	TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupObjects = SelectionHandler->GetSelectedFaderGroups();

	// Remove Fader Groups which don't match filtering
	SelectedFaderGroupObjects.RemoveAll([](const TWeakObjectPtr<UObject>& SelectedFaderGroupObject)
		{
			const UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
			return SelectedFaderGroup && !SelectedFaderGroup->IsMatchingFilter();
		});
	
	const bool bAreAllFaderGroupsUnlocked = Algo::AllOf(SelectedFaderGroupObjects, [](const TWeakObjectPtr<UObject>& SelectedFaderGroupObject)
		{
			const UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
			return SelectedFaderGroup && !SelectedFaderGroup->IsLocked();
		});

	if (bAreAllFaderGroupsUnlocked)
	{
		return ECheckBoxState::Unchecked;
	}
	
	const bool bIsAnyFaderGroupUnlocked = Algo::AnyOf(SelectedFaderGroupObjects, [](const TWeakObjectPtr<UObject>& SelectedFaderGroupObject)
		{
			const UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
			return SelectedFaderGroup && !SelectedFaderGroup->IsLocked();
		});

	return bIsAnyFaderGroupUnlocked ? ECheckBoxState::Undetermined : ECheckBoxState::Checked;
}

void FDMXControlConsoleFaderGroupDetails::OnLockToggleChanged(ECheckBoxState CheckState)
{
	UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel->GetSelectionHandler();
	const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupObjects = SelectionHandler->GetSelectedFaderGroups();
	Algo::ForEach(SelectedFaderGroupObjects, [CheckState](const TWeakObjectPtr<UObject>& SelectedFaderGroupObject)
		{
			UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
			if (SelectedFaderGroup && SelectedFaderGroup->IsMatchingFilter())
			{
				const bool bIsLocked = CheckState == ECheckBoxState::Checked;
				SelectedFaderGroup->SetLock(bIsLocked);
			}
		});
}

FText FDMXControlConsoleFaderGroupDetails::GetFixturePatchText() const
{
	UDMXControlConsoleEditorModel* EditorConsoleModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorConsoleModel->GetSelectionHandler();
	const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();

	if (SelectedFaderGroupsObjects.IsEmpty())
	{
		return FText::GetEmpty();
	}
	else if (SelectedFaderGroupsObjects.Num() > 1 && DoSelectedFaderGroupsHaveAnyFixturePatches())
	{
		return FText::FromString(TEXT("Multiple Values"));
	}

	const UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupsObjects[0]);
	if (!SelectedFaderGroup)
	{
		return FText::GetEmpty();
	}

	if (!SelectedFaderGroup->HasFixturePatch())
	{
		return LOCTEXT("FaderGroupFixturePatchNoneName", "None");
	}

	UDMXEntityFixturePatch* FixturePatch = SelectedFaderGroup->GetFixturePatch();
	return FText::FromString(FixturePatch->GetDisplayName());
}

EVisibility FDMXControlConsoleFaderGroupDetails::GetEditorColorVisibility() const
{
	return DoSelectedFaderGroupsHaveAnyFixturePatches() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility FDMXControlConsoleFaderGroupDetails::GetClearButtonVisibility() const
{
	return DoSelectedFaderGroupsHaveAnyFixturePatches() ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
