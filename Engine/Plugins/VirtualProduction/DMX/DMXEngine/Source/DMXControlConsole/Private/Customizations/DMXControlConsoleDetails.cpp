// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleDetails.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleManager.h"
#include "DMXControlConsoleRawFader.h"
#include "DMXControlConsoleSelection.h"
#include "DMXProtocolCommon.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "Widgets/SDMXControlConsoleDetailsRowWidget.h"
#include "Widgets/SDMXControlConsolePortSelector.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Layout/Visibility.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleDetails"

void FDMXControlConsoleDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	const UDMXControlConsole* ControlConsole = FDMXControlConsoleManager::Get().GetDMXControlConsole();
	if (!ControlConsole)
	{
		return;
	}

	PropertyUtilities = InDetailLayout.GetPropertyUtilities();
	DMXLibrary = ControlConsole->GetDMXLibrary();
	UpdateFixturePatches();

	IDetailCategoryBuilder& ControlConsoleCategory = InDetailLayout.EditCategory("DMX DMX Control Console", FText::GetEmpty());
	const TSharedPtr<IPropertyHandle> DMXLibraryHandle = InDetailLayout.GetProperty(UDMXControlConsole::GetDMXLibraryPropertyName());
	InDetailLayout.HideProperty(DMXLibraryHandle);
	ControlConsoleCategory.AddProperty(DMXLibraryHandle);
	DMXLibraryHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleDetails::ForceRefresh));
	DMXLibraryHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleDetails::ForceRefresh));
	
	GeneratePortSelectorRow(InDetailLayout);
	GenerateFixturePatchDetailsRows(InDetailLayout);
}

void FDMXControlConsoleDetails::GeneratePortSelectorRow(IDetailLayoutBuilder& InDetailLayout)
{
	IDetailCategoryBuilder& ControlConsoleCategory = InDetailLayout.EditCategory("DMX DMX Control Console", FText::GetEmpty());

	ControlConsoleCategory.AddCustomRow(FText::GetEmpty())
		.WholeRowContent()
		[
			SAssignNew(PortSelector, SDMXControlConsolePortSelector)
			.OnPortsSelected(this, &FDMXControlConsoleDetails::OnSelectedPortsChanged)
		];

	OnSelectedPortsChanged();
}

void FDMXControlConsoleDetails::GenerateFixturePatchDetailsRows(IDetailLayoutBuilder& InDetailLayout)
{
	const TSharedPtr<IPropertyHandle> FaderGroupRowsHandle = InDetailLayout.GetProperty(UDMXControlConsole::GetFaderGroupRowsPropertyName());
	InDetailLayout.HideProperty(FaderGroupRowsHandle);

	IDetailCategoryBuilder& ControlConsoleCategory = InDetailLayout.EditCategory("DMX DMX Control Console", FText::GetEmpty());
	ControlConsoleCategory.AddCustomRow(FText::GetEmpty())
		.Visibility(TAttribute<EVisibility>(this, &FDMXControlConsoleDetails::GetAddAllPatchesButtonVisibility))
		.WholeRowContent()
		[
			SNew(SBox)
			.Padding(5.f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.OnClicked(this, &FDMXControlConsoleDetails::OnAddAllPatchesClicked)
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(LOCTEXT("Add All Patches", "Add All Patches"))
				]
			]
		];

	FixturePatchDetailsRowWidgets.Reset();
	for (const FDMXEntityFixturePatchRef FixturePatchRef : FixturePatches)
	{
		const TSharedRef<SDMXControlConsoleDetailsRowWidget> ControlConsoleDetailsRow =
			SNew(SDMXControlConsoleDetailsRowWidget, FixturePatchRef)
			.OnSelectFixturePatchDetailsRow(this, &FDMXControlConsoleDetails::OnSelectFixturePatchDetailsRow)
			.OnGenerateFromFixturePatch(this, &FDMXControlConsoleDetails::OnGenerateFaderGroupFromFixturePatch);

		ControlConsoleCategory.AddCustomRow(FText::GetEmpty())
			.WholeRowContent()
			[
				ControlConsoleDetailsRow
			];

		FixturePatchDetailsRowWidgets.Add(ControlConsoleDetailsRow);
	}
}

