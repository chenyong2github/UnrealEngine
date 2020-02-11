// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXEntityEditor.h"

#include "DMXEditor.h"
#include "DMXEditorLog.h"
#include "DMXProtocolConstants.h"
#include "DMXEditorMacros.h"

#include "Widgets/SDMXEntityInspector.h"
#include "Widgets/SDMXEntityList.h"

#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXImport.h"

#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDMXEntityEditor"

void SDMXEntityEditor::Construct(const FArguments &InArgs)
{
	DMXEditor = InArgs._DMXEditor;
	EditorEntityType = InArgs._EditorEntityType;

	if (EditorEntityType->IsChildOf(UDMXEntityController::StaticClass()))
	{
		InspectorWidget = SNew(SDMXEntityInspectorControllers)
					.DMXEditor(DMXEditor)
					.OnFinishedChangingProperties(this, &SDMXEntityEditor::OnFinishedChangingProperties);
	}
	else if (EditorEntityType->IsChildOf(UDMXEntityFixtureType::StaticClass()))
	{
		InspectorWidget = SNew(SDMXEntityInspectorFixtureTypes)
					.DMXEditor(DMXEditor)
					.OnFinishedChangingProperties(this, &SDMXEntityEditor::OnFinishedChangingProperties);
	}
	else if (EditorEntityType->IsChildOf(UDMXEntityFixturePatch::StaticClass()))
	{
		InspectorWidget = SNew(SDMXEntityInspectorFixturePatches)
					.DMXEditor(DMXEditor)
					.OnFinishedChangingProperties(this, &SDMXEntityEditor::OnFinishedChangingProperties);
	}

	check(InspectorWidget.IsValid());

	ChildSlot
	.VAlign(VAlign_Fill)
	.HAlign(HAlign_Fill)
	[
		SAssignNew(SidesSplitter, SSplitter)
		.Orientation(EOrientation::Orient_Horizontal)
		.ResizeMode(ESplitterResizeMode::FixedPosition)

		// left side container
		+SSplitter::Slot()
		.Value(0.35f)
		[
			SAssignNew(ListWidget, SDMXEntityList, EditorEntityType)
			.DMXEditor(InArgs._DMXEditor)
			.OnSelectionUpdated(this, &SDMXEntityEditor::OnSelectionUpdated)
		]

		// right side container
		+SSplitter::Slot()
		.Value(0.65f)
		[
			InspectorWidget.ToSharedRef()
		]
	];
}

void SDMXEntityEditor::RequestRenameOnNewEntity(const UDMXEntity* InEntity, ESelectInfo::Type SelectionType)
{
	check(ListWidget.IsValid());

	ListWidget->UpdateTree();
	ListWidget->SelectItemByEntity(InEntity, SelectionType);
	ListWidget->OnRenameNode();
}

void SDMXEntityEditor::SelectEntity(UDMXEntity* InEntity, ESelectInfo::Type InSelectionType /*= ESelectInfo::Type::Direct*/)
{
	check(ListWidget.IsValid());

	ListWidget->SelectItemByEntity(InEntity, InSelectionType);
}

void SDMXEntityEditor::SelectEntities(const TArray<UDMXEntity*>& InEntities, ESelectInfo::Type InSelectionType /*= ESelectInfo::Type::Direct*/)
{
	check(ListWidget.IsValid());

	ListWidget->SelectItemsByEntity(InEntities, InSelectionType);
}

TArray<UDMXEntity*> SDMXEntityEditor::GetSelectedEntities() const
{
	return ListWidget->GetSelectedEntities();
}

void SDMXEntityEditor::OnSelectionUpdated(TArray<TSharedPtr<FDMXTreeNodeBase>> InSelectedNodes)
{
	check(GetInspectorWidget().IsValid());

	if (TSharedPtr<FDMXEditor> DMXEditorPtr = DMXEditor.Pin())
	{
		UDMXLibrary* DMXLibrary = DMXEditorPtr->GetDMXLibrary();

		TArray<UObject*> SelectedObjects;
		for (TSharedPtr<FDMXTreeNodeBase> Node : InSelectedNodes)
		{
			if (UDMXEntity* Entity = Node->GetEntity())
			{
				SelectedObjects.Add(Entity);
			}
		}

		// Update property inspector if we changed the selection or if there are no entities in the list.
		// So, if the user de-selects all entities, the last one's properties is still shown. But if they
		// delete all entities, we empty the inspector.
		if (SelectedObjects.Num() > 0 || ListWidget->IsListEmpty())
		{
			GetInspectorWidget()->ShowDetailsForEntities(SelectedObjects);
		}
	}
}

void SDMXEntityEditor::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (ListWidget.IsValid())
	{
		ListWidget->UpdateTree();
	}
}

void SDMXControllers::Construct(const FArguments& InArgs)
{
	SDMXEntityEditor::Construct(SDMXEntityEditor::FArguments()
		.DMXEditor(InArgs._DMXEditor)
		.EditorEntityType(UDMXEntityController::StaticClass())
	);
}

void SDMXFixtureTypes::Construct(const FArguments& InArgs)
{
	SDMXEntityEditor::Construct(SDMXEntityEditor::FArguments()
		.DMXEditor(InArgs._DMXEditor)
		.EditorEntityType(UDMXEntityFixtureType::StaticClass())
	);
}

void SDMXFixtureTypes::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
	{
		if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes)
			|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions)
			|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, SubFunctions))
		{
			// When the user adds a Mode, Function or Sub Function, their names can't be empty

			const TArray<UDMXEntity*>&& SelectedEntities = ListWidget->GetSelectedEntities();

			for (UDMXEntity* Entity : SelectedEntities)
			{
				if (UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(Entity))
				{
					FDMXEditorUtils::SetNewFixtureFunctionsNames(FixtureType);
				}
			}
		}
	}
	else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
	{
		if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, DMXImport))
		{
			const TArray<UDMXEntity*>&& SelectedEntities = ListWidget->GetSelectedEntities();

			for (UDMXEntity* Entity : SelectedEntities)
			{
				if (UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(Entity))
				{
					if (FixtureType->DMXImport != nullptr && FixtureType->DMXImport->IsValidLowLevelFast())
					{
						FixtureType->Modify();
						FixtureType->SetModesFromDMXImport(FixtureType->DMXImport);
					}
				}
			}
		}
	}

	SDMXEntityEditor::OnFinishedChangingProperties(PropertyChangedEvent);
}

void SDMXFixturePatch::Construct(const FArguments& InArgs)
{
	SDMXEntityEditor::Construct(SDMXEntityEditor::FArguments()
		.DMXEditor(InArgs._DMXEditor)
		.EditorEntityType(UDMXEntityFixturePatch::StaticClass())
	);
}

#undef LOCTEXT_NAMESPACE