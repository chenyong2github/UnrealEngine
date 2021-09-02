// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/FixturePatch/SDMXFixturePatchEditor.h"

#include "DMXEditor.h"
#include "DMXFixturePatchSharedData.h"
#include "SDMXFixturePatcher.h"
#include "SDMXFixturePatchInspector.h"
#include "SDMXFixturePatchTree.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"

#include "Widgets/Layout/SSplitter.h"

#include "DMXSubsystem.h"
#include "IO/DMXPortManager.h"
#include "IO/DMXOutputPort.h"
#include "IO/DMXOutputPortReference.h"


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
				SAssignNew(FixturePatchTree, SDMXFixturePatchTree)
				.DMXEditor(DMXEditorPtr)
				.OnAutoAssignAddressChanged(this, &SDMXFixturePatchEditor::OnFixturePatchTreeChangedAutoAssignAddress)
				.OnEntitiesAdded(this, &SDMXFixturePatchEditor::OnFixturePatchTreeAddedEntities)
				.OnEntityOrderChanged(this, &SDMXFixturePatchEditor::OnFixturePatchTreeChangedEntityOrder)
				.OnEntitiesRemoved(this, &SDMXFixturePatchEditor::OnFixturePatchTreeRemovedEntities)
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
		TSharedPtr<FDMXFixturePatchSharedData> SharedData = DMXEditor->GetFixturePatchSharedData();
		check(SharedData);

		SharedData->SelectUniverse(UniverseID);
	}
}

void SDMXFixturePatchEditor::OnFixturePatchTreeChangedAutoAssignAddress(TArray<UDMXEntityFixturePatch*> ChangedPatches)
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

void SDMXFixturePatchEditor::OnFixturePatchTreeAddedEntities()
{
	check(FixturePatcher.IsValid());
	FixturePatcher->RefreshFromLibrary();
}

void SDMXFixturePatchEditor::OnFixturePatchTreeChangedEntityOrder()
{
	check(FixturePatcher.IsValid());
	FixturePatcher->RefreshFromProperties();
	FixturePatcher->SelectUniverseThatContainsSelectedPatches();
}	

void SDMXFixturePatchEditor::OnFixturePatchTreeRemovedEntities()
{
	check(FixturePatcher.IsValid());
	FixturePatcher->RefreshFromLibrary();
}

void SDMXFixturePatchEditor::OnFixturePatcherPatchedFixturePatch()
{
	check(FixturePatchTree.IsValid());
	FixturePatchTree->UpdateTree();
}

void SDMXFixturePatchEditor::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	check(FixturePatchTree.IsValid());
	check(FixturePatcher.IsValid());

	FixturePatchTree->UpdateTree();
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