void FDMXControlConsoleDetails::OnSelectFixturePatchDetailsRow(const TSharedRef<SDMXControlConsoleDetailsRowWidget>& DetailsRow)
{
	if (SelectedDetailsRowWidget.IsValid())
	{
		SelectedDetailsRowWidget.Pin()->Unselect();
	}

	SelectedDetailsRowWidget = DetailsRow;
	DetailsRow->Select();
}

void FDMXControlConsoleDetails::OnGenerateFaderGroupFromFixturePatch(const TSharedRef<SDMXControlConsoleDetailsRowWidget>& DetailsRow)
{
	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();
	for (const TWeakObjectPtr<UObject>& SelectedFaderGroupObject : SelectedFaderGroupsObjects)
	{
		UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
		if (!SelectedFaderGroup)
		{
			continue;
		}

		const FScopedTransaction FaderGroupTransaction(LOCTEXT("FaderGroupTransaction", "Generate Fader Group from Fixture Patch"));
		SelectedFaderGroup->PreEditChange(UDMXControlConsoleFaderGroup::StaticClass()->FindPropertyByName(UDMXControlConsoleFaderGroup::GetFixturePatchRefPropertyName()));

		SelectionHandler->ClearFadersSelection(SelectedFaderGroup);

		const FDMXEntityFixturePatchRef FixturePatchRef = DetailsRow->GetFixturePatchRef();
		SelectedFaderGroup->GenerateFromFixturePatch(FixturePatchRef);

		SelectedFaderGroup->PostEditChange();
	}

	ForceRefresh();
}

void FDMXControlConsoleDetails::UpdateFixturePatches()
{
	if (!DMXLibrary.IsValid())
	{
		return;
	}

	TArray<UDMXEntityFixturePatch*> FixturePatchesInLibrary = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();

	FixturePatches.Reset(FixturePatchesInLibrary.Num());
	for (UDMXEntityFixturePatch* FixturePatch : FixturePatchesInLibrary)
	{
		FDMXEntityFixturePatchRef FixturePatchRef;
		FixturePatchRef.SetEntity(FixturePatch);

		FixturePatches.Add(FixturePatchRef);
	}
}

void FDMXControlConsoleDetails::ForceRefresh() const
{
	if (!PropertyUtilities.IsValid())
	{
		return;
	}
	
	PropertyUtilities->ForceRefresh();
}

void FDMXControlConsoleDetails::OnSelectedPortsChanged()
{
	UDMXControlConsole* ControlConsole = FDMXControlConsoleManager::Get().GetDMXControlConsole();
	if (!ControlConsole)
	{
		return;
	}

	if (!PortSelector.IsValid())
	{
		return;
	}

	const TArray<FDMXOutputPortSharedRef> SelectedOutputPorts = PortSelector->GetSelectedOutputPorts();
	ControlConsole->UpdateOutputPorts(SelectedOutputPorts);
}

FReply FDMXControlConsoleDetails::OnAddAllPatchesClicked()
{
	UDMXControlConsole* ControlConsole = FDMXControlConsoleManager::Get().GetDMXControlConsole();
	if (!ControlConsole)
	{
		return FReply::Unhandled();
	}

	const FScopedTransaction ControlConsoleTransaction(LOCTEXT("ControlConsoleTransaction", "Generate from Library"));
	ControlConsole->PreEditChange(nullptr);
	ControlConsole->GenarateFromDMXLibrary();
	ControlConsole->PostEditChange();

	return FReply::Handled();
}

EVisibility FDMXControlConsoleDetails::GetAddAllPatchesButtonVisibility() const
{
	return DMXLibrary.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
