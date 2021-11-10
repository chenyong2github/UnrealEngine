// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/FixturePatch/SDMXFixturePatchEditor.h"

#include "DMXEditor.h"
#include "DMXFixturePatchSharedData.h"
#include "DMXSubsystem.h"
#include "SDMXFixturePatcher.h"
#include "SDMXFixturePatchTree.h"
#include "Customizations/DMXEntityFixturePatchDetails.h"
#include "IO/DMXPortManager.h"
#include "IO/DMXOutputPort.h"
#include "IO/DMXOutputPortReference.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"

#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SSplitter.h"


#define LOCTEXT_NAMESPACE "SDMXFixturePatcher"

void SDMXFixturePatchEditor::Construct(const FArguments& InArgs)
{
	SDMXEntityEditor::Construct(SDMXEntityEditor::FArguments());

	DMXEditorPtr = InArgs._DMXEditor;
	FixturePatchSharedData = DMXEditorPtr.Pin()->GetFixturePatchSharedData();

	SetCanTick(false);
	bCanSupportFocus = false;

	FixturePatchDetailsView = GenerateFixturePatchDetailsView();

	ChildSlot
	.VAlign(VAlign_Fill)
	.HAlign(HAlign_Fill)
	[
		SNew(SSplitter)
		.Orientation(EOrientation::Orient_Horizontal)
		.ResizeMode(ESplitterResizeMode::FixedPosition)
		
		// Left, entity list & inspector
		+ SSplitter::Slot()	
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Vertical)
			.ResizeMode(ESplitterResizeMode::FixedPosition)

			+SSplitter::Slot()			
			.Value(0.38f)
			[
				SAssignNew(FixturePatchTree, SDMXFixturePatchTree)
				.DMXEditor(DMXEditorPtr)
			]
	
			+SSplitter::Slot()
			.Value(0.62f)
			[
				FixturePatchDetailsView.ToSharedRef()
			]
		]

		// Right, fixture patcher
		+ SSplitter::Slot()	
		[
			SAssignNew(FixturePatcher, SDMXFixturePatcher)
			.DMXEditor(DMXEditorPtr)
		]
	];

	// Adopt the selection
	OnFixturePatchesSelected();

	// Bind to selection changes
	FixturePatchSharedData->OnFixturePatchSelectionChanged.AddSP(this, &SDMXFixturePatchEditor::OnFixturePatchesSelected);
}

void SDMXFixturePatchEditor::RequestRenameOnNewEntity(const UDMXEntity* InEntity, ESelectInfo::Type SelectionType)
{
	check(FixturePatchTree.IsValid());

	FixturePatchTree->SelectItemByEntity(InEntity, SelectionType);
	FixturePatchTree->UpdateTree();
}

void SDMXFixturePatchEditor::SelectEntity(UDMXEntity* InEntity, ESelectInfo::Type InSelectionType /*= ESelectInfo::Type::Direct*/)
{
	check(FixturePatchTree.IsValid());

	FixturePatchTree->SelectItemByEntity(InEntity, InSelectionType);
}

void SDMXFixturePatchEditor::SelectEntities(const TArray<UDMXEntity*>& InEntities, ESelectInfo::Type InSelectionType /*= ESelectInfo::Type::Direct*/)
{
	check(FixturePatchTree.IsValid());

	FixturePatchTree->SelectItemsByEntities(InEntities, InSelectionType);
}

TArray<UDMXEntity*> SDMXFixturePatchEditor::GetSelectedEntities() const
{
	check(FixturePatchTree.IsValid());

	return FixturePatchTree->GetSelectedEntities();
}

void SDMXFixturePatchEditor::SelectUniverse(int32 UniverseID)
{
	check(UniverseID >= 0 && UniverseID <= DMX_MAX_UNIVERSE);

	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		FixturePatchSharedData->SelectUniverse(UniverseID);
	}
}

void SDMXFixturePatchEditor::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	check(FixturePatchTree.IsValid());
	check(FixturePatcher.IsValid());

	FixturePatchTree->UpdateTree();
	FixturePatcher->NotifyPropertyChanged(PropertyChangedEvent);
}

void SDMXFixturePatchEditor::OnFixturePatchesSelected()
{
	TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> SelectedFixturePatches = FixturePatchSharedData->GetSelectedFixturePatches();
	TArray<UObject*> SelectedObjects;
	for (TWeakObjectPtr<UDMXEntityFixturePatch> WeakSelectedFixturePatch : SelectedFixturePatches)
	{
		if (UDMXEntity* SelectedObject = WeakSelectedFixturePatch.Get())
		{
			SelectedObjects.Add(SelectedObject);
		}
	}
	FixturePatchDetailsView->SetObjects(SelectedObjects);
}

TSharedRef<IDetailsView> SDMXFixturePatchEditor::GenerateFixturePatchDetailsView() const
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;

	TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->RegisterInstancedCustomPropertyLayout(UDMXEntityFixturePatch::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FDMXEntityFixturePatchDetails::MakeInstance, DMXEditorPtr));

	return DetailsView;
}

#undef LOCTEXT_NAMESPACE
