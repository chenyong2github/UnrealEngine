// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleFixturePatchVerticalBox.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFaderGroupRow.h"
#include "DMXControlConsoleManager.h"
#include "DMXControlConsoleSelection.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXLibrary.h"
#include "Widgets/SDMXControlConsoleFixturePatchRowWidget.h"

#include "ScopedTransaction.h"
#include "Layout/Visibility.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleFixturePatchVerticalBox"

void SDMXControlConsoleFixturePatchVerticalBox::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.Padding(5.f)
				.Visibility(TAttribute<EVisibility>(this, &SDMXControlConsoleFixturePatchVerticalBox::GetAddAllPatchesButtonVisibility))
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.OnClicked(this, &SDMXControlConsoleFixturePatchVerticalBox::OnAddAllPatchesClicked)
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.Text(LOCTEXT("Add All Patches", "Add All Patches"))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(FixturePatchRowsBoxWidget, SVerticalBox)
			]
		];
}

void SDMXControlConsoleFixturePatchVerticalBox::UpdateFixturePatchRows()
{
	UDMXControlConsole* ControlConsole = FDMXControlConsoleManager::Get().GetDMXControlConsole();
	if (!ControlConsole)
	{
		return;
	}

	UDMXLibrary* CurrentDMXLibrary = ControlConsole->GetDMXLibrary();
	if (CurrentDMXLibrary == DMXLibrary)
	{
		return;
	}

	DMXLibrary = CurrentDMXLibrary;

	FixturePatchRowsBoxWidget->ClearChildren();
	FixturePatchRowWidgets.Reset();
	FixturePatches.Reset();
	UpdateFixturePatches();

	for (const FDMXEntityFixturePatchRef FixturePatchRef : FixturePatches)
	{
		auto DetailsRowVisibilityLambda = [ControlConsole, FixturePatchRef]()
		{
			const TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups = ControlConsole->GetAllFaderGroups();
			for (UDMXControlConsoleFaderGroup* FaderGroup : AllFaderGroups)
			{
				if (!FaderGroup)
				{
					continue;
				}

				if (!FaderGroup->HasFixturePatch())
				{
					continue;
				}

				UDMXEntityFixturePatch* FixturePatch = FaderGroup->GetFixturePatch();
				if (FixturePatch != FixturePatchRef.GetFixturePatch())
				{
					continue;
				}

				return EVisibility::Collapsed;
			}

			return EVisibility::Visible;
		};

		const TSharedRef<SDMXControlConsoleFixturePatchRowWidget> ControlConsoleDetailsRow =
			SNew(SDMXControlConsoleFixturePatchRowWidget, FixturePatchRef)
			.Visibility(TAttribute<EVisibility>::CreateLambda(DetailsRowVisibilityLambda))
			.OnSelectFixturePatchRow(this, &SDMXControlConsoleFixturePatchVerticalBox::OnSelectFixturePatchDetailsRow)
			.OnGenerateOnLastRow(this, &SDMXControlConsoleFixturePatchVerticalBox::OnGenerateFromFixturePatchOnLastRow)
			.OnGenerateOnNewRow(this, &SDMXControlConsoleFixturePatchVerticalBox::OnGenerateFromFixturePatchOnNewRow)
			.OnGenerateOnSelectedFaderGroup(this, &SDMXControlConsoleFixturePatchVerticalBox::OnGenerateSelectedFaderGroupFromFixturePatch);

		FixturePatchRowsBoxWidget->AddSlot()
			.AttachWidget(ControlConsoleDetailsRow);

		FixturePatchRowWidgets.Add(ControlConsoleDetailsRow);
	}
}

void SDMXControlConsoleFixturePatchVerticalBox::UpdateFixturePatches()
{
	if (!DMXLibrary.IsValid())
	{
		return;
	}

	const TArray<UDMXEntityFixturePatch*> FixturePatchesInLibrary = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
	for (UDMXEntityFixturePatch* FixturePatch : FixturePatchesInLibrary)
	{
		if (!FixturePatch)
		{
			continue;
		}

		FDMXEntityFixturePatchRef FixturePatchRef;
		FixturePatchRef.SetEntity(FixturePatch);

		FixturePatches.Add(FixturePatchRef);
	}
}

void SDMXControlConsoleFixturePatchVerticalBox::GenerateFaderGroupFromFixturePatch(UDMXControlConsoleFaderGroup* FaderGroup, UDMXEntityFixturePatch* FixturePatch)
{
	if (!FaderGroup || !FixturePatch)
	{
		return;
	}

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	SelectionHandler->ClearFadersSelection(FaderGroup);

	const FScopedTransaction FaderGroupTransaction(LOCTEXT("FaderGroupTransaction", "Generate Fader Group from Fixture Patch"));
	FaderGroup->PreEditChange(UDMXControlConsoleFaderGroup::StaticClass()->FindPropertyByName(UDMXControlConsoleFaderGroup::GetFixturePatchPropertyName()));

	FaderGroup->GenerateFromFixturePatch(FixturePatch);

	FaderGroup->PostEditChange();
}

