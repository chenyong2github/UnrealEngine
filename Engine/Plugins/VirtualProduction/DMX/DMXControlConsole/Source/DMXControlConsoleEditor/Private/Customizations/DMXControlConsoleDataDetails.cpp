// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleDataDetails.h"

#include "DMXControlConsoleData.h"
#include "Models/DMXControlConsoleEditorModel.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleDataDetails"

void FDMXControlConsoleDataDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	PropertyUtilities = InDetailLayout.GetPropertyUtilities();

	IDetailCategoryBuilder& ControlConsoleCategory = InDetailLayout.EditCategory("DMX Control Console", FText::GetEmpty());
	const TSharedPtr<IPropertyHandle> FaderGroupRowsHandle = InDetailLayout.GetProperty(UDMXControlConsoleData::GetFaderGroupRowsPropertyName());
	InDetailLayout.HideProperty(FaderGroupRowsHandle);
	
	const TSharedPtr<IPropertyHandle> DMXLibraryHandle = InDetailLayout.GetProperty(UDMXControlConsoleData::GetDMXLibraryPropertyName());
	DMXLibraryHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleDataDetails::ForceRefresh));
	DMXLibraryHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleDataDetails::ForceRefresh));
}

void FDMXControlConsoleDataDetails::ForceRefresh() const
{
	if (!PropertyUtilities.IsValid())
	{
		return;
	}
	
	PropertyUtilities->ForceRefresh();
}

#undef LOCTEXT_NAMESPACE
