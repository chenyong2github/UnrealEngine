// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLensFilePanel.h"

#include "IStructureDetailsView.h"
#include "LensFile.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SLensDataViewer.h"
#include "UObject/StructOnScope.h"


#define LOCTEXT_NAMESPACE "LensFilePanel"


void SLensFilePanel::Construct(const FArguments& InArgs, ULensFile* InLensFile)
{
	LensFile = TStrongObjectPtr<ULensFile>(InLensFile);
	CachedFIZ = InArgs._CachedFIZData;

	FStructureDetailsViewArgs LensInfoStructDetailsView;
	FDetailsViewArgs DetailArgs;
	DetailArgs.bAllowSearch = false;
	DetailArgs.bShowScrollBar = false;

	FPropertyEditorModule& PropertyEditor = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	TSharedPtr<FStructOnScope> LensInfoStructScope = MakeShared<FStructOnScope>(FLensInfo::StaticStruct(), reinterpret_cast<uint8*>(&InLensFile->LensInfo));
	TSharedPtr<IStructureDetailsView> LensInfoStructureDetailsView = PropertyEditor.CreateStructureDetailView(DetailArgs, LensInfoStructDetailsView, LensInfoStructScope);

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(0.8f)
		[
			SNew(SLensDataViewer, InLensFile)
			.CachedFIZData(InArgs._CachedFIZData)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(0.2f)
		[
			LensInfoStructureDetailsView->GetWidget().ToSharedRef()
		]
	];
}

#undef LOCTEXT_NAMESPACE /*RemoteControlPanel*/