void SDMXControlConsoleFixturePatchVerticalBox::OnSelectFixturePatchDetailsRow(const TSharedRef<SDMXControlConsoleFixturePatchRowWidget>& FixturePatchRow)
{
	if (SelectedFixturePatchRowWidget.IsValid())
	{
		SelectedFixturePatchRowWidget.Pin()->Unselect();
	}

	SelectedFixturePatchRowWidget = FixturePatchRow;
	FixturePatchRow->Select();
}

void SDMXControlConsoleFixturePatchVerticalBox::OnGenerateFromFixturePatchOnLastRow(const TSharedRef<SDMXControlConsoleFixturePatchRowWidget>& FixturePatchRow)
{
	UDMXControlConsole* ControlConsole = FDMXControlConsoleManager::Get().GetDMXControlConsole();
	if (!ControlConsole)
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroupRow*> FaderGroupRows = ControlConsole->GetFaderGroupRows();
	if (FaderGroupRows.IsEmpty())
	{
		return;
	}

	UDMXControlConsoleFaderGroupRow* FaderGroupRow = nullptr;
	int32 Index = -1;

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();
	if (SelectedFaderGroupsObjects.IsEmpty())
	{
		FaderGroupRow = FaderGroupRows.Last();
		if (!FaderGroupRow)
		{
			return;
		}

		Index = FaderGroupRow->GetFaderGroups().Num();
	}
	else
	{
		UDMXControlConsoleFaderGroup* SelectedFaderGroup = SelectionHandler->GetFirstSelectedFaderGroup();
		if (!SelectedFaderGroup)
		{
			return;
		}

		FaderGroupRow = &SelectedFaderGroup->GetOwnerFaderGroupRowChecked();
		Index = SelectedFaderGroup->GetIndex() + 1;
	}
	
	UDMXControlConsoleFaderGroup* FaderGroup = FaderGroupRow->AddFaderGroup(Index);
	UDMXEntityFixturePatch* FixturePatch = FixturePatchRow->GetFixturePatchRef().GetFixturePatch();

	GenerateFaderGroupFromFixturePatch(FaderGroup, FixturePatch);
}

void SDMXControlConsoleFixturePatchVerticalBox::OnGenerateFromFixturePatchOnNewRow(const TSharedRef<SDMXControlConsoleFixturePatchRowWidget>& FixturePatchRow)
{
	UDMXControlConsole* ControlConsole = FDMXControlConsoleManager::Get().GetDMXControlConsole();
	if (!ControlConsole)
	{
		return;
	}

	int32 NewRowIndex = -1;

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();
	if (SelectedFaderGroupsObjects.IsEmpty())
	{
		NewRowIndex = ControlConsole->GetFaderGroupRows().Num();
	}
	else
	{
		UDMXControlConsoleFaderGroup* SelectedFaderGroup = SelectionHandler->GetFirstSelectedFaderGroup();
		if (!SelectedFaderGroup)
		{
			return;
		}

		const int32 SelectedFaderGroupRowIndex = SelectedFaderGroup->GetOwnerFaderGroupRowChecked().GetRowIndex();
		NewRowIndex = SelectedFaderGroupRowIndex + 1;
	}

	UDMXControlConsoleFaderGroupRow* NewRow = ControlConsole->AddFaderGroupRow(NewRowIndex);
	if (NewRow->GetFaderGroups().IsEmpty())
	{
		return;
	}

	UDMXControlConsoleFaderGroup* FaderGroup = NewRow->GetFaderGroups()[0];
	UDMXEntityFixturePatch* FixturePatch = FixturePatchRow->GetFixturePatchRef().GetFixturePatch();

	GenerateFaderGroupFromFixturePatch(FaderGroup, FixturePatch);
}

void SDMXControlConsoleFixturePatchVerticalBox::OnGenerateSelectedFaderGroupFromFixturePatch(const TSharedRef<SDMXControlConsoleFixturePatchRowWidget>& FixturePatchRow)
{
	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();
	if (SelectedFaderGroupsObjects.IsEmpty())
	{
		return;
	}
	
	UDMXControlConsoleFaderGroup* FaderGroup = SelectionHandler->GetFirstSelectedFaderGroup();
	UDMXEntityFixturePatch* FixturePatch = FixturePatchRow->GetFixturePatchRef().GetFixturePatch();

	GenerateFaderGroupFromFixturePatch(FaderGroup, FixturePatch);
}

FReply SDMXControlConsoleFixturePatchVerticalBox::OnAddAllPatchesClicked()
{
	UDMXControlConsole* ControlConsole = FDMXControlConsoleManager::Get().GetDMXControlConsole();
	if (ControlConsole)
	{
		const FScopedTransaction ControlConsoleTransaction(LOCTEXT("ControlConsoleTransaction", "Generate from Library"));
		ControlConsole->PreEditChange(nullptr);
		ControlConsole->GenarateFromDMXLibrary();
		ControlConsole->PostEditChange();
	}

	return FReply::Handled();
}

EVisibility SDMXControlConsoleFixturePatchVerticalBox::GetAddAllPatchesButtonVisibility() const
{
	UDMXControlConsole* ControlConsole = FDMXControlConsoleManager::Get().GetDMXControlConsole();
	if (!ControlConsole)
	{
		return EVisibility::Collapsed;
	}

	return DMXLibrary.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
