// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/SMotionTrailOptions.h"
#include "Tools/MotionTrailOptions.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "MotionTrail"

void SMotionTrailOptions::Construct(const FArguments& InArgs)
{
	UMotionTrailToolOptions* Settings = GetMutableDefault<UMotionTrailToolOptions>();
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bShowPropertyMatrixButton = false;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowFavoriteSystem = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.ViewIdentifier = "MotionTrailOptions";

	DetailsView = PropertyEditor.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(Settings);

	ChildSlot
		[
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(10.0,5.0,10.0,5.0))
			[
			SNew(SVerticalBox)
			
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.HAlign(HAlign_Fill)
				[
					DetailsView.ToSharedRef()
				]
			]
			
		];
}


#undef LOCTEXT_NAMESPACE
