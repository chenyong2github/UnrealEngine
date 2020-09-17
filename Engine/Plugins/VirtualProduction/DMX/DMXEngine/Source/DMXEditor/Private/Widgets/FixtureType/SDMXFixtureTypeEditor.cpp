// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/FixtureType/SDMXFixtureTypeEditor.h"

#include "DMXEditor.h"
#include "DMXFixtureTypeSharedData.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXImport.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixtureType.h"
#include "Widgets/SDMXEntityInspector.h"
#include "Widgets/SDMXEntityList.h"

#include "Widgets/Layout/SSplitter.h"


void SDMXFixtureTypeEditor::Construct(const FArguments& InArgs)
{
	SDMXEntityEditor::Construct(SDMXEntityEditor::FArguments());

	DMXEditor = InArgs._DMXEditor;

	SetCanTick(false);
	bCanSupportFocus = false;

	FixtureSettingsInspector =
		SNew(SDMXEntityInspectorFixtureTypes, EDMXFixtureTypeLayout::FixtureSettings)		
		.DMXEditor(DMXEditor)														
		.OnFinishedChangingProperties(this, &SDMXFixtureTypeEditor::OnFinishedChangingProperties);

	ModesInspector =
		SNew(SDMXEntityInspectorFixtureTypes, EDMXFixtureTypeLayout::Modes)
		.DMXEditor(DMXEditor)
		.OnFinishedChangingProperties(this, &SDMXFixtureTypeEditor::OnFinishedChangingProperties);

	ModePropertiesInspector =
		SNew(SDMXEntityInspectorFixtureTypes, EDMXFixtureTypeLayout::ModeProperties)
		.DMXEditor(DMXEditor)
		.OnFinishedChangingProperties(this, &SDMXFixtureTypeEditor::OnFinishedChangingProperties);

	FunctionsInspector =
		SNew(SDMXEntityInspectorFixtureTypes, EDMXFixtureTypeLayout::Functions)
		.DMXEditor(DMXEditor)
		.OnFinishedChangingProperties(this, &SDMXFixtureTypeEditor::OnFinishedChangingProperties);

	FunctionPropertiesInspector =
		SNew(SDMXEntityInspectorFixtureTypes, EDMXFixtureTypeLayout::FunctionProperties)
		.DMXEditor(DMXEditor)
		.OnFinishedChangingProperties(this, &SDMXFixtureTypeEditor::OnFinishedChangingProperties);

	ChildSlot
	.VAlign(VAlign_Fill)
	.HAlign(HAlign_Fill)
	[
		SNew(SSplitter)
		.Orientation(EOrientation::Orient_Horizontal)
		.ResizeMode(ESplitterResizeMode::FixedPosition)

		// 1st Collumn
		+ SSplitter::Slot()
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Vertical)
			.ResizeMode(ESplitterResizeMode::FixedPosition)

			// Top Row
			+ SSplitter::Slot()
			[
				SAssignNew(EntityList, SDMXEntityList, UDMXEntityFixtureType::StaticClass())
				.DMXEditor(InArgs._DMXEditor)
				.OnSelectionUpdated(this, &SDMXFixtureTypeEditor::OnSelectionUpdated)
			]

			// Bottom Row
			+ SSplitter::Slot()
			[
				FixtureSettingsInspector.ToSharedRef()
			]
		]
			
		// 2nd Collumn
		+ SSplitter::Slot()
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Vertical)
			.ResizeMode(ESplitterResizeMode::FixedPosition)
				
			// Top Row
			+ SSplitter::Slot()			
			[
				ModesInspector.ToSharedRef()
			]

			// Bottom Row
			+ SSplitter::Slot()
			[
				ModePropertiesInspector.ToSharedRef()
			]
		]

		// 3rd Collumn
		+ SSplitter::Slot()
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Vertical)
			.ResizeMode(ESplitterResizeMode::FixedPosition)

			// Top Row
			+ SSplitter::Slot()
			[
				FunctionsInspector.ToSharedRef()
			]

			// Bottom Row
			+ SSplitter::Slot()
			[
				FunctionPropertiesInspector.ToSharedRef()
			]
		]
	];	
}

void SDMXFixtureTypeEditor::RequestRenameOnNewEntity(const UDMXEntity* InEntity, ESelectInfo::Type SelectionType)
{
	check(EntityList.IsValid());

	EntityList->UpdateTree();
	EntityList->SelectItemByEntity(InEntity, SelectionType);
	EntityList->OnRenameNode();
}

void SDMXFixtureTypeEditor::SelectEntity(UDMXEntity* InEntity, ESelectInfo::Type InSelectionType /*= ESelectInfo::Type::Direct*/)
{
	check(EntityList.IsValid());

	EntityList->SelectItemByEntity(InEntity, InSelectionType);
}

void SDMXFixtureTypeEditor::SelectEntities(const TArray<UDMXEntity*>& InEntities, ESelectInfo::Type InSelectionType /*= ESelectInfo::Type::Direct*/)
{
	check(EntityList.IsValid());

	EntityList->SelectItemsByEntity(InEntities, InSelectionType);
}

TArray<UDMXEntity*> SDMXFixtureTypeEditor::GetSelectedEntities() const
{
	check(EntityList.IsValid());

	return EntityList->GetSelectedEntities();
}

void SDMXFixtureTypeEditor::OnSelectionUpdated(TArray<UDMXEntity*> InSelectedEntities)
{
	if (TSharedPtr<FDMXEditor> DMXEditorPtr = DMXEditor.Pin())
	{
		// Set selected fixture types in shared data
		if (TSharedPtr<FDMXFixtureTypeSharedData> SharedData = DMXEditorPtr->GetFixtureTypeSharedData())
		{
			TArray<TWeakObjectPtr<UDMXEntityFixtureType>> SelectedFixtureTypes;
			for (UDMXEntity* Entity : InSelectedEntities)
			{
				if (UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(Entity))
				{
					SelectedFixtureTypes.Add(FixtureType);
				}
			}
			SharedData->SetFixtureTypesBeingEdited(SelectedFixtureTypes);
		}

		check(FixtureSettingsInspector.IsValid());
		check(ModesInspector.IsValid());
		check(ModePropertiesInspector.IsValid());
		check(FunctionsInspector.IsValid());
		check(FunctionPropertiesInspector.IsValid());

		FixtureSettingsInspector->ShowDetailsForEntities(InSelectedEntities);
		ModesInspector->ShowDetailsForEntities(InSelectedEntities);
		ModePropertiesInspector->ShowDetailsForEntities(InSelectedEntities);
		FunctionsInspector->ShowDetailsForEntities(InSelectedEntities);
		FunctionPropertiesInspector->ShowDetailsForEntities(InSelectedEntities);
	}
}

void SDMXFixtureTypeEditor::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
	{
		if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes)
			|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions))
		{
			// When the user adds a Mode or Function, their names can't be empty

			const TArray<UDMXEntity*>&& SelectedEntities = EntityList->GetSelectedEntities();

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
			const TArray<UDMXEntity*>&& SelectedEntities = EntityList->GetSelectedEntities();

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

	if (EntityList.IsValid())
	{
		EntityList->UpdateTree();
	}
}
