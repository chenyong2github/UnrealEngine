// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleDataDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DMXControlConsoleData.h"
#include "Editor.h"
#include "IPropertyUtilities.h"
#include "Layouts/DMXControlConsoleEditorGlobalLayoutBase.h"
#include "Layouts/DMXControlConsoleEditorGlobalLayoutDefault.h"
#include "Layouts/DMXControlConsoleEditorGlobalLayoutUser.h"
#include "Layouts/DMXControlConsoleEditorLayouts.h"
#include "Models/DMXControlConsoleEditorModel.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "TimerManager.h"
#include "Widgets/SDMXControlConsoleEditorLayoutPicker.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleDataDetails"

void FDMXControlConsoleDataDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	PropertyUtilities = InDetailLayout.GetPropertyUtilities();
	
	const TSharedPtr<IPropertyHandle> DMXLibraryHandle = InDetailLayout.GetProperty(UDMXControlConsoleData::GetDMXLibraryPropertyName());
	DMXLibraryHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleDataDetails::OnDMXLibraryChanged));
	DMXLibraryHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleDataDetails::OnDMXLibraryChanged));
	InDetailLayout.AddPropertyToCategory(DMXLibraryHandle);

	// Layout Mode selection section
	IDetailCategoryBuilder& ControlConsoleCategory = InDetailLayout.EditCategory("DMX Control Console", FText::GetEmpty());
	ControlConsoleCategory.AddCustomRow(FText::GetEmpty())
		.NameContent()
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.Text(LOCTEXT("LayoutLabel", "Layout"))
		]
		.ValueContent()
		[
			SNew(SDMXControlConsoleEditorLayoutPicker)
		];
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
	const FScopedTransaction ClearPatchedFaderGroupsTransaction(LOCTEXT("ClearPatchedFaderGroupsTransaction", "Clear Patched Fader Groups"));

	const UDMXControlConsoleEditorModel* EditorModel = GetDefault<UDMXControlConsoleEditorModel>();
	const UDMXControlConsoleEditorLayouts* EditorConsoleLayouts = EditorModel->GetEditorConsoleLayouts();
	if (EditorConsoleLayouts)
	{
		const TArray<UDMXControlConsoleEditorGlobalLayoutUser*> UserLayouts = EditorConsoleLayouts->GetUserLayouts();
		for (UDMXControlConsoleEditorGlobalLayoutUser* UserLayout : UserLayouts)
		{
			if (!UserLayout)
			{
				continue;
			}

			UserLayout->PreEditChange(nullptr);
			UserLayout->ClearAllPatchedFaderGroups();
			UserLayout->ClearEmptyLayoutRows();
			UserLayout->PostEditChange();
		}
	}

	UDMXControlConsoleData* EditorConsoleData = EditorModel->GetEditorConsoleData();
	if (EditorConsoleData)
	{
		EditorConsoleData->PreEditChange(nullptr);
		EditorConsoleData->ClearPatchedFaderGroups();
		EditorConsoleData->PostEditChange();

		const UDMXLibrary* DMXLibrary = EditorConsoleData->GetDMXLibrary();
		GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateSP(this, &FDMXControlConsoleDataDetails::UpdateControlConsole));
	}
}

void FDMXControlConsoleDataDetails::UpdateControlConsole() const
{
	const FScopedTransaction UpdateControlConsoleTransaction(LOCTEXT("UpdateControlConsoleTransaction", "Update Control Console"));
	UDMXControlConsoleEditorModel* EditorModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
	UDMXControlConsoleData* EditorConsoleData = EditorModel->GetEditorConsoleData();
	UDMXControlConsoleEditorLayouts* EditorConsoleLayouts = EditorModel->GetEditorConsoleLayouts();
	if (EditorConsoleData && EditorConsoleLayouts)
	{
		EditorConsoleData->PreEditChange(nullptr);
		EditorConsoleData->GenerateFromDMXLibrary();
		EditorConsoleData->PostEditChange();

		EditorConsoleLayouts->PreEditChange(nullptr);
		EditorConsoleLayouts->UpdateDefaultLayout(EditorConsoleData);
		EditorConsoleLayouts->PostEditChange();

		EditorModel->RequestRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
