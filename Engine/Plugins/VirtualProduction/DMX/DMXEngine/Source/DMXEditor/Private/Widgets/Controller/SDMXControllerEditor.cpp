// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Controller/SDMXControllerEditor.h"

#include "DMXEditor.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityController.h"
#include "Widgets/SDMXEntityInspector.h"
#include "Widgets/SDMXEntityList.h"

#include "Widgets/Layout/SSplitter.h"


void SDMXControllerEditor::Construct(const FArguments& InArgs)
{
	SDMXEntityEditor::Construct(SDMXEntityEditor::FArguments());

	DMXEditor = InArgs._DMXEditor;

	SetCanTick(false);
	bCanSupportFocus = false;

	// Required to instantiate this ahead of entity list to support Mac
	// Mac will raise OnSelectionUpdated as soon as it is instantiated.
	// At this point Controller Inspector needs to be valid.
	InspectorWidget =
		SNew(SDMXEntityInspectorControllers)
		.DMXEditor(DMXEditor)
		.OnFinishedChangingProperties(this, &SDMXControllerEditor::OnFinishedChangingProperties);

	ChildSlot
	.VAlign(VAlign_Fill)
	.HAlign(HAlign_Fill)
	[
		SNew(SSplitter)
		.Orientation(EOrientation::Orient_Horizontal)
		.ResizeMode(ESplitterResizeMode::FixedPosition)

		// left side container
		+SSplitter::Slot()
		.Value(0.35f)
		[
			SAssignNew(EntityList, SDMXEntityList, UDMXEntityController::StaticClass())
			.DMXEditor(InArgs._DMXEditor)
			.OnSelectionUpdated(this, &SDMXControllerEditor::OnSelectionUpdated)			
		]

		// right side container
		+SSplitter::Slot()
		.Value(0.65f)
		[
			InspectorWidget.ToSharedRef()
		]
	];
}

void SDMXControllerEditor::RequestRenameOnNewEntity(const UDMXEntity* InEntity, ESelectInfo::Type SelectionType)
{
	check(EntityList.IsValid());

	EntityList->UpdateTree();
	EntityList->SelectItemByEntity(InEntity, SelectionType);
	EntityList->OnRenameNode();
}

void SDMXControllerEditor::SelectEntity(UDMXEntity* InEntity, ESelectInfo::Type InSelectionType /*= ESelectInfo::Type::Direct*/)
{
	check(EntityList.IsValid());

	EntityList->SelectItemByEntity(InEntity, InSelectionType);
}

void SDMXControllerEditor::SelectEntities(const TArray<UDMXEntity*>& InEntities, ESelectInfo::Type InSelectionType /*= ESelectInfo::Type::Direct*/)
{
	check(EntityList.IsValid());

	EntityList->SelectItemsByEntity(InEntities, InSelectionType);
}

TArray<UDMXEntity*> SDMXControllerEditor::GetSelectedEntities() const
{
	check(EntityList.IsValid());

	return EntityList->GetSelectedEntities();
}

void SDMXControllerEditor::OnSelectionUpdated(TArray<UDMXEntity*> InSelectedEntities)
{
	check(InspectorWidget.IsValid());
	InspectorWidget->ShowDetailsForEntities(InSelectedEntities);	
}

void SDMXControllerEditor::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (EntityList.IsValid())
	{
		EntityList->UpdateTree();
	}
}
