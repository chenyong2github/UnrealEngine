// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/FixturePatch/SDMXFixturePatchEditor.h"

#include "DMXEditor.h"
#include "DMXFixturePatchSharedData.h"
#include "SDMXFixturePatcher.h"
#include "SDMXFixturePatchInspector.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Widgets/SDMXEntityList.h"

#include "Widgets/Layout/SSplitter.h"


#define LOCTEXT_NAMESPACE "SDMXFixturePatcher"

void SDMXFixturePatchEditor::Construct(const FArguments& InArgs)
{
	SDMXEntityEditor::Construct(SDMXEntityEditor::FArguments());

	DMXEditorPtr = InArgs._DMXEditor;

	SetCanTick(false);
	bCanSupportFocus = false;

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
				SAssignNew(EntityList, SDMXEntityList, UDMXEntityFixturePatch::StaticClass())
				.DMXEditor(DMXEditorPtr)
				.OnAutoAssignAddressChanged(this, &SDMXFixturePatchEditor::OnEntitiyListChangedAutoAssignAddress)
				.OnEntitiesAdded(this, &SDMXFixturePatchEditor::OnEntityListAddedEntities)
				.OnEntityOrderChanged(this, &SDMXFixturePatchEditor::OnEntityListChangedEntityOrder)
				.OnEntitiesRemoved(this, &SDMXFixturePatchEditor::OnEntityListRemovedEntities)
			]
	
			+SSplitter::Slot()
			.Value(0.62f)
			[
				SAssignNew(InspectorWidget, SDMXFixturePatchInspector)
				.DMXEditor(DMXEditorPtr)
				.OnFinishedChangingProperties(this, &SDMXFixturePatchEditor::OnFinishedChangingProperties)
			]
		]

		// Right, fixture patcher
		+ SSplitter::Slot()	
		[
			SAssignNew(FixturePatcher, SDMXFixturePatcher)
			.DMXEditor(DMXEditorPtr)
			.OnPatched(this, &SDMXFixturePatchEditor::OnFixturePatcherPatchedFixturePatch)
		]
	];

	MakeInitialSelection();
}

void SDMXFixturePatchEditor::RequestRenameOnNewEntity(const UDMXEntity* InEntity, ESelectInfo::Type SelectionType)
{
	check(EntityList.IsValid());

	EntityList->UpdateTree();
	EntityList->SelectItemByEntity(InEntity, SelectionType);
	EntityList->OnRenameNode();
}

void SDMXFixturePatchEditor::SelectEntity(UDMXEntity* InEntity, ESelectInfo::Type InSelectionType /*= ESelectInfo::Type::Direct*/)
{
	check(EntityList.IsValid());

	EntityList->SelectItemByEntity(InEntity, InSelectionType);
}

void SDMXFixturePatchEditor::SelectEntities(const TArray<UDMXEntity*>& InEntities, ESelectInfo::Type InSelectionType /*= ESelectInfo::Type::Direct*/)
{
	check(EntityList.IsValid());

	EntityList->SelectItemsByEntity(InEntities, InSelectionType);
}

TArray<UDMXEntity*> SDMXFixturePatchEditor::GetSelectedEntities() const
{
	check(EntityList.IsValid());

	return EntityList->GetSelectedEntities();
}

void SDMXFixturePatchEditor::SelectUniverse(int32 UniverseID)
{
	check(UniverseID >= 0 && UniverseID <= DMX_MAX_UNIVERSE);

	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		TSharedPtr<FDMXFixturePatchSharedData> SharedData = DMXEditor->GetFixturePatchSharedData();
		check(SharedData);

		SharedData->SelectUniverse(UniverseID);
	}
}

void SDMXFixturePatchEditor::OnEntitiyListChangedAutoAssignAddress(TArray<UDMXEntityFixturePatch*> ChangedPatches)
{
	check(FixturePatcher.IsValid());
	check(ChangedPatches.Num() > 0);
	
	FixturePatcher->RefreshFromProperties();

	const bool bAllPatchesInSameUniverse = [&ChangedPatches]()
	{
		const int32 FirstUniverseId = ChangedPatches[0]->GetUniverseID();
		for (UDMXEntityFixturePatch* Patch : ChangedPatches)
		{
			if(Patch->GetUniverseID() != FirstUniverseId)
			{
				return false;
			}
		}
		return true;
	}();
	if(bAllPatchesInSameUniverse
		&& ChangedPatches[0]->GetUniverseID() != INDEX_NONE)
	{
		SelectUniverse(ChangedPatches[0]->GetUniverseID());
	}
}

void SDMXFixturePatchEditor::OnEntityListAddedEntities()
{
	check(FixturePatcher.IsValid());
	FixturePatcher->RefreshFromLibrary();
}

void SDMXFixturePatchEditor::OnEntityListChangedEntityOrder()
{
	check(FixturePatcher.IsValid());
	FixturePatcher->RefreshFromProperties();
	FixturePatcher->SelectUniverseThatContainsSelectedPatches();
}	

void SDMXFixturePatchEditor::OnEntityListRemovedEntities()
{
	check(FixturePatcher.IsValid());
	FixturePatcher->RefreshFromLibrary();
}

void SDMXFixturePatchEditor::OnFixturePatcherPatchedFixturePatch()
{
	check(EntityList.IsValid());
	EntityList->UpdateTree();
}

void SDMXFixturePatchEditor::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	check(EntityList.IsValid());
	check(FixturePatcher.IsValid());

	EntityList->UpdateTree();
	FixturePatcher->NotifyPropertyChanged(PropertyChangedEvent);
}

void SDMXFixturePatchEditor::MakeInitialSelection()
{
	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		TSharedPtr<FDMXFixturePatchSharedData> SharedData = DMXEditor->GetFixturePatchSharedData();
		check(SharedData.IsValid());

		if (SharedData->GetSelectedFixturePatches().Num() == 0)
		{
			UDMXLibrary* Library = DMXEditor->GetDMXLibrary();
			TArray<UDMXEntityFixturePatch*> FixturePatches = Library->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
			if (FixturePatches.Num() > 0)
			{
				SharedData->SelectFixturePatch(FixturePatches[0]);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
