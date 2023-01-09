// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleDetails.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleEditorManager.h"
#include "Widgets/SDMXControlConsoleEditorPortSelector.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleDetails"

void FDMXControlConsoleDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	PropertyUtilities = InDetailLayout.GetPropertyUtilities();

	IDetailCategoryBuilder& ControlConsoleCategory = InDetailLayout.EditCategory("DMX Control Console", FText::GetEmpty());
	const TSharedPtr<IPropertyHandle> FaderGroupRowsHandle = InDetailLayout.GetProperty(UDMXControlConsole::GetFaderGroupRowsPropertyName());
	InDetailLayout.HideProperty(FaderGroupRowsHandle);
	
	const TSharedPtr<IPropertyHandle> DMXLibraryHandle = InDetailLayout.GetProperty(UDMXControlConsole::GetDMXLibraryPropertyName());
	InDetailLayout.HideProperty(DMXLibraryHandle);
	ControlConsoleCategory.AddProperty(DMXLibraryHandle);
	DMXLibraryHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleDetails::ForceRefresh));
	DMXLibraryHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleDetails::ForceRefresh));

	GeneratePortSelectorRow(InDetailLayout);
}

void FDMXControlConsoleDetails::GeneratePortSelectorRow(IDetailLayoutBuilder& InDetailLayout)
{
	IDetailCategoryBuilder& ControlConsoleCategory = InDetailLayout.EditCategory("DMX Control Console", FText::GetEmpty());

	ControlConsoleCategory.AddCustomRow(FText::GetEmpty())
		.WholeRowContent()
		[
			SAssignNew(PortSelector, SDMXControlConsoleEditorPortSelector)
			.OnPortsSelected(this, &FDMXControlConsoleDetails::OnSelectedPortsChanged)
		];

	OnSelectedPortsChanged();
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
	UDMXControlConsole* ControlConsole = FDMXControlConsoleEditorManager::Get().GetDMXControlConsole();
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

#undef LOCTEXT_NAMESPACE
