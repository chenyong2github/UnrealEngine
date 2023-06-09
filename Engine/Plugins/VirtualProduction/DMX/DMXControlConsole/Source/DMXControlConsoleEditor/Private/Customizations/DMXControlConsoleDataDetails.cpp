// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleDataDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DMXControlConsoleData.h"
#include "Editor.h"
#include "IPropertyUtilities.h"
#include "Models/DMXControlConsoleEditorModel.h"
#include "PropertyHandle.h"
#include "TimerManager.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleDataDetails"

void FDMXControlConsoleDataDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	PropertyUtilities = InDetailLayout.GetPropertyUtilities();

	IDetailCategoryBuilder& ControlConsoleCategory = InDetailLayout.EditCategory("DMX Control Console", FText::GetEmpty());
	const TSharedPtr<IPropertyHandle> FaderGroupRowsHandle = InDetailLayout.GetProperty(UDMXControlConsoleData::GetFaderGroupRowsPropertyName());
	InDetailLayout.HideProperty(FaderGroupRowsHandle);
	
	const TSharedPtr<IPropertyHandle> DMXLibraryHandle = InDetailLayout.GetProperty(UDMXControlConsoleData::GetDMXLibraryPropertyName());
	DMXLibraryHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleDataDetails::OnDMXLibraryChanged));
	DMXLibraryHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleDataDetails::OnDMXLibraryChanged));
}

void FDMXControlConsoleDataDetails::ForceRefresh() const
{
	if (!PropertyUtilities.IsValid())
	{
		return;
	}
	
	PropertyUtilities->ForceRefresh();
}

void FDMXControlConsoleDataDetails::OnDMXLibraryChanged() const
{
	const UDMXControlConsoleEditorModel* EditorModel = GetDefault<UDMXControlConsoleEditorModel>();
	if (UDMXControlConsoleData* EditorConsoleData = EditorModel->GetEditorConsoleData())
	{
		EditorConsoleData->Clear();
		if (const UDMXLibrary* DMXLibrary = EditorConsoleData->GetDMXLibrary())
		{
			GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateUObject(EditorConsoleData, &UDMXControlConsoleData::GenerateFromDMXLibrary));
		}
	}
}

#undef LOCTEXT_NAMESPACE
