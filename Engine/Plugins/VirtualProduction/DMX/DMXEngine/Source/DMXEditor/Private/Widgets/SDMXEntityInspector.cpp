// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXEntityInspector.h"

#include "DMXEditor.h"
#include "DMXEditorLog.h"
#include "DMXProtocolConstants.h"

#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"

#include "Game/DMXComponent.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "SDMXEntityInspector"

void SDMXEntityInspector::Construct(const FArguments& InArgs)
{
	// Initialize input arguments
	DMXEditor = InArgs._DMXEditor;
	UserOnFinishedChangingProperties = InArgs._OnFinishedChangingProperties;
	bIsShowSearch = InArgs._ShowSearch;
	bIsShowTitleArea = InArgs._ShowTitleArea;
	bIsHideFilterArea = InArgs._HideFilterArea;

	// Do not update by default
	bRefreshOnTick = false;

	// Initialize property view widget
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FNotifyHook* NotifyHook = nullptr;
	if (InArgs._SetNotifyHook)
	{
		NotifyHook = DMXEditor.Pin().Get();
	}
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = bIsShowSearch;
	DetailsViewArgs.NameAreaSettings = InArgs._HideNameArea ? FDetailsViewArgs::HideNameArea : FDetailsViewArgs::ObjectsUseNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.NotifyHook = NotifyHook; 
	DetailsViewArgs.ViewIdentifier = InArgs._ViewIdentifier;
	PropertyView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	ChildSlot
	[	SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(EditingWidget, SBorder)
		]
	];

	// Update based on the current (empty) selection set
	TArray<UObject*> InitialSelectedObjects;
	UpdateFromObjects(InitialSelectedObjects);
}

void SDMXEntityInspector::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRefreshOnTick)
	{
		UpdateFromObjects(RefreshPropertyObjects);
		RefreshPropertyObjects.Empty();
		bRefreshOnTick = false;
	}
}

void SDMXEntityInspector::ShowDetailsForSingleEntity(UDMXEntity* Entity)
{
	check(Entity);
	ShowDetailsForEntities(TArray<UDMXEntity*>{ Entity });
}

void SDMXEntityInspector::ShowDetailsForEntities(const TArray<UDMXEntity*>& Entities)
{
	// Refresh is being deferred until the next tick, this prevents batch operations from bombarding the details view with calls to refresh
	RefreshPropertyObjects = TArray<UObject*>(Entities);
	bRefreshOnTick = true;
}

void SDMXEntityInspector::UpdateFromObjects(const TArray<UObject*>& PropertyObjects)
{
	// Register IDetailsView callbacks
	PropertyView->OnFinishedChangingProperties().Clear();
	PropertyView->OnFinishedChangingProperties().Add(UserOnFinishedChangingProperties);

	// Update our context-sensitive editing widget
	EditingWidget->SetContent(MakeEditingWidget(PropertyObjects));
}

TSharedRef<SWidget> SDMXEntityInspector::MakeEditingWidget(const TArray<UObject*>& Objects)
{
	TSharedRef< SVerticalBox > InnerEditingWidget = SNew( SVerticalBox );

	// Show the property editor
	PropertyView->HideFilterArea(bIsHideFilterArea);
	PropertyView->SetObjects(Objects, true);

	InnerEditingWidget->AddSlot()
	.FillHeight( 0.9f )
	.VAlign( VAlign_Fill )
	[
		SNew( SBox )
		[
			SNew( SVerticalBox )
			+SVerticalBox::Slot()
			.AutoHeight()
			+SVerticalBox::Slot()
			[
				PropertyView.ToSharedRef()
			]
		]
	];

	return InnerEditingWidget;
}

#undef LOCTEXT_NAMESPACE
