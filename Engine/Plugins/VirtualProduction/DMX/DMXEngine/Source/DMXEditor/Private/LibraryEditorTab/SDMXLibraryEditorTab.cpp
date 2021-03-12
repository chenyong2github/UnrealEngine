// Copyright Epic Games, Inc. All Rights Reserved.

#include "LibraryEditorTab/SDMXLibraryEditorTab.h"

#include "Library/DMXLibrary.h"

#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"


void SDMXLibraryEditorTab::Construct(const FArguments& InArgs)
{
	DMXEditor = InArgs._DMXEditor;

	// Initialize property view widget
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs( 
		/* bUpdateFromSelection = */	false, 
		/* bLockable = */				false, 
		/* bAllowSearch = */			false,
		/* NameAreaSettings = */		FDetailsViewArgs::ObjectsUseNameArea,
		/* bHideSelectionTip = */		true, 
		/* InNotifyHook = */			nullptr, 
		/* InSearchInitialKeyFocus = */	false, 
		/* InViewIdentifier = */		NAME_None);

	DMXLibraryDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	if (IsValid(InArgs._DMXLibrary))
	{
		DMXLibraryDetailsView->SetObject(InArgs._DMXLibrary, true);

		ChildSlot
			[
				DMXLibraryDetailsView.ToSharedRef()
			];
	}
}
